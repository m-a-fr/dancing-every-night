#include "library.h"
#include "config.h"
#include "net.h"
#include "files.h"
#include "mapimport.h"
#include "sha256.h"
#include "ui.h"
#include "shapes.h"
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIB_JSON_MAX (4 * 1024 * 1024)   /* catalogue au plus : quelques centaines de chansons font ~0,5 Mo */

/* Liste (dessin et défilement) */
#define ROWS       10
#define ROW_H      36.0f
#define LIST_X     40.0f
#define LIST_W     540.0f
#define LIST_TOP   128.0f
#define PANEL_X    612.0f
#define PANEL_W    308.0f
#define PANEL_Y    108.0f
#define PANEL_H    (LIST_TOP + ROWS * ROW_H - PANEL_Y)
#define THUMB_W    280
#define THUMB_H    100
#define THUMB_DIR  SAVE_DIR "download/thumbs/"
#define DOWNLOAD_DIR SAVE_DIR "download/"   /* zips en cours de téléchargement (supprimés après l'installation) */
#define JOB_STACK  (512 * 1024)

/* ---------------------------------------------------------------- lecture du catalogue (JSON) */

typedef struct { const char *p, *end; } Js;

static void ws(Js *j) { while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++; }
static bool eat(Js *j, char c) {
    ws(j);
    if (j->p < j->end && *j->p == c) { j->p++; return true; }
    return false;
}

static void put_utf8(char *out, size_t *n, size_t cap, unsigned cp) {
    char b[4];
    int k;
    if (cp < 0x80) { b[0] = (char)cp; k = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
    else { b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); k = 3; }
    if (!out || *n + (size_t)k + 1 > cap) return;   /* trop long : le texte est coupé, jamais au milieu d'un caractère */
    for (int i = 0; i < k; i++) out[(*n)++] = b[i];
}

/* Chaîne JSON dans out (NULL : ignorée). Les échappements \uXXXX sont rendus en UTF-8. */
static bool js_str(Js *j, char *out, size_t cap) {
    ws(j);
    if (j->p >= j->end || *j->p != '"') return false;
    j->p++;
    size_t n = 0;
    while (j->p < j->end && *j->p != '"') {
        unsigned c = (unsigned char)*j->p++;
        if (c == '\\') {
            if (j->p >= j->end) return false;
            c = (unsigned char)*j->p++;
            if (c == 'n' || c == 't' || c == 'r') c = ' ';
            else if (c == 'u') {
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    if (j->p >= j->end || !isxdigit((unsigned char)*j->p)) return false;
                    char h = *j->p++;
                    cp = cp * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                }
                put_utf8(out, &n, cap, cp >= 0xD800 && cp < 0xE000 ? '?' : cp);   /* moitié de paire de substitution : « ? » */
                continue;
            }
        }
        if (out && n + 1 < cap) out[n++] = (char)c;
    }
    if (j->p >= j->end) return false;
    j->p++;
    if (out) out[n] = '\0';
    return true;
}

static bool js_skip(Js *j, int depth) {
    ws(j);
    if (j->p >= j->end || depth > 8) return false;
    char c = *j->p;
    if (c == '"') return js_str(j, NULL, 0);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        j->p++;
        if (eat(j, close)) return true;
        for (;;) {
            if (c == '{' && (!js_str(j, NULL, 0) || !eat(j, ':'))) return false;
            if (!js_skip(j, depth + 1)) return false;
            if (eat(j, ',')) continue;
            return eat(j, close);
        }
    }
    const char *s = j->p;   /* nombre ou littéral */
    while (j->p < j->end && !strchr(",}] \t\r\n", *j->p)) j->p++;
    return j->p > s;
}

static bool js_num(Js *j, double *v) {
    ws(j);
    char *e;
    *v = strtod(j->p, &e);
    if (e == j->p) return false;
    j->p = e;
    return true;
}

/* Valeur chaîne, ou rien si c'est autre chose (null) : la valeur est ignorée */
static bool js_text(Js *j, char *out, size_t cap) {
    ws(j);
    if (j->p < j->end && *j->p == '"') return js_str(j, out, cap);
    return js_skip(j, 0);
}

static bool valid_id(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n >= CHART_ID_MAX) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-')) return false;
    return true;
}

/* Clé relative au serveur : préfixe imposé, jamais de « .. » ni de caractère inattendu */
static bool valid_key(const char *s, const char *prefix) {
    if (strncmp(s, prefix, strlen(prefix)) != 0 || strstr(s, "..")) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-' || *s == '.' || *s == '/'))
            return false;
    return true;
}

static bool parse_diffs(Js *j, LibSong *s) {
    if (!eat(j, '[')) return js_skip(j, 0);
    if (eat(j, ']')) return true;
    for (;;) {
        if (!eat(j, '{')) return false;
        LibDiff d = {{0}, 0};
        if (!eat(j, '}')) for (;;) {
            char key[24];
            double v;
            if (!js_str(j, key, sizeof key) || !eat(j, ':')) return false;
            if (!strcmp(key, "name")) { if (!js_text(j, d.name, sizeof d.name)) return false; }
            else if (!strcmp(key, "notes")) { if (!js_num(j, &v)) return false; d.notes = (int)v; }
            else if (!js_skip(j, 0)) return false;
            if (eat(j, ',')) continue;
            if (!eat(j, '}')) return false;
            break;
        }
        if (d.name[0] && s->diff_count < LIB_MAX_DIFFS) s->diffs[s->diff_count++] = d;
        if (eat(j, ',')) continue;
        return eat(j, ']');
    }
}

