#include "update.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>
#include <psp2/libssl.h>
#include <psp2/sysmodule.h>
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
#define NET_POOL   (1 * 1024 * 1024)
#define HTTP_POOL  (256 * 1024)
#define SSL_POOL   (300 * 1024)
#define BODY_MAX   4096
#define TIMEOUT_US (6 * 1000 * 1000)

/* Une requête GET ; renvoie la taille lue (0 = échec). cert_ok = false : la vérification du certificat est coupée (le
   magasin de certificats de la Vita, de 2013, ne connaît pas toutes les autorités actuelles). Servi seulement pour lire
   un numéro de version : rien de ce qui arrive n'est exécuté, ni écrit sur la console. */
static int http_get(const char *url, char *buf, int cap, bool verify) {
    int len = 0, tmpl = -1, conn = -1, req = -1;
    if (!verify) sceHttpsDisableOption(SCE_HTTPS_FLAG_SERVER_VERIFY | SCE_HTTPS_FLAG_CN_CHECK | SCE_HTTPS_FLAG_NOT_BEFORE_CHECK |
                                       SCE_HTTPS_FLAG_NOT_AFTER_CHECK | SCE_HTTPS_FLAG_KNOWN_CA_CHECK);
    else sceHttpsEnableOption(SCE_HTTPS_FLAG_SERVER_VERIFY | SCE_HTTPS_FLAG_CN_CHECK | SCE_HTTPS_FLAG_NOT_BEFORE_CHECK |
                              SCE_HTTPS_FLAG_NOT_AFTER_CHECK | SCE_HTTPS_FLAG_KNOWN_CA_CHECK);
    tmpl = sceHttpCreateTemplate(GAME_NAME "/" GAME_VERSION, SCE_HTTP_VERSION_1_1, 1);
    if (tmpl < 0) goto done;
    sceHttpSetResolveTimeOut(tmpl, TIMEOUT_US);
    sceHttpSetConnectTimeOut(tmpl, TIMEOUT_US);
    sceHttpSetSendTimeOut(tmpl, TIMEOUT_US);
    sceHttpSetRecvTimeOut(tmpl, TIMEOUT_US);
    conn = sceHttpCreateConnectionWithURL(tmpl, url, 0);
    if (conn < 0) goto done;
    req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
    if (req < 0) goto done;
    if (sceHttpSendRequest(req, NULL, 0) < 0) goto done;
    int status = 0;
    if (sceHttpGetStatusCode(req, &status) < 0 || status != 200) goto done;
    for (;;) {
        int n = sceHttpReadData(req, buf + len, (unsigned)(cap - 1 - len));
        if (n <= 0) break;
        len += n;
        if (len >= cap - 1) break;
    }
done:
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
    buf[len] = '\0';
    return len;
}

static bool net_up(void) {
    static bool done, ok;
    if (done) return ok;
    done = true;
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0 || sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS) < 0) return false;
    static char pool[NET_POOL] __attribute__((aligned(16)));
    SceNetInitParam p = { pool, sizeof pool, 0 };
    int r = sceNetInit(&p);
    if (r < 0 && r != (int)0x80410202) return false;   /* déjà initialisé : pas une erreur */
    sceNetCtlInit();
    if (sceHttpInit(HTTP_POOL) < 0 || sceSslInit(SSL_POOL) < 0) return false;
    return ok = true;
}

static int check_thread(void *data) {
    Update *u = data;
    int state = 0;
    char *body = malloc(BODY_MAX);
    bool got = false;
    if (body && net_up() && sceNetCtlInetGetState(&state) >= 0 && state == SCE_NETCTL_STATE_CONNECTED) {
        got = http_get(UPDATE_URL, body, BODY_MAX, true) > 0 || http_get(UPDATE_URL, body, BODY_MAX, false) > 0;
    }
    finish(u, got ? body : NULL, GAME_VERSION);
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
