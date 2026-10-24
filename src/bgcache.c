#include "bgcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/stb_image.h"

static BgCacheSlot *find_slot(BgCache *c, const char *path) {
    for (int i = 0; i < c->slot_count; i++)
        if (c->slots[i].path[0] && strcmp(c->slots[i].path, path) == 0) return &c->slots[i];
    return NULL;
}

/* Niveaux de gris un peu contrastés (+30 %, +8 % de luminosité, comme la maquette) : fait dans le fil de décodage,
   donc sans coût à l'écran, et valable pour n'importe quelle image (cartes de la communauté comprises). */
static void to_grayscale(unsigned char *px, int count) {
    for (int i = 0; i < count; i++, px += 4) {
        float l = (px[0] * 0.299f + px[1] * 0.587f + px[2] * 0.114f) / 255.0f;
        float v = ((l - 0.5f) * 1.3f + 0.5f) * 1.08f;
        unsigned char g = (unsigned char)(v < 0.0f ? 0.0f : v > 1.0f ? 255.0f : v * 255.0f);
        px[0] = px[1] = px[2] = g;
    }
}

/* Vignette : recadrage centré au format tw x th (comme un « cover » CSS) puis réduction par moyenne des pixels
   couverts. Retourne un nouveau tampon RGBA (malloc, libérable par stbi_image_free), NULL si mémoire insuffisante. */
static unsigned char *shrink_cover(const unsigned char *px, int w, int h, int tw, int th) {
    unsigned char *out = malloc((size_t)tw * (size_t)th * 4);
    if (!out) return NULL;
    float cw = (float)w, ch = (float)h;
    if (cw * th > ch * tw) cw = ch * tw / th; else ch = cw * th / tw;   /* zone gardée, au format de la vignette */
    float x0 = (w - cw) * 0.5f, y0 = (h - ch) * 0.5f, sx = cw / tw, sy = ch / th;
    for (int ty = 0; ty < th; ty++) {
        int ya = (int)(y0 + ty * sy), yb = (int)(y0 + (ty + 1) * sy);
        if (yb <= ya) yb = ya + 1;
        if (yb > h) yb = h;
        for (int tx = 0; tx < tw; tx++) {
            int xa = (int)(x0 + tx * sx), xb = (int)(x0 + (tx + 1) * sx);
            if (xb <= xa) xb = xa + 1;
            if (xb > w) xb = w;
            unsigned sum[4] = {0, 0, 0, 0}, n = 0;
            for (int y = ya; y < yb; y++)
                for (int x = xa; x < xb; x++, n++)
                    for (int k = 0; k < 4; k++) sum[k] += px[((size_t)y * w + x) * 4 + k];
            for (int k = 0; k < 4; k++) out[((size_t)ty * tw + tx) * 4 + k] = (unsigned char)(n ? sum[k] / n : 0);
        }
    }
    return out;
}

/* Fil de décodage : prend la demande la plus prioritaire, la décode, attend qu'elle soit téléversée */
static int decode_thread(void *arg) {
    BgCache *c = arg;
    SDL_LockMutex(c->lock);
    for (;;) {
        while (!c->quit && (c->queue_n == 0 || c->done)) SDL_CondWait(c->cond, c->lock);
        if (c->quit) break;
        char path[BGCACHE_PATH];
        int dim = c->queue_dim[0];
        snprintf(path, sizeof path, "%s", c->queue[0]);
        memmove(c->queue[0], c->queue[1], sizeof c->queue[0] * (size_t)(BGCACHE_QUEUE - 1));
        memmove(&c->queue_dim[0], &c->queue_dim[1], sizeof c->queue_dim[0] * (size_t)(BGCACHE_QUEUE - 1));
        c->queue_n--;
        snprintf(c->busy, sizeof c->busy, "%s", path);
        SDL_UnlockMutex(c->lock);

        int w = 0, h = 0, n;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);   /* hors verrou : c'est la partie lente */
        if (!px) fprintf(stderr, "bgcache: impossible de charger '%s'\n", path);
        else if (c->grayscale) to_grayscale(px, w * h);   /* bichromie du menu : la teinte vient d'un dégradé en multiply */
        if (px && c->thumb_w > 0 && c->thumb_h > 0) {
            unsigned char *small = shrink_cover(px, w, h, c->thumb_w, c->thumb_h);
            stbi_image_free(px);
            px = small;
            w = c->thumb_w; h = c->thumb_h;
        }

        SDL_LockMutex(c->lock);
        c->busy[0] = '\0';
        if (px) {
            snprintf(c->done_path, sizeof c->done_path, "%s", path);
            c->done_dim = dim;
            c->done_w = w;
            c->done_h = h;
            c->done_px = px;
            c->done = true;
        }
    }
    SDL_UnlockMutex(c->lock);
    return 0;
}