/* -1 : JSON mal formé ; 0 : chanson inutilisable (ignorée) ; 1 : lue */
static int parse_song(Js *j, LibSong *s) {
    *s = (LibSong){0};
    if (!eat(j, '{')) return -1;
    if (!eat(j, '}')) for (;;) {
        char key[24];
        double v;
        if (!js_str(j, key, sizeof key) || !eat(j, ':')) return -1;
        bool ok;
        if (!strcmp(key, "id")) ok = js_text(j, s->id, sizeof s->id);
        else if (!strcmp(key, "local_id")) ok = js_text(j, s->local_id, sizeof s->local_id);
        else if (!strcmp(key, "title")) ok = js_text(j, s->title, sizeof s->title);
        else if (!strcmp(key, "artist")) ok = js_text(j, s->artist, sizeof s->artist);
        else if (!strcmp(key, "pack")) ok = js_text(j, s->pack, sizeof s->pack);
        else if (!strcmp(key, "mapper")) ok = js_text(j, s->mapper, sizeof s->mapper);
        else if (!strcmp(key, "added")) ok = js_text(j, s->added, sizeof s->added);
        else if (!strcmp(key, "zip")) ok = js_text(j, s->zip, sizeof s->zip);
        else if (!strcmp(key, "sha256")) ok = js_text(j, s->sha256, sizeof s->sha256);
        else if (!strcmp(key, "thumb")) ok = js_text(j, s->thumb, sizeof s->thumb);
        else if (!strcmp(key, "version")) { ok = js_num(j, &v); s->version = (int)v; }
        else if (!strcmp(key, "duration_s")) { ok = js_num(j, &v); s->duration_s = (int)v; }
        else if (!strcmp(key, "zip_bytes")) { ok = js_num(j, &v); s->zip_bytes = v > 0 && v < 2e9 ? (int)v : 0; }
        else if (!strcmp(key, "difficulties")) ok = parse_diffs(j, s);
        else ok = js_skip(j, 0);
        if (!ok) return -1;
        if (eat(j, ',')) continue;
        if (!eat(j, '}')) return -1;
        break;
    }
    if (!valid_id(s->id) || !valid_key(s->zip, "songs/") || strlen(s->sha256) != 64 || s->version < 1) return 0;
    if (s->local_id[0] && !valid_id(s->local_id)) s->local_id[0] = '\0';
    if (s->thumb[0] && !valid_key(s->thumb, "thumbs/")) s->thumb[0] = '\0';
    if (!s->title[0]) snprintf(s->title, sizeof s->title, "%s", s->id);
    return 1;
}

int library_parse(const char *json, LibSong *out, int max, char *base, size_t base_size) {
    Js j = { json, json + strlen(json) };
    int count = 0;
    if (base_size) base[0] = '\0';
    if (!eat(&j, '{')) return 0;
    for (;;) {
        char key[24];
        if (!js_str(&j, key, sizeof key) || !eat(&j, ':')) return count;
        if (!strcmp(key, "download_base")) {
            char b[160] = "";
            if (!js_text(&j, b, sizeof b)) return count;
            size_t n = strlen(b);
            while (n && b[n - 1] == '/') b[--n] = '\0';
            if (base_size && !strncmp(b, "https://", 8)) snprintf(base, base_size, "%s", b);   /* jamais autre chose que du HTTPS */
        } else if (!strcmp(key, "songs")) {
            if (!eat(&j, '[')) return count;
            if (!eat(&j, ']')) for (;;) {
                LibSong s;
                int r = parse_song(&j, &s);
                if (r < 0) return count;
                if (r > 0 && count < max) out[count++] = s;
                if (eat(&j, ',')) continue;
                if (!eat(&j, ']')) return count;
                break;
            }
        } else if (!js_skip(&j, 0)) return count;
        if (eat(&j, ',')) continue;
        break;
    }
    return count;
}

/* ---------------------------------------------------------------- lecture du réseau (fil d'exécution) */

Library *library_create(SDL_Renderer *r) {
    Library *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    SDL_AtomicSet(&l->status, LIBST_IDLE);
    bgcache_init(&l->thumbs, r);
    l->thumbs.thumb_w = THUMB_W;
    l->thumbs.thumb_h = THUMB_H;
    l->thumbs.slot_count = 12;
    return l;
}

LibStatus library_status(const Library *l) {
    return l ? (LibStatus)SDL_AtomicGet((SDL_atomic_t *)&l->status) : LIBST_IDLE;
}

static LibJob job_state(const Library *l) { return (LibJob)SDL_AtomicGet((SDL_atomic_t *)&l->job); }

bool library_busy(const Library *l) {
    LibJob j = l ? job_state(l) : LIBJ_IDLE;
    return j == LIBJ_DOWNLOAD || j == LIBJ_VERIFY || j == LIBJ_INSTALL;
}

static void drop_catalog(Library *l) {
    free(l->songs);
    free(l->order);
    l->songs = NULL;
    l->order = NULL;
    l->song_count = l->shown = l->sel = l->top = 0;
    l->scroll = 0.0f;
    l->prepared = false;
}

void library_destroy(Library *l) {
    if (!l || library_status(l) == LIBST_LOADING || library_busy(l) || SDL_AtomicGet(&l->thumb_busy) == 1) return;   /* un fil s'en sert encore : on ne libère pas */
    drop_catalog(l);
    bgcache_shutdown(&l->thumbs);
    files_remove_tree(THUMB_DIR);
    free(l);
}

#ifndef __vita__
static char *read_local(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc(LIB_JSON_MAX);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, LIB_JSON_MAX - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}
#endif

static int fetch_thread(void *data) {
    Library *l = data;
    char *json = NULL;
#ifndef __vita__
    const char *local = getenv("VITAGAME_LIBRARY_JSON");   /* essai de l'affichage sans réseau */
    if (local) json = read_local(local);
#endif
    if (!json) {
        if (!net_online()) { net_log("library: offline"); SDL_AtomicSet(&l->status, LIBST_OFFLINE); return 0; }
        int len = 0;
        json = net_get(LIBRARY_URL, LIB_JSON_MAX, &len);
    }
    LibSong *songs = json ? calloc(LIB_MAX_SONGS, sizeof *songs) : NULL;
    int n = songs ? library_parse(json, songs, LIB_MAX_SONGS, l->base, sizeof l->base) : 0;
    net_log("library: catalogue %s, %d bytes, %d songs read (%d bytes per song slot, table %s)", json ? "received" : "NOT received",
            json ? (int)strlen(json) : 0, n, (int)sizeof *songs, songs ? "allocated" : "NOT allocated");
    free(json);
    if (n <= 0) {
        free(songs);
        SDL_AtomicSet(&l->status, json ? LIBST_FAILED : LIBST_OFFLINE);   /* aucune réponse : plutôt une coupure qu'une panne du serveur */
        return 0;
    }
    l->songs = songs;
    l->song_count = n;
    SDL_AtomicSet(&l->status, LIBST_READY);
    return 0;
}

