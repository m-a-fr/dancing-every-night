#include "net.h"
#include "config.h"
#include "files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Trois moteurs, un seul choisi à la compilation :
     libcurl   Vita (liée, avec OpenSSL) et macOS (la libcurl du système, liée) ;
     libcurl chargée à l'exécution (NET_DLOPEN)   Linux : le jeu démarre même sans libcurl, la bibliothèque le dit ;
     WinHTTP   Windows (API du système : certificats et proxy de Windows, aucune bibliothèque à fournir).
   Sans aucun des trois, net_supported() est faux. */
#if defined(__vita__) || defined(HAVE_CURL)
#  define NET_CURL 1
#  ifdef NET_DLOPEN
#    define CURL_DISABLE_TYPECHECK 1   /* les appels passent par des pointeurs de fonction : pas de macros de contrôle de types */
#    include <dlfcn.h>
#  endif
#  include <curl/curl.h>
#elif defined(HAVE_WINHTTP)
#  define NET_WINHTTP 1
#  include <windows.h>
#  include <winhttp.h>
#endif

#ifdef __vita__
#include <stdarg.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#define NET_POOL  (1 * 1024 * 1024)
#define CA_BUNDLE ASSET_PATH("ui/cacert.pem")   /* Mozilla, curl.se/ca : la Vita ne connaît pas les autorités actuelles */

/* Journal du réseau de la bibliothèque (SAVE_DIR net_log.txt, réécrit à chaque lancement) : ce que la console a répondu,
   pour comprendre un écran « connexion requise » ou un téléchargement qui échoue. Aucune donnée personnelle. */
