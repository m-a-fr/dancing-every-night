#include "mapimport.h"
#include "config.h"
#include "chart.h"
#include "files.h"
#include "sdl.h"
#include "third_party/miniz.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STAGING_DIR      USER_SONGS_DIR ".import_tmp/"
#define FAILED_DIR       IMPORT_DIR "failed/"
#define MAX_CHART_BYTES  (1024 * 1024)   /* une carte lue pour son ID : au-delà, ce n'est pas une carte */

static bool has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s), k = strlen(suffix);
    return n > k && SDL_strcasecmp(s + n - k, suffix) == 0;
}

static bool valid_id(const char *id) {
    if (!id[0] || strlen(id) >= CHART_ID_MAX) return false;
    for (const char *c = id; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
            return false;
    return true;
}

int mapimport_scan(MapImport *mi) {
    *mi = (MapImport){0};
    files_mkdir(SAVE_DIR);
    files_mkdir(USER_SONGS_DIR);
    files_mkdir(IMPORT_DIR);   /* créé d'avance : le joueur le trouve dans VitaShell */
    DIR *d = opendir(IMPORT_DIR);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && mi->count < MAPIMPORT_MAX) {
        if (e->d_name[0] == '.' || strcmp(e->d_name, "failed") == 0 ||
            strcmp(e->d_name, "__MACOSX") == 0 || strlen(e->d_name) >= sizeof mi->zips[0]) continue;
        char path[256];
        snprintf(path, sizeof path, "%s%s", IMPORT_DIR, e->d_name);
        if (!has_suffix(e->d_name, ".zip") && !files_is_dir(path)) continue;
        snprintf(mi->zips[mi->count++], sizeof mi->zips[0], "%s", e->d_name);
    }
    closedir(d);
    return mi->count;
}

/* Fichier à ignorer dans un zip : métadonnées de macOS, fichiers cachés */
static bool ignored_entry(const char *name) {
    if (strncmp(name, "__MACOSX/", 9) == 0) return true;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    return base[0] == '.';
}

/* Zip refusé : déplacé dans import/failed/ avec la raison dans <zip>.txt */
static bool reject(MapImport *mi, const char *zip, const char *reason) {
    fprintf(stderr, "import: %s refusé : %s\n", zip, reason);
    char from[256], to[256], txt[256];
    snprintf(from, sizeof from, "%s%s", IMPORT_DIR, zip);
    snprintf(to, sizeof to, "%s%s", FAILED_DIR, zip);
    snprintf(txt, sizeof txt, "%s%s.txt", FAILED_DIR, zip);
    files_mkdir(FAILED_DIR);
    if (files_is_dir(to)) files_remove_tree(to);
    else files_remove(to);
    /* Conserver la source si la mise en quarantaine échoue. */
    files_rename(from, to);
    FILE *f = fopen(txt, "w");
    if (f) { fprintf(f, "%s\n", reason); fclose(f); }
    files_remove_tree(STAGING_DIR);
    mi->failed++;
    return false;
}

/* ID déclaré par une carte (ligne « ID xxx »), lu dans le zip ; "" s'il n'y en a pas */
static void chart_id_in_zip(mz_zip_archive *za, mz_uint index, char *id, size_t size) {
    id[0] = '\0';
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(za, index, &st) || st.m_uncomp_size > MAX_CHART_BYTES) return;
    size_t n = 0;
    char *text = mz_zip_reader_extract_to_heap(za, index, &n, 0);
    if (!text) return;
    for (char *line = text; line < text + n; ) {
        char *end = memchr(line, '\n', (size_t)(text + n - line));
        if (!end) end = text + n;
        while (line < end && (*line == ' ' || *line == '\t')) line++;
        if (end - line > 3 && strncmp(line, "ID", 2) == 0 && (line[2] == ' ' || line[2] == '\t')) {
            const char *v = line + 3;
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            size_t k = 0;
            while (v + k < end && v[k] != ' ' && v[k] != '\t' && v[k] != '\r' && k + 1 < size) k++;
            memcpy(id, v, k);
            id[k] = '\0';
            break;
        }
        line = end + 1;
    }
    mz_free(text);
}