void library_open(Library *l) {
    if (!l || library_status(l) == LIBST_LOADING || library_busy(l)) return;
    SDL_AtomicSet(&l->job, LIBJ_IDLE);
    l->gen++;
    bgcache_clear(&l->thumbs);
    if (SDL_AtomicGet(&l->thumb_busy) != 1) files_remove_tree(THUMB_DIR);
    drop_catalog(l);
    l->base[0] = '\0';
    l->hold = 0.0f;
    SDL_AtomicSet(&l->status, LIBST_LOADING);
    SDL_Thread *t = SDL_CreateThread(fetch_thread, "library", l);
    if (t) SDL_DetachThread(t);
    else SDL_AtomicSet(&l->status, LIBST_FAILED);
}

/* ---------------------------------------------------------------- état dans la bibliothèque du joueur */

/* VERSION de la chanson installée dans root/<id>/ (première carte trouvée), 1 si la ligne manque, -1 si le dossier n'existe pas */
static int installed_version(const char *root, const char *id) {
    char dir[CHART_PATH];
    if (snprintf(dir, sizeof dir, "%s%s/", root, id) >= (int)sizeof dir) return -1;
    DIR *d = opendir(dir);
    if (!d) return -1;
    int version = 1;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.' || n < 7 || strcmp(e->d_name + n - 6, ".chart") != 0) continue;
        char path[CHART_PATH];
        if (snprintf(path, sizeof path, "%s%s", dir, e->d_name) >= (int)sizeof path) continue;
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char line[256];
        for (int i = 0; i < 80 && fgets(line, sizeof line, f); i++) {
            int v;
            if (sscanf(line, "VERSION %d", &v) == 1 && v > 0) { version = v; break; }
        }
        fclose(f);
        break;
    }
    closedir(d);
    return version;
}

static void refresh_state(LibSong *s) {
    s->state = LIB_NEW;
    s->installed_version = 0;
    int v = installed_version(USER_SONGS_DIR, s->id);
    if (v > 0) {
        s->installed_version = v;
        s->state = v < s->version ? LIB_UPDATE : LIB_INSTALLED;
        return;
    }
    /* déjà là sous son ID d'origine (installée à la main avant la bibliothèque, ou chanson officielle) */
    if (s->local_id[0] && (installed_version(USER_SONGS_DIR, s->local_id) > 0 || installed_version(ASSET_DIR "songs/", s->local_id) > 0))
        s->state = LIB_INSTALLED;
}

/* ---------------------------------------------------------------- tri et filtre */

static int cmp_newest(const void *a, const void *b) {
    const LibSong *x = *(const LibSong *const *)a, *y = *(const LibSong *const *)b;
    int c = strcmp(y->added, x->added);
    return c ? c : SDL_strcasecmp(x->title, y->title);
}
static int cmp_title(const void *a, const void *b) {
    const LibSong *x = *(const LibSong *const *)a, *y = *(const LibSong *const *)b;
    return SDL_strcasecmp(x->title, y->title);
}
static int cmp_artist(const void *a, const void *b) {
    const LibSong *x = *(const LibSong *const *)a, *y = *(const LibSong *const *)b;
    int c = SDL_strcasecmp(x->artist, y->artist);
    return c ? c : SDL_strcasecmp(x->title, y->title);
}

static bool passes(const Library *l, const LibSong *s) {
    switch (l->filter) {
    case LIBF_NEW:       return s->state == LIB_NEW;
    case LIBF_INSTALLED: return s->state == LIB_INSTALLED;
    case LIBF_UPDATE:    return s->state == LIB_UPDATE;
    default:             return true;
    }
}

/* Recalcule la liste affichée ; garde la chanson `keep` (indice dans songs) sélectionnée si elle y est encore, sinon se
   place au rang `fallback`. keep_view : la fenêtre de la liste ne bouge pas (la liste ne change que par un état modifié). */
static void rebuild(Library *l, int keep, int fallback, bool keep_view) {
    const int old_top = l->top;
    const float old_scroll = l->scroll;
    free(l->order);
    l->order = NULL;
    l->shown = 0;
    const LibSong **tmp = malloc(sizeof *tmp * (size_t)(l->song_count ? l->song_count : 1));
    l->order = malloc(sizeof *l->order * (size_t)(l->song_count ? l->song_count : 1));
    if (!tmp || !l->order) { free(tmp); free(l->order); l->order = NULL; return; }
    for (int i = 0; i < l->song_count; i++)
        if (passes(l, &l->songs[i])) tmp[l->shown++] = &l->songs[i];
    qsort(tmp, (size_t)l->shown, sizeof *tmp, l->sort == LIBS_TITLE ? cmp_title : l->sort == LIBS_ARTIST ? cmp_artist : cmp_newest);
    l->sel = -1;
    for (int i = 0; i < l->shown; i++) {
        l->order[i] = (int)(tmp[i] - l->songs);
        if (l->order[i] == keep) l->sel = i;
    }
    free(tmp);
    if (l->sel < 0) l->sel = fallback < l->shown ? fallback : l->shown - 1;
    if (l->sel < 0) l->sel = 0;
    if (keep_view) { l->top = old_top; l->scroll = old_scroll; return; }
    l->top = l->sel > 1 ? l->sel - 1 : 0;
    l->scroll = (float)l->top;
}

static void prepare(Library *l) {
    for (int i = 0; i < l->song_count; i++) refresh_state(&l->songs[i]);
    rebuild(l, -1, 0, false);
    l->prepared = true;
}

/* ---------------------------------------------------------------- vignettes */

static void thumb_file(const LibSong *s, char *out, size_t cap) {
    snprintf(out, cap, THUMB_DIR "%s-v%d.jpg", s->id, s->version);
}

static int thumb_thread(void *data) {
    Library *l = data;
    files_mkdir(SAVE_DIR);
    files_mkdir(SAVE_DIR "download/");
    files_mkdir(THUMB_DIR);
    SDL_AtomicSet(&l->thumb_ok, net_download(l->thumb_url, l->thumb_file, 512 * 1024, &l->thumb_prog) ? 1 : 0);
    SDL_AtomicSet(&l->thumb_busy, 2);
    return 0;
}

