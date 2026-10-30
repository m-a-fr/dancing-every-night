#include "video_h264.h"

#ifndef __vita__
#include "h264idx.h"
#include "third_party/h264bsd/h264bsd_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Au-delà de ce retard (en secondes), on saute à l'image clé qui précède (voir video.c) */
#define MAX_LAG_S 2.0

typedef struct {
    uint8_t     *file;              /* le fichier .h264 entier (quelques Mo) */
    H264Index    ix;
    storage_t   *dec;
    double       frame_dur;
    int          w, h;              /* taille affichée */
    int          y_pitch, c_pitch, y_rows, c_rows;   /* plans alignés sur les macroblocs de 16 pixels */
    int          cursor;            /* prochaine image à décoder */
    SDL_Thread  *thread;
    SDL_mutex   *mutex;
    SDL_cond    *cond;
    bool         quit;
    int          want;              /* image demandée, < 0 = aucune */
    int          shown;             /* dernière image traitée, < 0 = aucune */
    bool         ready;
    uint8_t     *yuv;               /* copie de la dernière image décodée (Y, Cb, Cr à la suite) */
} Impl;

/* Décode l'image `idx` ; retourne les plans I420 de l'image rendue (valables jusqu'au prochain appel) ou NULL */
static const uint8_t *decode_au(Impl *im, int idx) {
    const uint8_t *p = im->file + im->ix.au[idx].off;
    uint32_t len = im->ix.au[idx].size, pos = 0;
    const uint8_t *out = NULL;
    while (pos < len) {
        uint32_t used = 0;
        uint32_t r = h264bsdDecode(im->dec, (u8 *)(uintptr_t)(p + pos), len - pos, 0, &used);
        if (r == H264BSD_HDRS_RDY) { pos += used; continue; }   /* en-têtes pris en compte : on redonne la suite */
        if (r == H264BSD_ERROR || r == H264BSD_PARAM_SET_ERROR || r == H264BSD_MEMALLOC_ERROR) {
            pos += used ? used : 1;                             /* image abîmée : on garde ce qu'on a */
            continue;
        }
        pos += used ? used : 1;
        if (r == H264BSD_PIC_RDY) {
            u32 id, idr, errs;
            const uint8_t *pic = h264bsdNextOutputPicture(im->dec, &id, &idr, &errs);
            if (pic) out = pic;
        }
    }
    return out;
}

static int decode_thread(void *arg) {
    Impl *im = arg;
    int lag_frames = (int)(MAX_LAG_S / im->frame_dur);
    SDL_LockMutex(im->mutex);
    while (!im->quit) {
        int target = im->want, shown = im->shown;
        if (target < 0 || target == shown) { SDL_CondWait(im->cond, im->mutex); continue; }
        SDL_UnlockMutex(im->mutex);

        const uint8_t *pic = NULL;
        if (shown < 0 || target < shown || target > shown + lag_frames) im->cursor = h264_index_key_at(&im->ix, target);
        else if (im->cursor > target) im->cursor = shown + 1;
        while (im->cursor <= target) {
            const uint8_t *p = decode_au(im, im->cursor);
            if (im->cursor == target) pic = p;
            im->cursor++;
        }

        SDL_LockMutex(im->mutex);
        if (pic) {
            memcpy(im->yuv, pic, (size_t)im->y_pitch * im->y_rows + 2 * (size_t)im->c_pitch * im->c_rows);
            im->ready = true;
        }
        im->shown = target;
    }
    SDL_UnlockMutex(im->mutex);
    return 0;
}

static bool read_file(const char *path, uint8_t **data, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 * 1024 * 1024) { fclose(f); return false; }
    *data = malloc((size_t)n);
    bool ok = *data && fread(*data, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok) { free(*data); *data = NULL; return false; }
    *size = (size_t)n;
    return true;
}