const char *mapimport_unpack(const char *zip_path, const char *zip_name, const char *dest, char *id, size_t id_size) {
    mz_zip_archive za;
    memset(&za, 0, sizeof za);
    if (!mz_zip_reader_init_file(&za, zip_path, 0)) return "unreadable or damaged zip file";

    /* 1. Organisation : fichiers à la racine, ou tous dans un même dossier, sans sous-dossier */
    char prefix[CHART_ID_MAX + 1] = "";
    bool first = true;
    int charts = 0;
    mz_uint chart_index = 0;
    const char *problem = NULL;
    mz_uint n = mz_zip_reader_get_num_files(&za);
    for (mz_uint k = 0; k < n && !problem; k++) {
        char name[256];
        mz_zip_reader_get_filename(&za, k, name, sizeof name);
        if (mz_zip_reader_is_file_a_directory(&za, k) || ignored_entry(name)) continue;
        const char *slash = strchr(name, '/');
        char top[CHART_ID_MAX + 1] = "";
        if (slash) {
            size_t len = (size_t)(slash - name) + 1;
            if (len >= sizeof top || strchr(slash + 1, '/')) { problem = "sub-folders in the zip (one song = one folder)"; break; }
            memcpy(top, name, len);
            top[len] = '\0';
        }
        if (first) { snprintf(prefix, sizeof prefix, "%s", top); first = false; }
        else if (strcmp(prefix, top) != 0) problem = "files spread over several folders in the zip";
        const char *base = slash ? slash + 1 : name;
        if (!base[0] || strlen(base) >= 64 || strpbrk(base, ":\\")) problem = "invalid file name in the zip";
        if (has_suffix(base, ".chart") && charts++ == 0) chart_index = k;
    }
    if (!problem && charts == 0) problem = "no .chart file in the zip";
    if (problem) { mz_zip_reader_end(&za); return problem; }

    /* 2. ID de la map : ligne ID, sinon dossier du zip, sinon nom du zip */
    chart_id_in_zip(&za, chart_index, id, id_size);
    if (!id[0] && prefix[0]) snprintf(id, id_size, "%.*s", (int)strlen(prefix) - 1, prefix);
    if (!id[0]) snprintf(id, id_size, "%.*s", (int)(strlen(zip_name) - 4), zip_name);
    if (!valid_id(id)) { mz_zip_reader_end(&za); return "invalid song ID (letters, digits, _ and - only)"; }

    /* 3. Extraction dans dest */
    files_remove_tree(dest);
    if (!files_mkdir(dest)) { mz_zip_reader_end(&za); return "cannot create the destination folder"; }
    for (mz_uint k = 0; k < n; k++) {
        char name[256], out[CHART_PATH];
        mz_zip_reader_get_filename(&za, k, name, sizeof name);
        if (mz_zip_reader_is_file_a_directory(&za, k) || ignored_entry(name)) continue;
        snprintf(out, sizeof out, "%s%s", dest, name + strlen(prefix));
        if (!mz_zip_reader_extract_to_file(&za, k, out, 0)) {
            mz_zip_reader_end(&za);
            return "cannot extract the files (memory card full?)";
        }
    }
    mz_zip_reader_end(&za);
    return NULL;
}