/* Reçoit la vignette terminée, demande l'affichage de celles qui sont prêtes (sélection et voisines) et lance le
   téléchargement de la prochaine : la sélection d'abord, puis le voisin du dessous, celui du dessus... */
static void thumbs_update(Library *l) {
    if (SDL_AtomicGet(&l->thumb_busy) == 2) {
        if (l->thumb_gen == l->gen && l->thumb_idx < l->song_count)
            l->songs[l->thumb_idx].thumb_state = SDL_AtomicGet(&l->thumb_ok) ? LIBTH_READY : LIBTH_FAILED;
        SDL_AtomicSet(&l->thumb_busy, 0);
    }
    static const int offs[5] = { 0, 1, -1, 2, -2 };
    char paths[5][BGCACHE_PATH];
    const char *ptrs[5];
    int dims[5] = {0}, n = 0, next = -1;
    for (int k = 0; k < 5; k++) {
        int rank = l->sel + offs[k];
        if (rank < 0 || rank >= l->shown) continue;
        const LibSong *s = &l->songs[l->order[rank]];
        if (!s->thumb[0]) continue;
        if (s->thumb_state == LIBTH_READY) { thumb_file(s, paths[n], sizeof paths[n]); ptrs[n] = paths[n]; n++; }
        else if (s->thumb_state == LIBTH_NONE && next < 0) next = l->order[rank];
    }
    bgcache_want(&l->thumbs, ptrs, dims, n);
    if (next < 0 || SDL_AtomicGet(&l->thumb_busy) != 0 || !l->base[0] || !net_online()) return;
    LibSong *s = &l->songs[next];
    if (snprintf(l->thumb_url, sizeof l->thumb_url, "%s/%s", l->base, s->thumb) >= (int)sizeof l->thumb_url) { s->thumb_state = LIBTH_FAILED; return; }
    thumb_file(s, l->thumb_file, sizeof l->thumb_file);
    l->thumb_idx = next;
    l->thumb_gen = l->gen;
    s->thumb_state = LIBTH_LOADING;
    SDL_AtomicSet(&l->thumb_prog.cancel, 0);
    SDL_AtomicSet(&l->thumb_busy, 1);
    SDL_Thread *t = SDL_CreateThread(thumb_thread, "thumb", l);
    if (t) SDL_DetachThread(t);
    else { s->thumb_state = LIBTH_FAILED; SDL_AtomicSet(&l->thumb_busy, 0); }
}

/* ---------------------------------------------------------------- installation */

static void job_fail(Library *l, const char *why) {
    snprintf(l->job_err, sizeof l->job_err, "%s", why);
    SDL_AtomicSet(&l->job, LIBJ_FAILED);
}

static int job_thread(void *data) {
    Library *l = data;
    char path[160];
    files_mkdir(SAVE_DIR);
    files_mkdir(DOWNLOAD_DIR);
    snprintf(path, sizeof path, "%s%s.zip", DOWNLOAD_DIR, l->job_id);
    files_remove(path);
    int cap = l->job_bytes + l->job_bytes / 8 + 65536;   /* la taille annoncée, avec un peu de marge */
    if (!net_download(l->job_url, path, cap, &l->prog)) {
        if (SDL_AtomicGet(&l->prog.cancel)) SDL_AtomicSet(&l->job, LIBJ_IDLE);
        else job_fail(l, "download failed. Check the Wi-Fi and the free space.");
        return 0;
    }
    SDL_AtomicSet(&l->job, LIBJ_VERIFY);
    char sum[65];
    if (!sha256_file(path, sum) || SDL_strcasecmp(sum, l->job_sha) != 0) {
        files_remove(path);
        job_fail(l, "the downloaded file is damaged: try again");
        return 0;
    }
    SDL_AtomicSet(&l->job, LIBJ_INSTALL);
    const char *why = mapimport_install(path, l->job_id);
    files_remove(path);
    if (why) job_fail(l, why);
    else SDL_AtomicSet(&l->job, LIBJ_DONE);
    return 0;
}

static void job_start(Library *l, const LibSong *s) {
    snprintf(l->job_id, sizeof l->job_id, "%s", s->id);
    snprintf(l->job_sha, sizeof l->job_sha, "%s", s->sha256);
    l->job_bytes = s->zip_bytes;
    l->job_err[0] = '\0';
    if (!l->base[0] || snprintf(l->job_url, sizeof l->job_url, "%s/%s", l->base, s->zip) >= (int)sizeof l->job_url) {
        job_fail(l, "this song cannot be downloaded");
        return;
    }
    if (!net_online()) { job_fail(l, "no connection. Connect the console to Wi-Fi."); return; }
    SDL_AtomicSet(&l->prog.cancel, 0);
    SDL_AtomicSet(&l->prog.done, 0);
    SDL_AtomicSet(&l->prog.total, 0);
    SDL_AtomicSet(&l->job, LIBJ_DOWNLOAD);
    SDL_Thread *t = SDL_CreateThreadWithStackSize(job_thread, "install", JOB_STACK, l);
    if (t) SDL_DetachThread(t);
    else job_fail(l, "could not start the download");
}

/* ---------------------------------------------------------------- navigation */

static int hold_steps(float t) { return t < 0.35f ? 0 : 1 + (int)floorf((t - 0.35f) / 0.045f); }

