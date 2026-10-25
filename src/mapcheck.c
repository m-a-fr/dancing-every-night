#include "mapcheck.h"
#include "chart.h"
#include "files.h"
#include "mapimport.h"
#include "sdl.h"
#include "third_party/pl_mpeg.h"
#include "third_party/stb_image.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"

#ifndef __vita__
#  include <unistd.h>
#endif

#define MAX_FILES        64
#define MAX_TEXTURE      2048                 /* plus grande texture que le GPU de la Vita accepte */
#define VIDEO_MAX_PIXELS (640 * 368)          /* au-delà, le décodage logiciel risque de saccader sur la Vita */
#define VIDEO_MAX_FPS    30.0
#define AUDIO_WARN_BYTES (15 * 1024 * 1024)   /* la piste est chargée entière en mémoire */
#define MAP_WARN_BYTES   (100LL * 1024 * 1024)

#define MAX_MESSAGES 200

typedef struct { char name[24], file[64]; int notes, order; float bpm; } DiffMeta;

/* Rapport : texte lisible par défaut ; avec --json, les messages sont gardés et tout sort en un objet JSON à la fin
   (métadonnées de la chanson comprises : sert à la bibliothèque, tools/catalog.py) */
typedef struct {
    int errors, warnings;
    bool json;
    int nmsg;
    struct { char level[6]; char text[300]; } msg[MAX_MESSAGES];
    char id[CHART_ID_MAX], title[64], artist[64], pack[64], mapper[64], source[64], license[64];
    int version, ndiff, duration_ms;
    DiffMeta diff[MAX_FILES];
    long long size_bytes;
    bool has_background, has_video;
} Report;

static void say(Report *r, const char *level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (r->json) {
        if (r->nmsg < MAX_MESSAGES && strcmp(level, "OK") != 0) {
            snprintf(r->msg[r->nmsg].level, sizeof r->msg[0].level, "%s", level);
            vsnprintf(r->msg[r->nmsg].text, sizeof r->msg[0].text, fmt, ap);
            r->nmsg++;
        }
    } else {
        printf("%-5s ", level);
        vprintf(fmt, ap);
        printf("\n");
    }
    va_end(ap);
    if (strcmp(level, "ERROR") == 0) r->errors++;
    else if (strcmp(level, "WARN") == 0) r->warnings++;
}

static bool has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s), k = strlen(suffix);
    return n > k && SDL_strcasecmp(s + n - k, suffix) == 0;
}

/* Le texte s'affiche-t-il avec la police du jeu (ASCII + Latin-1) ? */
static bool latin1_only(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            unsigned cp = ((unsigned)(*p & 0x1F) << 6) | (p[1] & 0x3F);
            if (cp > 0xFF) return false;
            p += 2;
            continue;
        }
        return false;
    }
    return true;
}

static void check_text(Report *r, const char *chart, const char *key, const char *value) {
    if (value[0] && !latin1_only(value))
        say(r, "WARN", "%s: %s \"%s\" has characters the game font cannot show (only Latin-1)", chart, key, value);
}

/* Médias nommés par une carte (lignes AUDIO / BACKGROUND / VIDEO, telles qu'écrites) */
typedef struct { char audio[64], background[64], video[64]; } MediaNames;

static void read_media_names(const char *path, MediaNames *m) {
    *m = (MediaNames){0};
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256], key[16], value[64];
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, " %15s %63s", key, value) != 2) continue;
        if (strcmp(key, "AUDIO") == 0) snprintf(m->audio, sizeof m->audio, "%s", value);
        else if (strcmp(key, "BACKGROUND") == 0) snprintf(m->background, sizeof m->background, "%s", value);
        else if (strcmp(key, "VIDEO") == 0) snprintf(m->video, sizeof m->video, "%s", value);
    }
    fclose(f);
}

static long long file_size(const char *path) {
    long long size = -1, mtime;
    return files_stat(path, &size, &mtime) ? size : -1;
}

/* Retourne la durée de la piste en ms (-1 si illisible) */
static int check_audio(Report *r, const char *dir, const char *name) {
    char path[CHART_PATH];
    snprintf(path, sizeof path, "%s%s", dir, name);
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_filename(path, &err, NULL);
    if (!v) { say(r, "ERROR", "%s: not a readable Ogg Vorbis file (the game only plays .ogg Vorbis)", name); return -1; }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    float seconds = stb_vorbis_stream_length_in_seconds(v);
    say(r, "OK", "%s: Ogg Vorbis, %u Hz, %d channel(s), %.0f s", name, info.sample_rate, info.channels, seconds);
    stb_vorbis_close(v);
    long long size = file_size(path);
    if (size > AUDIO_WARN_BYTES)
        say(r, "WARN", "%s: %.1f MB, loaded whole into memory on the Vita: a lower bitrate is recommended",
            name, (double)size / 1048576.0);
    return (int)(seconds * 1000.0f);
}