/* Copie plate et bornée en mémoire : mêmes noms et organisation que les ZIP. */
static const char *import_folder(const char *source, const char *name, const char *dest,
                                 char *id, size_t id_size, bool wrapper) {
    DIR *d = opendir(source);
    if (!d) return "cannot read the song folder";
    struct dirent *e;
    char child[256] = "", child_name[96] = "", first_chart[256] = "";
    int dirs = 0, files = 0;
    const char *problem = NULL;
    while ((e = readdir(d))) {
        if (ignored_entry(e->d_name) || strcmp(e->d_name, "__MACOSX") == 0) continue;
        char path[256];
        if (snprintf(path, sizeof path, "%s/%s", source, e->d_name) >= (int)sizeof path) {
            problem = "file path too long"; break;
        }
        if (files_is_dir(path)) {
            dirs++;
            snprintf(child, sizeof child, "%s", path);
            snprintf(child_name, sizeof child_name, "%.*s", (int)sizeof child_name - 1, e->d_name);
        } else {
            if (strlen(e->d_name) >= 64 || strpbrk(e->d_name, ":\\")) {
                problem = "invalid file name in the song folder"; break;
            }
            files++;
            if (!first_chart[0] && has_suffix(e->d_name, ".chart"))
                snprintf(first_chart, sizeof first_chart, "%s", path);
        }
    }
    closedir(d);
    if (problem) return problem;
    if (wrapper && dirs == 1 && files == 0)
        return import_folder(child, child_name, dest, id, id_size, false);
    if (dirs) return "sub-folders in the song folder (one song = one folder)";
    if (!first_chart[0]) return "no .chart file in the song folder";
    id[0] = '\0';
    FILE *chart = fopen(first_chart, "rb");
    if (!chart) return "cannot read the .chart file";
    char line[1024];
    size_t read_bytes = 0;
    while (fgets(line, sizeof line, chart) && (read_bytes += strlen(line)) <= MAX_CHART_BYTES) {
        char *v = line;
        while (*v == ' ' || *v == '\t') v++;
        if (strncmp(v, "ID", 2) || (v[2] != ' ' && v[2] != '\t')) continue;
        v += 2;
        while (*v == ' ' || *v == '\t') v++;
        size_t n = strcspn(v, " \t\r\n");
        if (n >= id_size) { fclose(chart); return "invalid song ID"; }
        memcpy(id, v, n); id[n] = '\0'; break;
    }
    fclose(chart);
    if (!id[0]) {
        if (strlen(name) >= id_size) return "invalid song ID";
        snprintf(id, id_size, "%s", name);
    }
    if (!valid_id(id)) return "invalid song ID (letters, digits, _ and - only)";
    files_remove_tree(dest);
    if (!files_mkdir(dest)) return "cannot create the destination folder";
    d = opendir(source);
    if (!d) return "cannot read the song folder";
    while ((e = readdir(d))) {
        if (ignored_entry(e->d_name) || strcmp(e->d_name, "__MACOSX") == 0) continue;
        char in[256], out[256];
        if (snprintf(in, sizeof in, "%s/%s", source, e->d_name) >= (int)sizeof in ||
            snprintf(out, sizeof out, "%s%s", dest, e->d_name) >= (int)sizeof out) {
            problem = "file path too long"; break;
        }
        FILE *src = fopen(in, "rb"), *dst = src ? fopen(out, "wb") : NULL;
        if (!src || !dst) problem = "cannot copy the song files";
        if (src && dst) {
            char buffer[32768];
            size_t n;
            while ((n = fread(buffer, 1, sizeof buffer, src)))
                if (fwrite(buffer, 1, n, dst) != n) { problem = "cannot write the song files (memory card full?)"; break; }
            if (ferror(src)) problem = "cannot read the song files";
        }
        if (src) fclose(src);
        if (dst && fclose(dst) != 0) problem = "cannot finish writing the song files";
        if (problem) break;
    }
    closedir(d);
    return problem;
}

bool mapimport_run(MapImport *mi, int i) {
    const char *zip = mi->zips[i];
    char zip_path[256], id[CHART_ID_MAX];
    snprintf(zip_path, sizeof zip_path, "%s%s", IMPORT_DIR, zip);
    bool folder = files_is_dir(zip_path);
    const char *problem = folder ? import_folder(zip_path, zip, STAGING_DIR, id, sizeof id, true)
                                 : mapimport_unpack(zip_path, zip, STAGING_DIR, id, sizeof id);
    if (problem) return reject(mi, zip, problem);

    char official[CHART_PATH];
    snprintf(official, sizeof official, "%ssongs/%s", ASSET_DIR, id);
    if (files_is_dir(official)) return reject(mi, zip, "song ID already used by a song shipped with the game");

    /* Remplacement de la map de même ID */
    char dest[CHART_PATH];
    snprintf(dest, sizeof dest, "%s%s", USER_SONGS_DIR, id);
    files_remove_tree(dest);
    if (!files_rename(STAGING_DIR, dest)) return reject(mi, zip, "cannot install the song folder");
    if (folder) files_remove_tree(zip_path);
    else files_remove(zip_path);
    fprintf(stderr, "import: %s -> %s\n", zip, dest);
    mi->imported++;
    return true;
}

const char *mapimport_install(const char *zip_path, const char *expect_id) {
    char id[CHART_ID_MAX];
    const char *base = strrchr(zip_path, '/');
    const char *problem = mapimport_unpack(zip_path, base ? base + 1 : zip_path, STAGING_DIR, id, sizeof id);
    if (problem) { files_remove_tree(STAGING_DIR); return problem; }
    if (strcmp(id, expect_id) != 0) { files_remove_tree(STAGING_DIR); return "the song's ID does not match the catalogue"; }

    char official[CHART_PATH];
    snprintf(official, sizeof official, "%ssongs/%s", ASSET_DIR, id);
    if (files_is_dir(official)) { files_remove_tree(STAGING_DIR); return "song ID already used by a song shipped with the game"; }

    char dest[CHART_PATH];
    snprintf(dest, sizeof dest, "%s%s", USER_SONGS_DIR, id);
    files_remove_tree(dest);
    if (!files_rename(STAGING_DIR, dest)) { files_remove_tree(STAGING_DIR); return "cannot install the song folder"; }
    return NULL;
}