int library_update(Library *l, const Input *in, float dt) {
    int ev = 0;
    l->time += dt;
    bgcache_update(&l->thumbs);   /* téléverse la vignette décodée, même pendant une installation */
    if (library_busy(l)) {   /* installation en cours : Rond l'annule pendant le téléchargement, sinon on attend */
        if (in->pressed[BTN_CIRCLE] && job_state(l) == LIBJ_DOWNLOAD) SDL_AtomicSet(&l->prog.cancel, 1);
        return 0;
    }
    int ev_done = 0;
    if (job_state(l) == LIBJ_DONE) {   /* la chanson est installée : sa pastille et la liste suivent */
        int keep = l->shown > 0 ? l->order[l->sel] : -1;
        for (int i = 0; i < l->song_count; i++)
            if (!strcmp(l->songs[i].id, l->job_id)) refresh_state(&l->songs[i]);
        rebuild(l, keep, l->sel, true);
        SDL_AtomicSet(&l->job, LIBJ_IDLE);
        ev_done = LIB_EV_INSTALLED;
    }
    if (in->pressed[BTN_CIRCLE]) return LIB_EV_BACK | ev_done;
    LibStatus st = library_status(l);
    if (st == LIBST_READY && !l->prepared) prepare(l);
    if ((st == LIBST_OFFLINE || st == LIBST_FAILED) && in->pressed[BTN_CROSS]) {
        library_open(l);
        return LIB_EV_CONFIRM;
    }
    if (st != LIBST_READY || !l->order) return ev;
    thumbs_update(l);

    if (in->pressed[BTN_CROSS] && l->shown > 0) {
        const LibSong *s = &l->songs[l->order[l->sel]];
        if (s->state != LIB_INSTALLED) { job_start(l, s); return ev_done | LIB_EV_CONFIRM; }
    }

    int before = l->sel, step = 0;
    if (in->pressed[BTN_DOWN]) step++;
    if (in->pressed[BTN_UP]) step--;
    if (in->held[BTN_DOWN] != in->held[BTN_UP]) {   /* appui maintenu : la liste défile de plus en plus vite */
        float prev = l->hold;
        l->hold += dt;
        step += (in->held[BTN_DOWN] ? 1 : -1) * (hold_steps(l->hold) - hold_steps(prev));
    } else l->hold = 0.0f;
    if (in->pressed[BTN_R]) step += ROWS - 1;
    if (in->pressed[BTN_L]) step -= ROWS - 1;
    if (l->shown > 0) {
        l->sel += step;
        if (l->sel < 0) l->sel = 0;
        if (l->sel > l->shown - 1) l->sel = l->shown - 1;
    }
    if (l->sel != before) ev |= LIB_EV_CURSOR;

    if (in->pressed[BTN_SQUARE] || in->pressed[BTN_TRIANGLE]) {
        int keep = l->shown > 0 ? l->order[l->sel] : -1;
        if (in->pressed[BTN_SQUARE]) l->filter = (LibFilter)((l->filter + 1) % LIBF_COUNT);
        else l->sort = (LibSort)((l->sort + 1) % LIBS_COUNT);
        rebuild(l, keep, 0, false);
        ev |= LIB_EV_CHANGE;
    }

    /* la sélection reste dans la fenêtre, avec une rangée de marge quand la liste le permet */
    if (l->sel < l->top + 1) l->top = l->sel - 1;
    if (l->sel > l->top + ROWS - 2) l->top = l->sel - ROWS + 2;
    int max_top = l->shown > ROWS ? l->shown - ROWS : 0;
    if (l->top > max_top) l->top = max_top;
    if (l->top < 0) l->top = 0;
    ui_approach(&l->scroll, (float)l->top, 16.0f, dt);
    return ev | ev_done;
}

/* ---------------------------------------------------------------- dessin */

static const FontRot FLAT = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

/* Texte centré verticalement sur cy */
static void text(const Font *f, SDL_Renderer *r, FontFace face, float x, float cy, TextAlign al, float scale, SDL_Color c, const char *s) {
    font_draw_ex(f, r, face, x, cy - (float)font_cap_height(f, face) * 0.5f, al, scale, c, UI_NONE, s);
}

/* Texte en capitales, coupé pour tenir dans max_w px à cette échelle */
static void text_fit(const Font *f, SDL_Renderer *r, FontFace face, float x, float cy, TextAlign al, float scale, SDL_Color c,
                     const char *s, float max_w) {
    char up[160], fitted[160];
    ui_upper(s, up, sizeof up);
    ui_fit_text(f, face, up, (int)(max_w / scale), fitted, sizeof fitted);
    text(f, r, face, x, cy, al, scale, c, fitted);
}

/* Coche (installée) ou flèche haute (mise à jour), de la couleur `c`, dans un cercle de rayon rad */
static void draw_glyph(SDL_Renderer *r, float cx, float cy, float rad, LibState s, SDL_Color c) {
    if (s == LIB_INSTALLED) {
        const float t = rad * 0.26f;
        shapes_thick_line(r, cx - rad * 0.46f, cy + rad * 0.02f, cx - rad * 0.12f, cy + rad * 0.38f, t, c);
        shapes_thick_line(r, cx - rad * 0.12f, cy + rad * 0.38f, cx + rad * 0.48f, cy - rad * 0.36f, t, c);
    } else if (s == LIB_UPDATE) {
        ui_poly(r, (const SDL_FPoint[]){{cx, cy - rad * 0.55f}, {cx - rad * 0.52f, cy + rad * 0.06f}, {cx + rad * 0.52f, cy + rad * 0.06f}}, 3, c);
        shapes_thick_line(r, cx, cy + rad * 0.02f, cx, cy + rad * 0.52f, rad * 0.3f, c);
    }
}

/* Pastille d'état : cercle vide (absente), coche sur cyan (installée), flèche haute sur or (mise à jour) */
static void draw_badge(SDL_Renderer *r, float cx, float cy, float rad, LibState s, Uint8 a) {
    if (s == LIB_NEW) { shapes_circle_outline(r, cx, cy, rad, 1.5f, ui_alpha(CH_WHITE, (Uint8)(a / 5))); return; }
    shapes_filled_circle(r, cx, cy, rad, ui_alpha(s == LIB_INSTALLED ? CH_CYAN : CH_GOLD, a));
    draw_glyph(r, cx, cy, rad, s, ui_alpha(CH_INK, a));
}

