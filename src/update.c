#include "update.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <curl/curl.h>
#endif

int update_version_compare(const char *a, const char *b) {
    for (int i = 0; i < 3; i++) {
        long x = 0, y = 0;
        if (*a) { x = strtol(a, (char **)&a, 10); if (*a == '.') a++; }
        if (*b) { y = strtol(b, (char **)&b, 10); if (*b == '.') b++; }
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

/* Valeur texte d'une clé de premier niveau de la fiche : "clé": "valeur". Gère \" \\ \n ; le reste est copié tel quel. */
static bool json_string(const char *json, const char *key, char *out, size_t cap) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return false;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\' && p[1]) {
            p++;
            out[n++] = *p == 'n' ? ' ' : *p;
            p++;
        } else out[n++] = *p++;
    }
    out[n] = '\0';
    if (!*p) return false;   /* fiche tronquée : guillemet fermant absent */
    return n > 0;
}

bool update_parse(const char *json, const char *current, Update *out) {
    char version[24];
    if (!json_string(json, "version", version, sizeof version)) return false;
    for (const char *c = version; *c; c++)
        if ((*c < '0' || *c > '9') && *c != '.') return false;   /* « 0.9.1 » seulement : jamais de texte libre dans le menu */
    if (update_version_compare(version, current) <= 0) return false;
    snprintf(out->version, sizeof out->version, "%s", version);
    out->notes[0] = out->url[0] = '\0';
    json_string(json, "notes_en", out->notes, sizeof out->notes);
    json_string(json, "url", out->url, sizeof out->url);
    return true;
}

Update *update_create(void) {
    Update *u = calloc(1, sizeof *u);
    if (u) SDL_AtomicSet(&u->state, UPDATE_IDLE);
    return u;
}

UpdateState update_state(const Update *u) {
    return u ? (UpdateState)SDL_AtomicGet((SDL_atomic_t *)&u->state) : UPDATE_IDLE;
}

static void finish(Update *u, const char *json, const char *current) {
    SDL_AtomicSet(&u->state, !json ? UPDATE_FAILED : update_parse(json, current, u) ? UPDATE_AVAILABLE : UPDATE_CURRENT);
}

#ifdef __vita__
#include <stdarg.h>
#include "files.h"
/* Journal de la vérification (SAVE_DIR update_log.txt, réécrit à chaque démarrage) : ce que le réseau a répondu, pour
   comprendre une annonce qui n'apparaît pas. Aucune donnée personnelle. */
static void ulog(const char *fmt, ...) {
    static bool first = true;
    files_mkdir(SAVE_DIR);
    FILE *f = fopen(SAVE_DIR "update_log.txt", first ? "w" : "a");
    first = false;
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

#define NET_POOL   (1 * 1024 * 1024)
#define BODY_MAX   4096
#define CA_BUNDLE  ASSET_PATH("ui/cacert.pem")   /* Mozilla, curl.se/ca : la Vita ne connaît pas les autorités actuelles */

typedef struct { char *buf; int cap, len; } Sink;

static size_t sink_write(void *data, size_t size, size_t n, void *user) {
    Sink *k = user;
    size_t bytes = size * n, room = (size_t)(k->cap - 1 - k->len);
    if (bytes > room) bytes = room;   /* fiche trop longue : on garde le début, la lecture JSON échouera proprement */
    memcpy(k->buf + k->len, data, bytes);
    k->len += (int)bytes;
    return size * n;
}

/* Une requête GET en HTTPS (libcurl + OpenSSL : la couche HTTPS de la Vita ne sait pas négocier le TLS actuel) ; renvoie
   la taille lue (0 = échec). verify = false : certificat non contrôlé (horloge de la console fausse, par exemple). Servi
   seulement pour lire un numéro de version : rien de ce qui arrive n'est exécuté, ni écrit sur la console. */
static int http_get(const char *url, char *buf, int cap, bool verify) {
    Sink k = { buf, cap, 0 };
    buf[0] = '\0';
    ulog("GET %s (verify=%d)", url, verify);
    CURL *c = curl_easy_init();
    if (!c) { ulog("  curl_easy_init failed"); return 0; }
    char err[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, GAME_NAME "/" GAME_VERSION);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    ulog("  curl: %d (%s) http %ld, %d bytes", (int)rc, err[0] ? err : curl_easy_strerror(rc), status, k.len);
    curl_easy_cleanup(c);
    buf[k.len] = '\0';
    return rc == CURLE_OK ? k.len : 0;
}

static bool net_up(void) {
    static bool done, ok;
    if (done) return ok;
    done = true;
    int m = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    ulog("sysmodule net=%08x", m);
    if (m < 0) return false;
    static char pool[NET_POOL] __attribute__((aligned(16)));
    SceNetInitParam p = { pool, sizeof pool, 0 };
    int r = sceNetInit(&p);
    ulog("netinit=%08x", r);
    if (r < 0 && r != (int)0x80410202) return false;   /* déjà initialisé : pas une erreur */
    int c = sceNetCtlInit();
    CURLcode g = curl_global_init(CURL_GLOBAL_DEFAULT);
    ulog("netctl=%08x curl_global_init=%d", c, (int)g);
    return ok = g == CURLE_OK;
}

static int check_thread(void *data) {
    Update *u = data;
    int state = 0;
    char *body = malloc(BODY_MAX);
    bool got = false;
    ulog("update check, game %s", GAME_VERSION);
    if (body && net_up()) {
        int cs = sceNetCtlInetGetState(&state);
        ulog("inet state: ret=%08x state=%d (3 = connected)", cs, state);
        if (cs >= 0 && state == SCE_NETCTL_STATE_CONNECTED)
            got = http_get(UPDATE_URL, body, BODY_MAX, true) > 0 || http_get(UPDATE_URL, body, BODY_MAX, false) > 0;
    }
    finish(u, got ? body : NULL, GAME_VERSION);
    ulog("result: %s", got ? (update_state(u) == UPDATE_AVAILABLE ? "newer version found" : "up to date") : "failed");
    free(body);
    return 0;
}

void update_start(Update *u, const char *current_version) {
    (void)current_version;
    if (!u) return;
    SDL_AtomicSet(&u->state, UPDATE_CHECKING);
    SDL_Thread *t = SDL_CreateThread(check_thread, "update", u);
    if (t) SDL_DetachThread(t);
    else SDL_AtomicSet(&u->state, UPDATE_FAILED);
}
#else
void update_start(Update *u, const char *current_version) {
    const char *file = getenv("VITAGAME_UPDATE_JSON");   /* essai de l'affichage sur desktop */
    if (!u || !file) return;
    char body[4096] = "";
    FILE *f = fopen(file, "rb");
    if (f) { size_t n = fread(body, 1, sizeof body - 1, f); body[n] = '\0'; fclose(f); }
    finish(u, f ? body : NULL, current_version);
}
#endif