bool bgcache_init(BgCache *c, SDL_Renderer *r) {
    *c = (BgCache){0};
    c->renderer = r;
    c->slot_count = BGCACHE_SLOTS;
    c->lock = SDL_CreateMutex();
    c->cond = SDL_CreateCond();
    if (!c->lock || !c->cond) return false;
    c->thread = SDL_CreateThread(decode_thread, "bg-decode", c);
    return c->thread != NULL;
}

void bgcache_clear(BgCache *c) {
    if (c->lock) SDL_LockMutex(c->lock);
    c->queue_n = 0;
    if (c->done) { stbi_image_free(c->done_px); c->done_px = NULL; c->done = false; }
    if (c->cond) SDL_CondSignal(c->cond);
    if (c->lock) SDL_UnlockMutex(c->lock);
    for (int i = 0; i < BGCACHE_SLOTS_MAX; i++) {
        background_free(&c->slots[i].bg);
        c->slots[i].path[0] = '\0';
    }
}

void bgcache_shutdown(BgCache *c) {
    if (c->thread) {
        SDL_LockMutex(c->lock);
        c->quit = true;
        SDL_CondSignal(c->cond);
        SDL_UnlockMutex(c->lock);
        SDL_WaitThread(c->thread, NULL);
        c->thread = NULL;
    }
    bgcache_clear(c);
    if (c->cond) SDL_DestroyCond(c->cond);
    if (c->lock) SDL_DestroyMutex(c->lock);
    c->cond = NULL;
    c->lock = NULL;
}

void bgcache_want(BgCache *c, const char *const *paths, const int *dims, int n) {
    if (!c->lock) return;
    SDL_LockMutex(c->lock);
    c->queue_n = 0;
    for (int i = 0; i < n && c->queue_n < BGCACHE_QUEUE; i++) {
        const char *p = paths[i];
        if (!p[0] || find_slot(c, p) || strcmp(p, c->busy) == 0 || (c->done && strcmp(p, c->done_path) == 0)) continue;
        bool dup = false;
        for (int k = 0; k < c->queue_n; k++) dup |= strcmp(c->queue[k], p) == 0;
        if (dup) continue;
        snprintf(c->queue[c->queue_n], BGCACHE_PATH, "%s", p);
        c->queue_dim[c->queue_n++] = dims[i];
    }
    if (c->queue_n) SDL_CondSignal(c->cond);
    SDL_UnlockMutex(c->lock);
}

void bgcache_update(BgCache *c) {
    if (!c->lock) return;
    SDL_LockMutex(c->lock);
    if (!c->done) { SDL_UnlockMutex(c->lock); return; }
    char path[BGCACHE_PATH];
    snprintf(path, sizeof path, "%s", c->done_path);
    unsigned char *px = c->done_px;
    int w = c->done_w, h = c->done_h, dim = c->done_dim;
    c->done_px = NULL;
    c->done = false;
    SDL_CondSignal(c->cond);   /* le fil peut décoder la suivante pendant qu'on téléverse */
    SDL_UnlockMutex(c->lock);

    /* emplacement libre, sinon le moins récemment utilisé */
    BgCacheSlot *slot = &c->slots[0];
    for (int i = 0; i < c->slot_count && i < BGCACHE_SLOTS_MAX; i++) {
        if (!c->slots[i].path[0]) { slot = &c->slots[i]; break; }
        if (c->slots[i].last_use < slot->last_use) slot = &c->slots[i];
    }
    background_free(&slot->bg);
    slot->path[0] = '\0';
    if (background_from_rgba(&slot->bg, c->renderer, px, w, h, dim)) {
        slot->bg.image_fade = 1.0f;   /* affichage immédiat, sans fondu */
        snprintf(slot->path, sizeof slot->path, "%s", path);
        slot->last_use = ++c->clock;
    }
    stbi_image_free(px);
}

const Background *bgcache_get(BgCache *c, const char *path) {
    if (!path[0]) return NULL;
    BgCacheSlot *s = find_slot(c, path);
    if (!s) return NULL;
    s->last_use = ++c->clock;
    return &s->bg;
}

const Background *bgcache_peek(const BgCache *c, const char *path) {
    if (!path[0]) return NULL;
    for (int i = 0; i < c->slot_count; i++)
        if (c->slots[i].path[0] && strcmp(c->slots[i].path, path) == 0) return &c->slots[i].bg;
    return NULL;
}