void net_log(const char *fmt, ...) {
    static bool first = true;
    files_mkdir(SAVE_DIR);
    FILE *f = fopen(SAVE_DIR "net_log.txt", first ? "w" : "a");
    first = false;
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
#  define NLOG(...) net_log(__VA_ARGS__)

/* Initialise la couche réseau une fois (update.c l'a peut-être déjà fait au lancement) */
static bool net_up(void) {
    static bool done, ok;
    if (done) return ok;
    done = true;
    int m = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    net_log("sysmodule net=%08x", m);
    if (m < 0) return false;
    static char pool[NET_POOL] __attribute__((aligned(16)));
    SceNetInitParam p = { pool, sizeof pool, 0 };
    int r = sceNetInit(&p);
    net_log("netinit=%08x", r);
    /* update.c l'a déjà démarrée au lancement : « déjà initialisé » (0x80410202) ou « occupé » (0x80410110), pas une panne */
    if (r < 0 && r != (int)0x80410202 && r != (int)0x80410110) return false;
    int cc = sceNetCtlInit();
    CURLcode g = curl_global_init(CURL_GLOBAL_DEFAULT);
    net_log("netctl=%08x curl_global_init=%d", cc, (int)g);
    return ok = g == CURLE_OK;
}

bool net_supported(void) { return true; }

bool net_online(void) {
    int state = 0;
    bool up = net_up();
    int cs = up ? sceNetCtlInetGetState(&state) : -1;
    static int last = -2;
    if (last != (up ? state : -1)) { last = up ? state : -1; net_log("online? up=%d inet ret=%08x state=%d (3 = connected)", up, cs, state); }
    return up && cs >= 0 && state == SCE_NETCTL_STATE_CONNECTED;
}
#else
void net_log(const char *fmt, ...) { (void)fmt; }
#  define NLOG(...) ((void)0)
#endif

/* ---------------------------------------------------------------- moteur libcurl */

#ifdef NET_CURL

#ifdef NET_DLOPEN
typedef struct {
    CURL *(*easy_init)(void);
    void (*easy_cleanup)(CURL *);
    CURLcode (*easy_setopt)(CURL *, CURLoption, ...);
    CURLcode (*easy_perform)(CURL *);
    CURLcode (*easy_getinfo)(CURL *, CURLINFO, ...);
    const char *(*easy_strerror)(CURLcode);
    CURLcode (*global_init)(long);
} CurlApi;
static CurlApi cu;

static bool curl_load(void) {
    static bool tried, ok;
    if (tried) return ok;
    tried = true;
    void *h = dlopen("libcurl.so.4", RTLD_NOW);
    if (!h) h = dlopen("libcurl.so", RTLD_NOW);
#ifdef __APPLE__
    if (!h) h = dlopen("libcurl.4.dylib", RTLD_NOW);   /* pas utilisé en distribution (macOS lie libcurl), sert à essayer ce moteur */
#endif
    if (!h) return false;
    cu.easy_init = (CURL *(*)(void))dlsym(h, "curl_easy_init");
    cu.easy_cleanup = (void (*)(CURL *))dlsym(h, "curl_easy_cleanup");
    cu.easy_setopt = (CURLcode (*)(CURL *, CURLoption, ...))dlsym(h, "curl_easy_setopt");
    cu.easy_perform = (CURLcode (*)(CURL *))dlsym(h, "curl_easy_perform");
    cu.easy_getinfo = (CURLcode (*)(CURL *, CURLINFO, ...))dlsym(h, "curl_easy_getinfo");
    cu.easy_strerror = (const char *(*)(CURLcode))dlsym(h, "curl_easy_strerror");
    cu.global_init = (CURLcode (*)(long))dlsym(h, "curl_global_init");
    if (!cu.easy_init || !cu.easy_cleanup || !cu.easy_setopt || !cu.easy_perform || !cu.easy_getinfo || !cu.easy_strerror ||
        !cu.global_init)
        return false;
    return ok = cu.global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}
#  undef curl_easy_setopt   /* certaines versions de curl.h les définissent en macros même sans contrôle de types */
#  undef curl_easy_getinfo
#  define curl_easy_init cu.easy_init
#  define curl_easy_cleanup cu.easy_cleanup
#  define curl_easy_setopt cu.easy_setopt
#  define curl_easy_perform cu.easy_perform
#  define curl_easy_getinfo cu.easy_getinfo
#  define curl_easy_strerror cu.easy_strerror
bool net_supported(void) { return curl_load(); }
bool net_online(void) { return curl_load(); }
#elif !defined(__vita__)
bool net_supported(void) { return true; }
bool net_online(void) { return true; }
#endif

typedef struct { NetWriteFn wr; void *user; bool aborted; NetProgress *p; } CurlCtx;

static size_t curl_write(void *data, size_t size, size_t n, void *user) {
    CurlCtx *c = user;
    if (!c->wr(data, size * n, c->user)) { c->aborted = true; return 0; }   /* 0 : curl abandonne */
    return size * n;
}

static int curl_progress(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    CurlCtx *c = user;
    SDL_AtomicSet(&c->p->done, (int)dlnow);
    SDL_AtomicSet(&c->p->total, (int)dltotal);
    return SDL_AtomicGet(&c->p->cancel) ? 1 : 0;
}

static bool http_run(const char *url, NetWriteFn wr, void *user, NetProgress *p, bool verify, NetResult *res) {
    CurlCtx ctx = { wr, user, false, p };
    CURL *c = curl_easy_init();
    if (!c) { snprintf(res->why, sizeof res->why, "curl_easy_init failed"); return false; }
    char err[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, GAME_NAME "/" GAME_VERSION);
    /* HTTPS seulement, redirections comprises (les options *_STR n'existent qu'à partir de curl 7.85 : on garde les anciennes) */
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    if (p) {   /* téléchargement : pas de limite de durée, mais une connexion perdue (moins de 1 Ko/s pendant 30 s) s'arrête */
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    } else {
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    }
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
#ifdef __vita__
    curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
#else
    (void)verify;   /* PC : les certificats du système, toujours contrôlés */
#endif
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
    if (p) {
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, curl_progress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ctx);
    }
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    snprintf(res->why, sizeof res->why, "curl %d (%s) http %ld%s", (int)rc, err[0] ? err : curl_easy_strerror(rc), status,
             ctx.aborted ? ", stopped" : "");
    res->status = (int)status;
    curl_easy_cleanup(c);
    return rc == CURLE_OK && !ctx.aborted;
}

/* ---------------------------------------------------------------- moteur WinHTTP */

#elif defined(NET_WINHTTP)

bool net_supported(void) { return true; }
bool net_online(void) { return true; }