static void fmt_size(int bytes, char *out, size_t cap) {
    if (bytes >= 1024 * 1024) snprintf(out, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else snprintf(out, cap, "%d KB", (bytes + 1023) / 1024);
}

static void draw_row(const Library *l, SDL_Renderer *r, const Font *f, const LibSong *s, float y, bool sel) {
    const float cy = y + ROW_H * 0.5f, lean = ROW_H * 0.5f * 0.1405f;
    const bool dim = s->state == LIB_INSTALLED && !sel;   /* déjà dans la bibliothèque : en retrait */
    if (sel) {
        ui_skew_box(r, (SDL_FRect){LIST_X + 4.0f, y + 6.0f, LIST_W - 4.0f, ROW_H - 4.0f}, lean, CH_PINK);
        ui_skew_box(r, (SDL_FRect){LIST_X, y + 2.0f, LIST_W - 4.0f, ROW_H - 4.0f}, lean, CH_INK);
    } else {
        ui_skew_box(r, (SDL_FRect){LIST_X, y + 2.0f, LIST_W - 4.0f, ROW_H - 4.0f}, lean, ui_alpha(CH_INK, dim ? 120 : 175));
    }
    draw_badge(r, LIST_X + 24.0f, cy, 10.0f, s->state, dim ? 190 : 255);
    const SDL_Color tc = sel ? CH_GOLD : dim ? ui_alpha(CH_WHITE, 150) : CH_WHITE;
    text_fit(f, r, FONT_CARD_NAME, LIST_X + 46.0f, cy, ALIGN_LEFT, 0.62f, tc, s->title, 232.0f);
    text_fit(f, r, FONT_CARD, LIST_X + 290.0f, cy, ALIGN_LEFT, 1.0f, ui_alpha(CH_MUTED, dim ? 120 : 199), s->artist, 142.0f);
    float dx = LIST_X + LIST_W - 14.0f - (float)s->diff_count * 14.0f;
    for (int i = 0; i < s->diff_count; i++, dx += 14.0f)
        shapes_filled_circle(r, dx + 5.0f, cy, 4.5f, ui_alpha(ui_difficulty_color(s->diffs[i].name), dim ? 140 : 255));
    (void)l;
}

static void draw_pill(SDL_Renderer *r, const Font *f, float right, float cy, const char *s, SDL_Color c, float *left) {
    const float w = (float)font_width_tracked(f, FONT_LABEL, s, 1.5f) + 28.0f, h = 28.0f, x = right - w;
    ui_round_rect_rot(r, (SDL_FRect){x - 2.0f, cy - h * 0.5f - 2.0f, w + 4.0f, h + 4.0f}, h * 0.5f + 2.0f, FLAT, ui_alpha(c, 150));
    ui_round_rect_rot(r, (SDL_FRect){x, cy - h * 0.5f, w, h}, h * 0.5f, FLAT, CH_INK);
    float px = x + 14.0f;
    for (const char *p = s; *p; p++) {   /* texte espacé, lettre à lettre (ASCII) */
        char one[2] = { *p, '\0' };
        font_draw(f, r, FONT_LABEL, px, cy - (float)font_cap_height(f, FONT_LABEL) * 0.5f, ALIGN_LEFT, CH_WHITE, one);
        px += (float)font_width(f, FONT_LABEL, one) + 1.5f;
    }
    *left = x;
}

static const char *FILTER_NAMES[LIBF_COUNT] = { "ALL SONGS", "NOT INSTALLED", "INSTALLED", "UPDATES" };
static const char *SORT_NAMES[LIBS_COUNT] = { "NEWEST", "TITLE", "ARTIST" };

static void draw_panel(const Library *l, SDL_Renderer *r, const Font *f, const Sprites *sprites, const LibSong *s) {
    ui_round_rect_rot(r, (SDL_FRect){PANEL_X + 6.0f, PANEL_Y + 6.0f, PANEL_W, PANEL_H}, 14.0f, FLAT, CH_PINK);
    ui_round_rect_rot(r, (SDL_FRect){PANEL_X, PANEL_Y, PANEL_W, PANEL_H}, 14.0f, FLAT, CH_INK);
    const float x = PANEL_X + 18.0f, w = PANEL_W - 36.0f;
    char line[160];

    /* vignette (la place reste réservée tant qu'elle n'est pas là) */
    const SDL_FRect img = { PANEL_X + 14.0f, PANEL_Y + 14.0f, (float)THUMB_W, (float)THUMB_H };
    ui_panel(r, (SDL_FRect){img.x - 2.0f, img.y - 2.0f, img.w + 4.0f, img.h + 4.0f}, 5.0f, ui_alpha(CH_WHITE, 64), UI_NONE, 0.0f);
    const Background *th = NULL;
    if (s->thumb_state == LIBTH_READY) {
        char path[BGCACHE_PATH];
        thumb_file(s, path, sizeof path);
        th = bgcache_peek(&l->thumbs, path);
    }
    if (th && th->tex) {
        SDL_SetTextureAlphaMod(th->tex, 255);
        SDL_RenderCopyF(r, th->tex, NULL, &img);
    } else {
        ui_skew_box(r, img, 0.0f, ui_alpha(CH_VIOLET, 90));
    }

    /* état dans la bibliothèque du joueur, posé en bas à gauche de l'image */
    const float ph = 26.0f;
    const char *label;
    SDL_Color pc, tc;
    if (s->state == LIB_INSTALLED) { label = "IN YOUR LIBRARY"; pc = CH_CYAN; tc = CH_INK; }
    else if (s->state == LIB_UPDATE) {
        if (s->installed_version > 0) snprintf(line, sizeof line, "UPDATE  V%d TO V%d", s->installed_version, s->version);
        else snprintf(line, sizeof line, "UPDATE  V%d", s->version);
        label = line; pc = CH_GOLD; tc = CH_INK;
    } else { label = "NOT IN YOUR LIBRARY"; pc = CH_INK; tc = ui_alpha(CH_WHITE, 220); }
    const float lw = (float)font_width(f, FONT_LABEL, label);
    const float lead = s->state == LIB_NEW ? 14.0f : 32.0f;   /* la pastille d'état précède le texte, sauf pour une chanson absente */
    const float py = img.y + img.h - ph - 8.0f;
    ui_round_rect_rot(r, (SDL_FRect){img.x + 8.0f, py, lw + lead + 16.0f, ph}, ph * 0.5f, FLAT, ui_alpha(CH_INK, 215));
    if (s->state != LIB_NEW) ui_round_rect_rot(r, (SDL_FRect){img.x + 10.0f, py + 2.0f, lw + lead + 12.0f, ph - 4.0f}, ph * 0.5f - 2.0f, FLAT, pc);
    draw_glyph(r, img.x + 23.0f, py + ph * 0.5f, 8.0f, s->state, CH_INK);
    text(f, r, FONT_LABEL, img.x + 8.0f + lead, py + ph * 0.5f, ALIGN_LEFT, 1.0f, tc, label);

    float y = img.y + img.h + 26.0f;
    char title[160];
    ui_upper(s->title, title, sizeof title);
    float tsc = 0.95f;   /* le titre rétrécit pour tenir (jusqu'à 0,7), puis il est coupé */
    const float tw = (float)font_width(f, FONT_CARD_NAME, title) * tsc;
    if (tw > w) tsc = fmaxf(0.7f, tsc * w / tw);
    text_fit(f, r, FONT_CARD_NAME, x, y, ALIGN_LEFT, tsc, CH_WHITE, title, w);
    y += 26.0f;
    text_fit(f, r, FONT_CARD, x, y, ALIGN_LEFT, 1.0f, CH_CYAN, s->artist[0] ? s->artist : "UNKNOWN ARTIST", w);
    y += 23.0f;

    const SDL_Color mc = CH_MUTED;
    if (s->pack[0]) { snprintf(line, sizeof line, "PACK  %s", s->pack); text_fit(f, r, FONT_CARD, x, y, ALIGN_LEFT, 1.0f, mc, line, w); y += 20.0f; }
    if (s->mapper[0]) { snprintf(line, sizeof line, "BY  %s", s->mapper); text_fit(f, r, FONT_CARD, x, y, ALIGN_LEFT, 1.0f, mc, line, w); y += 20.0f; }
    char size[24];
    fmt_size(s->zip_bytes, size, sizeof size);
    snprintf(line, sizeof line, "%d:%02d  \xC2\xB7  %s  \xC2\xB7  V%d", s->duration_s / 60, s->duration_s % 60, size, s->version);
    text(f, r, FONT_CARD, x, y, ALIGN_LEFT, 1.0f, mc, line);
    y += 27.0f;

    for (int i = 0; i < s->diff_count; i++, y += 19.0f) {
        const SDL_Color dc = ui_difficulty_color(s->diffs[i].name);
        shapes_filled_circle(r, x + 5.0f, y, 5.0f, dc);
        char up[32];
        ui_upper(s->diffs[i].name, up, sizeof up);
        text(f, r, FONT_CARD_BOLD, x + 20.0f, y, ALIGN_LEFT, 1.0f, dc, up);
        snprintf(line, sizeof line, "%d NOTES", s->diffs[i].notes);
        text(f, r, FONT_CARD, x + w, y, ALIGN_RIGHT, 1.0f, mc, line);
    }

    /* action : installer, avancement, ou raison de l'échec (pour cette chanson seulement) */
    const float ay = PANEL_Y + PANEL_H - 42.0f;
    const LibJob job = job_state(l);
    const bool mine = job != LIBJ_IDLE && !strcmp(l->job_id, s->id);
    if (mine && (job == LIBJ_DOWNLOAD || job == LIBJ_VERIFY || job == LIBJ_INSTALL)) {
        int done = SDL_AtomicGet((SDL_atomic_t *)&l->prog.done), total = SDL_AtomicGet((SDL_atomic_t *)&l->prog.total);
        if (total <= 0) total = s->zip_bytes;
        float frac = total > 0 ? (float)done / (float)total : 0.0f;
        if (job != LIBJ_DOWNLOAD) frac = 1.0f;
        frac = frac < 0.0f ? 0.0f : frac > 1.0f ? 1.0f : frac;
        ui_round_rect_rot(r, (SDL_FRect){x, ay - 16.0f, w, 10.0f}, 5.0f, FLAT, ui_alpha(CH_WHITE, 36));
        if (frac > 0.0f) ui_round_rect_rot(r, (SDL_FRect){x, ay - 16.0f, fmaxf(10.0f, w * frac), 10.0f}, 5.0f, FLAT, CH_CYAN);
        snprintf(line, sizeof line, job == LIBJ_DOWNLOAD ? "DOWNLOADING  %d%%" : job == LIBJ_VERIFY ? "CHECKING THE FILE" : "INSTALLING",
                 (int)(frac * 100.0f));
        text(f, r, FONT_LABEL, x, ay + 8.0f, ALIGN_LEFT, 1.0f, CH_WHITE, line);
        if (job == LIBJ_DOWNLOAD) {
            const Button cancel[] = { BTN_CIRCLE };
            ui_button_hint_pop(r, f, sprites, x + w - ui_button_hint_pop_width(f, cancel, 1, "CANCEL"), ay + 10.0f, cancel, 1, "CANCEL");
        }
    } else if (mine && job == LIBJ_FAILED) {
        char up[96], seg[96];
        ui_upper(l->job_err, up, sizeof up);
        const char *p = up;
        for (int row = 0; row < 2 && *p; row++) {   /* deux lignes au plus, coupées entre deux mots */
            size_t n = strlen(p), cut = n;
            while (cut > 0) {
                snprintf(seg, sizeof seg, "%.*s", (int)cut, p);
                if (font_width(f, FONT_CARD, seg) <= (int)w) break;
                cut--;
                while (cut > 0 && p[cut] != ' ') cut--;
            }
            if (cut == 0) cut = n;
            snprintf(seg, sizeof seg, "%.*s", (int)cut, p);
            text(f, r, FONT_CARD, x, ay - 18.0f + 17.0f * (float)row, ALIGN_LEFT, 1.0f, CH_ROSE, seg);
            p += cut;
            while (*p == ' ') p++;
        }
        ui_button_hint_pop(r, f, sprites, x, ay + 24.0f, (const Button[]){BTN_CROSS}, 1, "TRY AGAIN");
    } else if (s->state != LIB_INSTALLED) {
        ui_button_hint_pop(r, f, sprites, x, ay, (const Button[]){BTN_CROSS}, 1, s->state == LIB_UPDATE ? "UPDATE" : "INSTALL");
    }
}

static void draw_message(SDL_Renderer *r, const Font *f, const char *head, const char *body, float time, bool dots) {
    const float cx = LIST_X + (PANEL_X + PANEL_W - LIST_X) * 0.5f, cy = 290.0f;
    ui_round_rect_rot(r, (SDL_FRect){cx - 330.0f + 6.0f, cy - 70.0f + 6.0f, 660.0f, 140.0f}, 16.0f, FLAT, CH_PINK);
    ui_round_rect_rot(r, (SDL_FRect){cx - 330.0f, cy - 70.0f, 660.0f, 140.0f}, 16.0f, FLAT, CH_INK);
    static const char *const DOTS[4] = { "", " .", " . .", " . . ." };
    char h[64];
    snprintf(h, sizeof h, "%s%s", head, dots ? DOTS[(int)(time * 3.0f) % 4] : "");
    text(f, r, FONT_CARD_NAME, cx, cy - 20.0f, ALIGN_CENTER, 1.0f, CH_GOLD, h);
    text(f, r, FONT_CARD, cx, cy + 24.0f, ALIGN_CENTER, 1.0f, CH_MUTED, body);
}

void library_draw(const Library *l, SDL_Renderer *r, const Font *f, const Sprites *sprites) {
    const LibStatus st = library_status(l);
    const float hy = SCREEN_H - 26.0f;
    float hx = 34.0f;

    if (st != LIBST_READY || !l->order) {
        if (st == LIBST_OFFLINE) draw_message(r, f, "CONNECTION REQUIRED", "THE LIBRARY NEEDS THE INTERNET. CONNECT THE CONSOLE TO WI-FI, THEN TRY AGAIN.", l->time, false);
        else if (st == LIBST_FAILED) draw_message(r, f, "LIBRARY UNAVAILABLE", "THE CATALOGUE COULD NOT BE READ. TRY AGAIN IN A MOMENT.", l->time, false);
        else draw_message(r, f, "CONTACTING THE LIBRARY", "ONE MOMENT", l->time, true);
        if (st == LIBST_OFFLINE || st == LIBST_FAILED)
            hx = ui_button_hint_pop(r, f, sprites, hx, hy, (const Button[]){BTN_CROSS}, 1, "RETRY") + 18.0f;
        ui_button_hint_pop(r, f, sprites, hx, hy, (const Button[]){BTN_CIRCLE}, 1, "BACK");
        return;
    }

    /* filtre, tri et compteur, alignés à droite sous le haut de l'écran */
    float left = 920.0f;
    char cnt[32];
    snprintf(cnt, sizeof cnt, "%d / %d", l->shown ? l->sel + 1 : 0, l->shown);
    draw_pill(r, f, left, 78.0f, cnt, CH_GOLD, &left);
    char buf[48];
    snprintf(buf, sizeof buf, "SORT  %s", SORT_NAMES[l->sort]);
    draw_pill(r, f, left - 8.0f, 78.0f, buf, CH_CYAN, &left);
    snprintf(buf, sizeof buf, "%s", FILTER_NAMES[l->filter]);
    draw_pill(r, f, left - 8.0f, 78.0f, buf, l->filter == LIBF_ALL ? CH_CYAN : CH_PINK, &left);

    if (l->shown == 0) {
        draw_message(r, f, "NOTHING HERE", l->filter == LIBF_UPDATE ? "ALL YOUR SONGS ARE UP TO DATE." : "NO SONG MATCHES THIS FILTER.", l->time, false);
    } else {
        const SDL_Rect clip = { 0, (int)LIST_TOP, SCREEN_W, (int)(ROWS * ROW_H) };
        SDL_RenderSetClipRect(r, &clip);
        int first = (int)floorf(l->scroll);
        for (int i = first; i <= first + ROWS && i < l->shown; i++) {
            if (i < 0) continue;
            draw_row(l, r, f, &l->songs[l->order[i]], LIST_TOP + ((float)i - l->scroll) * ROW_H, i == l->sel);
        }
        SDL_RenderSetClipRect(r, NULL);

        if (l->shown > ROWS) {   /* barre de défilement */
            const float tx = LIST_X + LIST_W + 10.0f, th = ROWS * ROW_H;
            ui_round_rect_rot(r, (SDL_FRect){tx, LIST_TOP, 6.0f, th}, 3.0f, FLAT, ui_alpha(CH_INK, 170));
            const float frac = (float)ROWS / (float)l->shown, bh = th * frac < 24.0f ? 24.0f : th * frac;
            const float pos = l->scroll / (float)(l->shown - ROWS);
            ui_round_rect_rot(r, (SDL_FRect){tx, LIST_TOP + (th - bh) * (pos < 0 ? 0 : pos > 1 ? 1 : pos), 6.0f, bh}, 3.0f, FLAT, CH_PINK);
        }
        draw_panel(l, r, f, sprites, &l->songs[l->order[l->sel]]);
    }

    hx = ui_button_hint_pop(r, f, sprites, hx, hy, (const Button[]){BTN_UP, BTN_DOWN}, 2, "CHOOSE") + 18.0f;
    hx = ui_button_hint_pop(r, f, sprites, hx, hy, (const Button[]){BTN_L, BTN_R}, 2, "PAGE") + 18.0f;
    hx = ui_button_hint_pop(r, f, sprites, hx, hy, (const Button[]){BTN_SQUARE}, 1, "FILTER") + 18.0f;
    hx = ui_button_hint_pop(r, f, sprites, hx, hy, (const Button[]){BTN_TRIANGLE}, 1, "SORT") + 18.0f;
    if (!library_busy(l)) ui_button_hint_pop(r, f, sprites, hx, hy, (const Button[]){BTN_CIRCLE}, 1, "BACK");

    /* légende des pastilles, à droite de la barre d'aide */
    const float lx = PANEL_X - 24.0f;
    draw_badge(r, lx, hy, 8.0f, LIB_INSTALLED, 255);
    text(f, r, FONT_LABEL, lx + 14.0f, hy, ALIGN_LEFT, 1.0f, ui_alpha(CH_WHITE, 170), "IN LIBRARY");
    draw_badge(r, lx + 134.0f, hy, 8.0f, LIB_UPDATE, 255);
    text(f, r, FONT_LABEL, lx + 148.0f, hy, ALIGN_LEFT, 1.0f, ui_alpha(CH_WHITE, 170), "UPDATE");
}