bool video_h264_open(Video *v, SDL_Renderer *r, const char *path) {
    *v = (Video){0};
    Impl *im = calloc(1, sizeof *im);
    if (!im) return false;
    im->want = -1; im->shown = -1;
    v->impl = im;
    v->h264 = true;

    size_t size = 0;
    if (!read_file(path, &im->file, &size) || !h264_index_build(im->file, size, &im->ix)) {
        fprintf(stderr, "video: '%s' n'est pas un flux H.264 lisible\n", path);
        video_h264_close(v);
        return false;
    }
    if (im->ix.profile != 66) {     /* h264bsd ne lit que le profil Baseline */
        fprintf(stderr, "video: '%s' n'est pas en profil Baseline (profil %d)\n", path, im->ix.profile);
        video_h264_close(v);
        return false;
    }
    im->w = im->ix.width;
    im->h = im->ix.height;
    im->y_pitch = (im->w + 15) & ~15;
    im->y_rows = (im->h + 15) & ~15;
    im->c_pitch = im->y_pitch / 2;
    im->c_rows = im->y_rows / 2;
    im->frame_dur = 1.0 / (im->ix.fps > 0.0 ? im->ix.fps : 24.0);
    im->yuv = malloc((size_t)im->y_pitch * im->y_rows + 2 * (size_t)im->c_pitch * im->c_rows);
    im->dec = h264bsdAlloc();
    if (!im->yuv || !im->dec || h264bsdInit(im->dec, 1) != 0) {     /* 1 = pas de réordonnancement : sans B-frames, une sortie par image */
        video_h264_close(v);
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    v->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, im->w, im->h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    im->mutex = SDL_CreateMutex();
    im->cond = SDL_CreateCond();
    if (!v->tex || !im->mutex || !im->cond) { video_h264_close(v); return false; }
    v->w = im->w;
    v->h = im->h;
    im->thread = SDL_CreateThread(decode_thread, "video-h264", im);
    if (!im->thread) { video_h264_close(v); return false; }
    fprintf(stderr, "video: '%s' (H.264 %dx%d, %.0f ips, %d images)\n", path, im->w, im->h, im->ix.fps, im->ix.count);
    return true;
}

void video_h264_close(Video *v) {
    Impl *im = v->impl;
    if (im) {
        if (im->thread) {
            SDL_LockMutex(im->mutex);
            im->quit = true;
            SDL_CondSignal(im->cond);
            SDL_UnlockMutex(im->mutex);
            SDL_WaitThread(im->thread, NULL);
        }
        if (im->cond) SDL_DestroyCond(im->cond);
        if (im->mutex) SDL_DestroyMutex(im->mutex);
        if (im->dec) { h264bsdShutdown(im->dec); h264bsdFree(im->dec); }
        h264_index_free(&im->ix);
        free(im->file);
        free(im->yuv);
        free(im);
    }
    if (v->tex) SDL_DestroyTexture(v->tex);
    *v = (Video){0};
}

void video_h264_seek(Video *v, float t_ms) {
    Impl *im = v->impl;
    if (!im) return;
    double t = t_ms > 0.0f ? (double)t_ms / 1000.0 : 0.0;
    int f = (int)(t / im->frame_dur);
    if (f >= im->ix.count) f = im->ix.count - 1;
    SDL_LockMutex(im->mutex);
    if (im->want != f) {
        im->want = f;
        SDL_CondSignal(im->cond);
    }
    SDL_UnlockMutex(im->mutex);
}

void video_h264_update(Video *v) {
    Impl *im = v->impl;
    if (!im) return;
    SDL_LockMutex(im->mutex);
    if (im->ready) {
        const uint8_t *y = im->yuv;
        const uint8_t *cb = y + (size_t)im->y_pitch * im->y_rows;
        const uint8_t *cr = cb + (size_t)im->c_pitch * im->c_rows;
        SDL_UpdateYUVTexture(v->tex, NULL, y, im->y_pitch, cb, im->c_pitch, cr, im->c_pitch);
        im->ready = false;
        v->has_frame = true;
    }
    SDL_UnlockMutex(im->mutex);
}

#endif  /* !__vita__ */