static bool http_run(const char *url, NetWriteFn wr, void *user, NetProgress *p, bool verify, NetResult *res) {
    (void)verify;
    wchar_t wurl[1024], host[256], path[1024], extra[512], full[1600];
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024)) { snprintf(res->why, sizeof res->why, "bad address"); return false; }
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host;     uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;      uc.dwUrlPathLength = 1024;
    uc.lpszExtraInfo = extra;   uc.dwExtraInfoLength = 512;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        snprintf(res->why, sizeof res->why, "not an https address");
        return false;
    }
    wcscpy(full, path);
    wcscat(full, extra);

    bool ok = false;
    HINTERNET session = WinHttpOpen(L"DancingEveryNight", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET conn = session ? WinHttpConnect(session, host, uc.nPort, 0) : NULL;
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE) : NULL;
    snprintf(res->why, sizeof res->why, "winhttp error %lu", (unsigned long)GetLastError());
    if (req) {
        WinHttpSetTimeouts(session, 10000, 10000, 30000, p ? 60000 : 30000);   /* résolution, connexion, envoi, réception */
        DWORD code = 0, size = sizeof code;
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, NULL) &&
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code,
                                &size, WINHTTP_NO_HEADER_INDEX)) {
            res->status = (int)code;
            if (code != 200) {
                snprintf(res->why, sizeof res->why, "http %lu", (unsigned long)code);
            } else {
                DWORD length = 0;
                size = sizeof length;
                if (p && WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                             WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX))
                    SDL_AtomicSet(&p->total, (int)length);
                char buf[16384];
                long long got = 0;
                ok = true;
                for (;;) {
                    DWORD n = 0;
                    if (p && SDL_AtomicGet(&p->cancel)) { ok = false; snprintf(res->why, sizeof res->why, "cancelled"); break; }
                    if (!WinHttpReadData(req, buf, sizeof buf, &n)) {
                        ok = false;
                        snprintf(res->why, sizeof res->why, "read error %lu", (unsigned long)GetLastError());
                        break;
                    }
                    if (n == 0) break;   /* fin de la réponse */
                    if (!wr(buf, n, user)) { ok = false; snprintf(res->why, sizeof res->why, "stopped"); break; }
                    got += n;
                    if (p) SDL_AtomicSet(&p->done, (int)got);
                }
                if (ok) snprintf(res->why, sizeof res->why, "http 200");
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

/* ---------------------------------------------------------------- sans réseau */

#else

bool net_supported(void) { return false; }
bool net_online(void) { return false; }
static bool http_run(const char *url, NetWriteFn wr, void *user, NetProgress *p, bool verify, NetResult *res) {
    (void)url; (void)wr; (void)user; (void)p; (void)verify;
    snprintf(res->why, sizeof res->why, "no network engine in this build");
    return false;
}

#endif

/* ---------------------------------------------------------------- GET en mémoire et téléchargement dans un fichier */

typedef struct { char *buf; int len, cap, max; bool overflow; } MemSink;

static bool mem_write(const void *data, size_t n, void *user) {
    MemSink *k = user;
    if ((long long)k->len + (long long)n + 1 > k->max) { k->overflow = true; return false; }
    if (k->len + (int)n + 1 > k->cap) {
        int cap = k->cap ? k->cap * 2 : 64 * 1024;
        while (cap < k->len + (int)n + 1) cap *= 2;
        char *grown = realloc(k->buf, (size_t)cap);
        if (!grown) { k->overflow = true; return false; }
        k->buf = grown;
        k->cap = cap;
    }
    memcpy(k->buf + k->len, data, n);
    k->len += (int)n;
    return true;
}

char *net_get(const char *url, int max_bytes, int *len) {
    if (!net_online()) return NULL;
    for (int attempt = 0; attempt < 2; attempt++) {
        const bool verify = attempt == 0;
        MemSink k = { NULL, 0, 0, max_bytes, false };
        NetResult res = { 0, "" };
        bool ok = http_run(url, mem_write, &k, NULL, verify, &res);
        NLOG("GET verify=%d: %s, %d bytes%s", verify, res.why, k.len, k.overflow ? ", too big" : "");
        if (ok && k.buf) {
            k.buf[k.len] = '\0';
            if (len) *len = k.len;
            return k.buf;
        }
        free(k.buf);
#ifndef __vita__
        break;   /* seule la Vita retente sans contrôler le certificat (horloge fausse, autorités inconnues) */
#endif
    }
    return NULL;
}

typedef struct { FILE *f; long long len, max; bool overflow; } FileSink;

static bool file_write(const void *data, size_t n, void *user) {
    FileSink *k = user;
    if (k->len + (long long)n > k->max) { k->overflow = true; return false; }
    if (fwrite(data, 1, n, k->f) != n) return false;   /* carte pleine */
    k->len += (long long)n;
    return true;
}

bool net_download(const char *url, const char *path, int max_bytes, NetProgress *p) {
    if (!net_online()) return false;
    SDL_AtomicSet(&p->done, 0);
    SDL_AtomicSet(&p->total, 0);
    for (int attempt = 0; attempt < 2; attempt++) {
        const bool verify = attempt == 0;
        FileSink k = { fopen(path, "wb"), 0, max_bytes, false };
        if (!k.f) return false;
        NetResult res = { 0, "" };
        bool ok = http_run(url, file_write, &k, p, verify, &res);
        ok = fclose(k.f) == 0 && ok && !k.overflow;
        NLOG("download verify=%d: %s, %ld bytes%s", verify, res.why, (long)k.len, k.overflow ? ", too big" : "");
        if (ok) return true;
        files_remove(path);
#ifdef __vita__
        if (SDL_AtomicGet(&p->cancel)) break;
#else
        break;
#endif
    }
    return false;
}