static void check_image(Report *r, const char *dir, const char *name) {
    char path[CHART_PATH];
    snprintf(path, sizeof path, "%s%s", dir, name);
    int w, h, n;
    if (!stbi_info(path, &w, &h, &n)) { say(r, "ERROR", "%s: not a readable JPEG or PNG image", name); return; }
    if (w > MAX_TEXTURE || h > MAX_TEXTURE)
        say(r, "ERROR", "%s: %dx%d is too large for the Vita (%d px max per side; 960x544 is ideal)", name, w, h, MAX_TEXTURE);
    else
        say(r, "OK", "%s: %dx%d image%s", name, w, h, (w != 960 || h != 544) ? " (cropped to fill 960x544)" : "");
}

static void check_video(Report *r, const char *dir, const char *name) {
    char path[CHART_PATH];
    snprintf(path, sizeof path, "%s%s", dir, name);
    plm_t *plm = plm_create_with_filename(path);
    if (!plm || !plm_probe(plm, 5000 * 1024) || plm_get_num_video_streams(plm) == 0) {
        say(r, "ERROR", "%s: not a readable MPEG-1 video (.mpg); other codecs are not supported", name);
        if (plm) plm_destroy(plm);
        return;
    }
    int w = plm_get_width(plm), h = plm_get_height(plm);
    double fps = plm_get_framerate(plm);
    say(r, "OK", "%s: MPEG-1 video, %dx%d, %.0f fps, %.0f s", name, w, h, fps, plm_get_duration(plm));
    if (w * h > VIDEO_MAX_PIXELS || fps > VIDEO_MAX_FPS + 0.5)
        say(r, "WARN", "%s: may stutter on the Vita (480x272 at 24 fps is recommended)", name);
    if (plm_get_num_audio_streams(plm) > 0)
        say(r, "WARN", "%s: contains an audio track the game ignores (the song plays from the AUDIO file)", name);
    plm_destroy(plm);
}

