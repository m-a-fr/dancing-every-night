#include "net.h"
#include "config.h"
#include "files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__vita__) || defined(HAVE_CURL)
#  define NET_CURL 1
#  include <curl/curl.h>
#endif

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#define NET_POOL  (1 * 1024 * 1024)
#define CA_BUNDLE ASSET_PATH("ui/cacert.pem")   /* Mozilla, curl.se/ca : la Vita ne connaît pas les autorités actuelles */

/* Initialise la couche réseau une fois (update.c peut l'avoir fait avant : « déjà initialisé » n'est pas une erreur) */
static bool net_up(void) {
    static bool done, ok;
    if (done) return ok;
    done = true;
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0) return false;
    static char pool[NET_POOL] __attribute__((aligned(16)));
    SceNetInitParam p = { pool, sizeof pool, 0 };
    int r = sceNetInit(&p);
    if (r < 0 && r != (int)0x80410202) return false;
    sceNetCtlInit();
    return ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

bool net_online(void) {
    int state = 0;
    return net_up() && sceNetCtlInetGetState(&state) >= 0 && state == SCE_NETCTL_STATE_CONNECTED;
}
#else
bool net_online(void) {
#ifdef NET_CURL
    return true;
#else
    return false;
#endif
}
#endif

#ifdef NET_CURL
typedef struct { char *buf; int len, cap, max; bool overflow; } Sink;

static size_t sink_write(void *data, size_t size, size_t n, void *user) {
    Sink *k = user;
    size_t bytes = size * n;
    if (k->len + (long long)bytes + 1 > k->max) { k->overflow = true; return 0; }   /* 0 : curl abandonne */
    if (k->len + (int)bytes + 1 > k->cap) {
        int cap = k->cap ? k->cap * 2 : 64 * 1024;
        while (cap < k->len + (int)bytes + 1) cap *= 2;
        char *grown = realloc(k->buf, (size_t)cap);
        if (!grown) { k->overflow = true; return 0; }
        k->buf = grown;
        k->cap = cap;
    }
    memcpy(k->buf + k->len, data, bytes);
    k->len += (int)bytes;
    return bytes;
}

static char *get_once(const char *url, int max_bytes, int *len, bool verify) {
    Sink k = { NULL, 0, 0, max_bytes, false };
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, GAME_NAME "/" GAME_VERSION);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
#ifdef __vita__
    curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
#else
    (void)verify;
#endif
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || k.overflow || !k.buf) { free(k.buf); return NULL; }
    k.buf[k.len] = '\0';
    if (len) *len = k.len;
    return k.buf;
}

char *net_get(const char *url, int max_bytes, int *len) {
    if (!net_online()) return NULL;
    char *body = get_once(url, max_bytes, len, true);
#ifdef __vita__
    if (!body) body = get_once(url, max_bytes, len, false);
#endif
    return body;
}
typedef struct { FILE *f; long long len, max; NetProgress *p; bool overflow; } FileSink;

static size_t file_write(void *data, size_t size, size_t n, void *user) {
    FileSink *k = user;
    size_t bytes = size * n;
    if (k->len + (long long)bytes > k->max) { k->overflow = true; return 0; }
    if (fwrite(data, 1, bytes, k->f) != bytes) return 0;   /* carte pleine : curl abandonne */
    k->len += (long long)bytes;
    return bytes;
}

static int file_progress(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    NetProgress *p = user;
    SDL_AtomicSet(&p->done, (int)dlnow);
    SDL_AtomicSet(&p->total, (int)dltotal);
    return SDL_AtomicGet(&p->cancel) ? 1 : 0;
}

static bool download_once(const char *url, const char *path, int max_bytes, NetProgress *p, bool verify) {
    FileSink k = { fopen(path, "wb"), 0, max_bytes, p, false };
    if (!k.f) return false;
    CURL *c = curl_easy_init();
    if (!c) { fclose(k.f); return false; }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, GAME_NAME "/" GAME_VERSION);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);   /* moins de 1 Ko/s pendant 30 s : la connexion est perdue */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
#ifdef __vita__
    curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
#else
    (void)verify;
#endif
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, file_progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, p);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    bool ok = fclose(k.f) == 0 && rc == CURLE_OK && !k.overflow;
    if (!ok) files_remove(path);
    return ok;
}

bool net_download(const char *url, const char *path, int max_bytes, NetProgress *p) {
    if (!net_online()) return false;
    SDL_AtomicSet(&p->done, 0);
    SDL_AtomicSet(&p->total, 0);
    if (download_once(url, path, max_bytes, p, true)) return true;
#ifdef __vita__
    if (!SDL_AtomicGet(&p->cancel) && download_once(url, path, max_bytes, p, false)) return true;
#endif
    return false;
}
#else
char *net_get(const char *url, int max_bytes, int *len) {
    (void)url; (void)max_bytes; (void)len;
    return NULL;
}
bool net_download(const char *url, const char *path, int max_bytes, NetProgress *p) {
    (void)url; (void)path; (void)max_bytes; (void)p;
    return false;
}
#endif
