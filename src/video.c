#include "video.h"
#include "video_h264.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "third_party/pl_mpeg.h"

/* Au-delà de ce retard (ou en cas de retour en arrière), on saute à l'image clé qui précède
   le temps demandé, puis on rattrape image par image. Doit rester supérieur à l'écart entre
   deux images clés (1 s, voir tools/media.py), sinon on resauterait sans fin à la même. */
#define MAX_LAG_S 2.0

typedef struct {
    plm_t      *plm;
    double      frame_dur;    /* 1 / cadence du fichier */
    SDL_Thread *thread;
    SDL_mutex  *mutex;
    SDL_cond   *cond;
    bool        quit;
    double      want;         /* temps demandé (s), < 0 = aucun */
    double      shown;        /* temps de la dernière image décodée, < 0 = aucune */
    bool        ready;        /* une image attend d'être téléversée */
    uint8_t    *y, *cb, *cr;  /* copie des plans de la dernière image (tailles alignées sur 16) */
    int         y_pitch, c_pitch, y_rows, c_rows;
    int         w, h;
} Impl;

static void copy_frame(Impl *im, const plm_frame_t *f) {
    memcpy(im->y, f->y.data, (size_t)im->y_pitch * (size_t)im->y_rows);
    memcpy(im->cb, f->cb.data, (size_t)im->c_pitch * (size_t)im->c_rows);
    memcpy(im->cr, f->cr.data, (size_t)im->c_pitch * (size_t)im->c_rows);
    im->shown = f->time;
    im->ready = true;
}

static int decode_thread(void *arg) {
    Impl *im = arg;
    SDL_LockMutex(im->mutex);
    while (!im->quit) {
        double target = im->want;
        double shown = im->shown;
        /* rien à faire : pas de demande, ou l'image affichée couvre déjà le temps demandé */
        if (target < 0.0 || (shown >= 0.0 && target >= shown && target < shown + im->frame_dur)) {
            SDL_CondWait(im->cond, im->mutex);
            continue;
        }
        SDL_UnlockMutex(im->mutex);

        /* décodage hors verrou */
        plm_frame_t *f = NULL;
        if (shown < 0.0 || target < shown || target > shown + MAX_LAG_S) {
            f = plm_seek_frame(im->plm, target, 0);          /* image clé <= target */
        } else {
            f = plm_decode_video(im->plm);                    /* image suivante */
        }

        SDL_LockMutex(im->mutex);
        if (f) {
            copy_frame(im, f);
        } else {
            im->shown = target; /* fin du fichier ou données illisibles : on garde la dernière image */
        }
    }
    SDL_UnlockMutex(im->mutex);
    return 0;
}

static bool is_h264(const char *path) {
    size_t n = strlen(path);
    return n > 5 && strcmp(path + n - 5, ".h264") == 0;
}

bool video_open(Video *v, SDL_Renderer *r, const char *path) {
    *v = (Video){0};
    if (!path || !path[0]) return false;
    if (is_h264(path)) return video_h264_open(v, r, path);

    FILE *fh = fopen(path, "rb");
    if (!fh) { fprintf(stderr, "video: introuvable '%s'\n", path); return false; }
    plm_t *plm = plm_create_with_file(fh, 1);
    if (!plm || !plm_probe(plm, 5000 * 1024) || plm_get_num_video_streams(plm) < 1) {
        fprintf(stderr, "video: '%s' n'est pas une vidéo MPEG-1 lisible\n", path);
        if (plm) plm_destroy(plm); else fclose(fh);
        return false;
    }
    plm_set_audio_enabled(plm, 0);

    Impl *im = calloc(1, sizeof *im);
    if (!im) { plm_destroy(plm); return false; }
    im->plm = plm;
    im->w = plm_get_width(plm);
    im->h = plm_get_height(plm);
    double fps = plm_get_framerate(plm);
    im->frame_dur = fps > 0.0 ? 1.0 / fps : 1.0 / 30.0;
    im->want = -1.0; im->shown = -1.0;
    /* les plans décodés sont alignés sur les macroblocs de 16x16 pixels */
    im->y_pitch = (im->w + 15) & ~15;
    im->y_rows = (im->h + 15) & ~15;
    im->c_pitch = im->y_pitch / 2;
    im->c_rows = im->y_rows / 2;
    im->y = malloc((size_t)im->y_pitch * (size_t)im->y_rows);
    im->cb = malloc((size_t)im->c_pitch * (size_t)im->c_rows);
    im->cr = malloc((size_t)im->c_pitch * (size_t)im->c_rows);

    /* YUV -> RGB fait par le GPU (texture IYUV), pas par le CPU */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    v->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, im->w, im->h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    im->mutex = SDL_CreateMutex();
    im->cond = SDL_CreateCond();
    v->impl = im;
    if (!im->y || !im->cb || !im->cr || !v->tex || !im->mutex || !im->cond) {
        fprintf(stderr, "video: ressources: %s\n", SDL_GetError());
        video_close(v);
        return false;
    }
    v->w = im->w;
    v->h = im->h;
    im->thread = SDL_CreateThread(decode_thread, "video-decode", im);
    fprintf(stderr, "video: '%s' (%dx%d, %.0f ips, %.1f s)\n", path, im->w, im->h, fps, plm_get_duration(plm));
    return true;
}

void video_close(Video *v) {
    if (v->h264) { video_h264_close(v); return; }
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
        if (im->plm) plm_destroy(im->plm);
        free(im->y); free(im->cb); free(im->cr);
        free(im);
    }
    if (v->tex) SDL_DestroyTexture(v->tex);
    *v = (Video){0};
}

void video_seek(Video *v, float t_ms) {
    if (v->h264) { video_h264_seek(v, t_ms); return; }
    Impl *im = v->impl;
    if (!im) return;
    double t = t_ms > 0.0f ? (double)t_ms / 1000.0 : 0.0;
    SDL_LockMutex(im->mutex);
    if (im->want != t) {
        im->want = t;
        SDL_CondSignal(im->cond);
    }
    SDL_UnlockMutex(im->mutex);
}

void video_update(Video *v) {
    if (v->h264) { video_h264_update(v); return; }
    Impl *im = v->impl;
    if (!im) return;
    SDL_LockMutex(im->mutex);
    if (im->ready) {
        SDL_UpdateYUVTexture(v->tex, NULL, im->y, im->y_pitch, im->cb, im->c_pitch, im->cr, im->c_pitch);
        im->ready = false;
        v->has_frame = true;
    }
    SDL_UnlockMutex(im->mutex);
}