/* Vérifie le dossier `dir` ('/' final) d'une map ; expected_id = dossier ou ID trouvé à l'import */
static void check_folder(Report *r, const char *dir, const char *expected_id) {
    DIR *d = opendir(dir);
    if (!d) { say(r, "ERROR", "cannot open %s", dir); return; }
    char files[MAX_FILES][64];
    bool used[MAX_FILES] = {0};
    int nfiles = 0;
    long long total = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[CHART_PATH];
        if (snprintf(path, sizeof path, "%s%s", dir, e->d_name) >= (int)sizeof path) { say(r, "ERROR", "%s: path too long", e->d_name); continue; }
        if (files_is_dir(path)) { say(r, "ERROR", "%s/: sub-folders are not allowed (one song = one folder)", e->d_name); continue; }
        if (strlen(e->d_name) >= sizeof files[0]) { say(r, "ERROR", "%s: file name too long", e->d_name); continue; }
        if (nfiles == MAX_FILES) { say(r, "ERROR", "more than %d files in the song", MAX_FILES); break; }
        snprintf(files[nfiles++], sizeof files[0], "%s", e->d_name);
        total += file_size(path);
    }
    closedir(d);

    char id[CHART_ID_MAX] = "", difficulties[MAX_FILES][24];
    int charts = 0, end_ms = -1, audio_ms = -1, orders[MAX_FILES], ordered = 0;
    MediaNames media_all[MAX_FILES];
    for (int i = 0; i < nfiles; i++) {
        if (!has_suffix(files[i], ".chart")) continue;
        used[i] = true;
        char path[CHART_PATH];
        snprintf(path, sizeof path, "%s%s", dir, files[i]);
        Chart c;
        if (!chart_load(&c, path)) { say(r, "ERROR", "%s: unreadable chart", files[i]); continue; }
        const char *f = files[i];
        int errors_before = r->errors;
        if (c.format != 1) say(r, "ERROR", "%s: missing format line (FORMAT 1)", f);
        if (!c.id[0]) say(r, "ERROR", "%s: missing or invalid song ID (ID: letters, digits, _ and - only)", f);
        else if (!id[0]) snprintf(id, sizeof id, "%s", c.id);
        else if (strcmp(id, c.id) != 0) say(r, "ERROR", "%s: song ID %s differs from %s (all difficulties need the same ID)", f, c.id, id);
        if (!c.title[0]) say(r, "WARN", "%s: missing title (TITLE): the menu will show the file name", f);
        if (!c.difficulty[0]) say(r, "WARN", "%s: missing difficulty name (DIFFICULTY)", f);
        if (!c.mapper[0]) say(r, "WARN", "%s: missing mapper (MAPPER): your name, shown to players", f);
        check_text(r, f, "TITLE", c.title);
        check_text(r, f, "ARTIST", c.artist);
        check_text(r, f, "PACK", c.pack);
        check_text(r, f, "DIFFICULTY", c.difficulty);
        if (c.bad_lines > 0) say(r, "WARN", "%s: %d line(s) ignored (see the messages above)", f, c.bad_lines);
        if (c.note_count == 0) say(r, "ERROR", "%s: no notes", f);
        if (c.end_ms >= 0) {
            if (c.end_ms > end_ms) end_ms = c.end_ms;
            int last = 0;
            for (int k = 0; k < c.note_count; k++) {
                int e = c.offset_ms + c.notes[k].time_ms + c.notes[k].duration_ms;
                if (e > last) last = e;
            }
            if (last > c.end_ms)
                say(r, "WARN", "%s: notes after the end of the song (END): the game ends after the last note instead", f);
        }
        for (int k = 1; k < c.note_count; k++)
            if (c.notes[k].time_ms < c.notes[k - 1].time_ms) {
                say(r, "ERROR", "%s: notes must be sorted by time (first problem near beat %d)", f,
                    (int)(c.notes[k].time_ms / (60000.0f / c.bpm)));
                break;
            }
        for (int k = 0; k < charts; k++)
            if (SDL_strcasecmp(difficulties[k], c.difficulty) == 0)
                say(r, "ERROR", "%s: difficulty %s appears twice (best scores would be mixed up)", f, c.difficulty);
        if (c.order > 0)
            for (int k = 0; k < charts; k++)
                if (orders[k] == c.order)
                    say(r, "WARN", "%s: ORDER %d already used by %s (their order in the menu is not guaranteed)", f, c.order,
                        difficulties[k]);
        orders[charts] = c.order;
        ordered += c.order > 0;
        snprintf(difficulties[charts], sizeof difficulties[0], "%s", c.difficulty);
        if (charts == 0) {
            snprintf(r->title, sizeof r->title, "%s", c.title);   snprintf(r->artist, sizeof r->artist, "%s", c.artist);
            snprintf(r->pack, sizeof r->pack, "%s", c.pack);       snprintf(r->mapper, sizeof r->mapper, "%s", c.mapper);
            snprintf(r->source, sizeof r->source, "%s", c.source); snprintf(r->license, sizeof r->license, "%s", c.license);
            r->version = c.version;
        }
        if (charts < MAX_FILES) {
            DiffMeta *dm = &r->diff[charts];
            snprintf(dm->name, sizeof dm->name, "%s", c.difficulty);
            snprintf(dm->file, sizeof dm->file, "%s", f);
            dm->notes = c.note_count; dm->bpm = c.bpm; dm->order = c.order;
            r->ndiff = charts + 1;
        }
        read_media_names(path, &media_all[charts]);
        if (!media_all[charts].audio[0]) say(r, "WARN", "%s: no audio track (AUDIO): only a metronome will play", f);
        if (r->errors == errors_before)
            say(r, "OK", "%s: %s, %d notes, %.0f BPM", f, c.difficulty[0] ? c.difficulty : "?", c.note_count, c.bpm);
        chart_free(&c);
        charts++;
    }
    if (charts == 0) say(r, "ERROR", "no .chart file in the song");
    if (ordered > 0 && ordered < charts)
        say(r, "WARN", "ORDER set on %d of %d difficulties: the others come last in the menu", ordered, charts);
    if (id[0] && expected_id && strcmp(id, expected_id) != 0)
        say(r, "WARN", "folder name %s differs from the song ID %s (the game installs the song as %s)", expected_id, id, id);

    /* Médias nommés par les cartes : présents, simples noms de fichiers, lisibles par le jeu */
    for (int k = 0; k < charts; k++) {
        const char *names[3] = { media_all[k].audio, media_all[k].background, media_all[k].video };
        for (int t = 0; t < 3; t++) {
            if (!names[t][0]) continue;
            int found = -1;
            for (int i = 0; i < nfiles; i++) if (strcmp(files[i], names[t]) == 0) found = i;
            if (found < 0) {
                say(r, "ERROR", "%s is used by a chart but is not in the song folder (file names only, no paths)", names[t]);
                continue;
            }
            if (used[found]) continue;   /* déjà vérifié */
            used[found] = true;
            if (t == 0) audio_ms = check_audio(r, dir, names[t]);
            else if (t == 1) check_image(r, dir, names[t]);
            else check_video(r, dir, names[t]);
        }
    }
    if (end_ms >= 0 && audio_ms > 0 && end_ms > audio_ms + 500)
        say(r, "WARN", "the end of the song (END) is %.1f s after the end of the audio track: silence before the results",
            (end_ms - audio_ms) / 1000.0);
    for (int i = 0; i < nfiles; i++)
        if (!used[i]) say(r, "WARN", "%s: not used by any chart (it would be shipped for nothing)", files[i]);
    snprintf(r->id, sizeof r->id, "%s", id);
    r->size_bytes = total;
    r->duration_ms = audio_ms;
    for (int k = 0; k < charts; k++) {
        for (int i = 0; i < nfiles; i++) {
            if (media_all[k].background[0] && strcmp(files[i], media_all[k].background) == 0) r->has_background = true;
            if (media_all[k].video[0] && strcmp(files[i], media_all[k].video) == 0) r->has_video = true;
        }
    }
    if (total > MAP_WARN_BYTES) say(r, "WARN", "the song weighs %.0f MB: players download it, keep it light", (double)total / 1048576.0);
    else say(r, "OK", "song size: %.1f MB", (double)total / 1048576.0);
    if (id[0] && !r->json) printf("Song ID: %s\n", id);
}

static void json_string(const char *s) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p == '\n') fputs("\\n", stdout);
        else if (*p < 0x20) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

static void json_field(const char *key, const char *value, bool *first) {
    printf("%s\"%s\": ", *first ? "" : ", ", key);
    *first = false;
    json_string(value);
}

static void print_json(const Report *r) {
    bool f = true;
    printf("{");
    printf("\"ok\": %s, \"errors\": %d, \"warnings\": %d", r->errors ? "false" : "true", r->errors, r->warnings);
    f = false;
    json_field("id", r->id, &f); json_field("title", r->title, &f); json_field("artist", r->artist, &f);
    json_field("pack", r->pack, &f); json_field("mapper", r->mapper, &f); json_field("source", r->source, &f);
    json_field("license", r->license, &f);
    printf(", \"version\": %d, \"duration_s\": %.1f, \"size_bytes\": %lld", r->version, r->duration_ms / 1000.0, r->size_bytes);
    printf(", \"has_background\": %s, \"has_video\": %s", r->has_background ? "true" : "false", r->has_video ? "true" : "false");
    printf(", \"difficulties\": [");
    for (int i = 0; i < r->ndiff; i++) {
        bool g = true;
        printf("%s{", i ? ", " : "");
        json_field("name", r->diff[i].name, &g); json_field("file", r->diff[i].file, &g);
        printf(", \"notes\": %d, \"bpm\": %.2f, \"order\": %d}", r->diff[i].notes, r->diff[i].bpm, r->diff[i].order);
    }
    printf("], \"messages\": [");
    for (int i = 0; i < r->nmsg; i++) {
        bool g = true;
        printf("%s{", i ? ", " : "");
        json_field("level", r->msg[i].level, &g); json_field("text", r->msg[i].text, &g);
        printf("}");
    }
    printf("]}\n");
}

int mapcheck_run(const char *path, bool json) {
    static Report r;          /* ~60 Ko : pas sur la pile */
    r = (Report){0};
    r.json = json;
    if (!json) printf("Checking %s\n", path);
    if (has_suffix(path, ".zip")) {
#ifdef __vita__
        say(&r, "ERROR", "zip check is only available on desktop");
#else
        const char *tmp_root = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        char tmp[CHART_PATH], dest[CHART_PATH], id[CHART_ID_MAX] = "";
        snprintf(tmp, sizeof tmp, "%s/dne_check_XXXXXX", tmp_root);
        if (!files_mkdtemp(tmp)) { say(&r, "ERROR", "cannot create a temporary folder"); if (json) print_json(&r); return 1; }
        snprintf(dest, sizeof dest, "%s/map/", tmp);
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        const char *problem = mapimport_unpack(path, base, dest, id, sizeof id);
        if (problem) say(&r, "ERROR", "zip rejected by the game: %s", problem);
        else check_folder(&r, dest, id);
        files_remove_tree(tmp);
#endif
    } else {
        char dir[CHART_PATH];
        snprintf(dir, sizeof dir, "%s%s", path, path[0] && path[strlen(path) - 1] == '/' ? "" : "/");
        char name[CHART_ID_MAX] = "";
        size_t n = strlen(dir) - 1;
        size_t start = n;
        while (start > 0 && dir[start - 1] != '/') start--;
        snprintf(name, sizeof name, "%.*s", (int)(n - start), dir + start);
        check_folder(&r, dir, name);
    }
    if (json) { print_json(&r); return r.errors ? 1 : 0; }
    printf("\n%s: %d error(s), %d warning(s)\n", r.errors ? "FAILED" : "PASSED", r.errors, r.warnings);
    return r.errors ? 1 : 0;
}
