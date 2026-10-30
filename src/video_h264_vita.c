#include "video_h264.h"

#ifdef __vita__
#include "config.h"
#include "h264idx.h"
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/videodec.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mémoire physiquement contiguë et non mise en cache, exigée par le décodeur (valeur de
   SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW : seul le _R est dans les en-têtes) */
#define MEMBLOCK_PHYCONT_NC_RW 0x0D808060u
#define BLOCK_ALIGN (1024 * 1024)
#define ES_BUF_SIZE (1024 * 1024)   /* plus grande image compressée acceptée */
/* Au-delà de ce retard (en secondes), on saute à l'image clé qui précède (voir video.c) */
#define MAX_LAG_S 2.0
#define MAX_REF_FRAMES 4
#define SETTLE_US 3000   /* attente avant de copier une image décodée */

typedef struct {
    uint8_t     *file;              /* le fichier .h264 entier (quelques Mo) */
    H264Index    ix;
    double       frame_dur;
    int          w, h, pitch;       /* pitch en pixels (multiple de 16) */
    SceUID       blk_dec, blk_es, blk_pic;
    void        *es_buf, *pic_buf;
    SceAvcdecCtrl dec;
    bool         lib_up, dec_up;
    int          cursor;            /* prochaine image à donner au décodeur */
    SDL_Texture *tex[2];            /* écrites en alternance : le GPU peut encore lire la précédente */
    int          tex_next;
    SDL_Thread  *thread;
    SDL_mutex   *mutex;
    SDL_cond    *cond;
    bool         quit;
    int          want;              /* image demandée, < 0 = aucune */
    int          shown;             /* dernière image traitée, < 0 = aucune */
    bool         ready;
    uint8_t     *rgba;              /* copie de la dernière image décodée */
    int          log_lines;
    int          mixed_logged;
    uint32_t     gen;               /* génération des images données au décodeur (change à chaque saut) */
    int          last_out;          /* numéro de la dernière image rendue (d'après son étiquette), -1 = aucune */
    bool         pts_ok;            /* le décodeur rend bien les étiquettes qu'on lui donne */
    int          requests;
    int          delay;             /* images de retard du décodeur (nb d'images données avant la 1re sortie), -1 = inconnu */
} Impl;

/* Journal (SAVE_DIR video_log.txt, réécrit à chaque ouverture) : stderr n'est pas visible sur la console */
static void hwlog(Impl *im, const char *fmt, ...) {
    if (im->log_lines >= 200) return;
    FILE *f = fopen(SAVE_DIR "video_log.txt", im->log_lines == 0 ? "w" : "a");
    if (!f) return;
    im->log_lines++;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void *phycont_alloc(size_t size, SceUID *uid) {
    size = (size + BLOCK_ALIGN - 1) & ~(size_t)(BLOCK_ALIGN - 1);
    *uid = sceKernelAllocMemBlock("video_hw", MEMBLOCK_PHYCONT_NC_RW, (SceSize)size, NULL);
    void *base = NULL;
    if (*uid < 0 || sceKernelGetMemBlockBase(*uid, &base) < 0) { *uid = -1; return NULL; }
    return base;
}

/* Donne l'image `idx` au décodeur, étiquetée (génération, numéro). Retourne vrai si une image décodée est sortie
   (dans pic_buf) ; *out = numéro de cette image d'après son étiquette (-1 si inconnu ou d'une ancienne génération). */
static bool decode_au(Impl *im, int idx, uint32_t gen, int *out) {
    *out = -1;
    H264Au a = im->ix.au[idx];
    if (a.size > ES_BUF_SIZE) { hwlog(im, "image %d trop grosse (%u octets)", idx, a.size); return false; }
    memcpy(im->es_buf, im->file + a.off, a.size);

    SceAvcdecAu au;
    memset(&au, 0, sizeof au);
    au.pts.upper = au.dts.upper = gen;
    au.pts.lower = au.dts.lower = (uint32_t)idx;
    au.es.pBuf = im->es_buf;
    au.es.size = a.size;

    SceAvcdecPicture pic;
    memset(&pic, 0, sizeof pic);
    pic.size = sizeof pic;
    pic.frame.pixelType = SCE_AVCDEC_PIXELFORMAT_RGBA8888;
    pic.frame.framePitch = (uint32_t)im->pitch;
    pic.frame.frameWidth = (uint32_t)im->w;
    pic.frame.frameHeight = (uint32_t)im->h;
    pic.frame.opt.rgba.alpha = 255;
    pic.frame.pPicture[0] = im->pic_buf;
    SceAvcdecPicture *pp = &pic;
    SceAvcdecArrayPicture arr;
    memset(&arr, 0, sizeof arr);
    arr.numOfElm = 1;
    arr.pPicture = &pp;

    int r = sceAvcdecDecode(&im->dec, &au, &arr);
    bool ok = r >= 0 && arr.numOfOutput > 0;
    if (ok && pic.info.pts.upper == gen && pic.info.pts.lower < (uint32_t)im->ix.count) *out = (int)pic.info.pts.lower;
    if (im->log_lines < 24 || r < 0 || arr.numOfOutput > 1)
        hwlog(im, "decode image %d -> 0x%08X, sorties=%u, étiquette rendue=%08X/%08X", idx, (unsigned)r, arr.numOfOutput,
              (unsigned)pic.info.pts.upper, (unsigned)pic.info.pts.lower);
    return ok;
}

static void copy_picture(Impl *im) {
    const uint8_t *src = im->pic_buf;
    size_t row = (size_t)im->w * 4, pitch_b = (size_t)im->pitch * 4;
    for (int y = 0; y < im->h; y++) memcpy(im->rgba + (size_t)y * row, src + (size_t)y * pitch_b, row);
}

/* Quelques pixels de l'image de sortie (haut, milieu, bas) pour voir si elle change encore après le retour du décodeur */
static void probe_pixels(const Impl *im, uint32_t out[3]) {
    const uint32_t *p = im->pic_buf;
    out[0] = p[(size_t)4 * im->pitch + im->w / 2];
    out[1] = p[(size_t)(im->h / 2) * im->pitch + im->w / 2];
    out[2] = p[(size_t)(im->h - 4) * im->pitch + im->w / 2];
}

/* sceAvcdecDecode rend la main avant que l'image soit entièrement écrite ? (image mi-noire mi-blanche vue sur la console) :
   on attend un peu avant de copier, et on note dans le journal si l'image a encore bougé pendant l'attente. */
static void settle_picture(Impl *im) {
    uint32_t a[3], b[3];
    probe_pixels(im, a);
    sceKernelDelayThread(SETTLE_US);
    probe_pixels(im, b);
    /* le clip de test de synchro n'a que des images tout noires ou tout blanches : haut/milieu/bas différents = mélange */
    if ((b[0] != b[1] || b[1] != b[2]) && im->mixed_logged < 8) {
        im->mixed_logged++;
        hwlog(im, "image MÉLANGÉE en sortie du décodeur : %08X/%08X/%08X", b[0], b[1], b[2]);
    }
    if (memcmp(a, b, sizeof a) != 0)
        hwlog(im, "image encore en écriture au retour du décodeur : %08X/%08X/%08X -> %08X/%08X/%08X", a[0], a[1], a[2], b[0], b[1], b[2]);
}

static int decode_thread(void *arg) {
    Impl *im = arg;
    int lag_frames = (int)(MAX_LAG_S / im->frame_dur);
    /* Sonde : la première image rendue doit porter l'étiquette de l'image 0, sinon on se rabat sur un retard fixe */
    for (int j = 0; j < 8 && j < im->ix.count; j++) {
        int o;
        if (decode_au(im, j, 0, &o)) { im->delay = j; im->pts_ok = (o == 0); break; }
    }
    hwlog(im, "retard du décodeur au démarrage : %d images ; étiquettes %s", im->delay, im->pts_ok ? "fiables" : "NON fiables (retard fixe)");
    SDL_LockMutex(im->mutex);
    while (!im->quit) {
        int target = im->want, shown = im->shown;
        if (target < 0 || target == shown) { SDL_CondWait(im->cond, im->mutex); continue; }
        SDL_UnlockMutex(im->mutex);

        bool got = false;
        if (shown < 0 || target < shown || target > shown + lag_frames) {
            im->cursor = h264_index_key_at(&im->ix, target);  /* saut à l'image clé <= target */
            im->gen++;                                        /* les images encore en attente dans le décodeur sont périmées */
            im->last_out = -1;
        } else if (!im->pts_ok && im->cursor > target + (im->delay > 0 ? im->delay : 0)) {
            im->cursor = shown + 1;
        }
        for (;;) {
            if (im->cursor >= im->ix.count) break;
            if (im->pts_ok) {
                if (im->last_out >= target) break;            /* l'image voulue est déjà sortie */
            } else {
                int need = target + (im->delay > 0 ? im->delay : 0);
                if (need >= im->ix.count) need = im->ix.count - 1;
                if (im->cursor > need) break;
            }
            int o;
            if (decode_au(im, im->cursor++, im->gen, &o)) {
                got = true;
                if (o >= 0) im->last_out = o;
            }
        }
        if (++im->requests % 120 == 0 && im->requests < 3000)
            hwlog(im, "demande %d : image voulue %d, dernière rendue %d, prochaine donnée %d", im->requests, target, im->last_out, im->cursor);

        if (got) settle_picture(im);
        SDL_LockMutex(im->mutex);
        if (got) { copy_picture(im); im->ready = true; }
        im->shown = target;     /* même sans image sortie : on ne reboucle pas */
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
    im->blk_dec = im->blk_es = im->blk_pic = -1;
    im->want = -1; im->shown = -1; im->delay = -1; im->last_out = -1;
    v->impl = im;
    v->h264 = true;

    size_t size = 0;
    if (!read_file(path, &im->file, &size) || !h264_index_build(im->file, size, &im->ix)) {
        fprintf(stderr, "video: '%s' n'est pas un flux H.264 lisible\n", path);
        hwlog(im, "'%s' : flux H.264 illisible", path);
        video_h264_close(v);
        return false;
    }
    im->w = im->ix.width;
    im->h = im->ix.height;
    im->pitch = (im->w + 15) & ~15;
    im->frame_dur = 1.0 / (im->ix.fps > 0.0 ? im->ix.fps : 24.0);
    int refs = im->ix.num_ref_frames < 1 ? 1 : im->ix.num_ref_frames > MAX_REF_FRAMES ? MAX_REF_FRAMES : im->ix.num_ref_frames;
    hwlog(im, "'%s' : %dx%d, %d images, %.3f ips, refs=%d", path, im->w, im->h, im->ix.count, im->ix.fps, refs);

    sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC);
    SceVideodecQueryInitInfoHwAvcdec init = {
        .size = sizeof init, .horizontal = (uint32_t)im->pitch, .vertical = (uint32_t)((im->h + 15) & ~15),
        .numOfRefFrames = (uint32_t)refs, .numOfStreams = 1,
    };
    int rc = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, &init);
    hwlog(im, "sceVideodecInitLibrary -> 0x%08X", (unsigned)rc);
    if (rc < 0) { video_h264_close(v); return false; }
    im->lib_up = true;

    SceAvcdecQueryDecoderInfo q = { init.horizontal, init.vertical, init.numOfRefFrames };
    SceAvcdecDecoderInfo di;
    memset(&di, 0, sizeof di);
    rc = sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC, &q, &di);
    hwlog(im, "sceAvcdecQueryDecoderMemSize -> 0x%08X, %u octets", (unsigned)rc, di.frameMemSize);
    if (rc < 0) { video_h264_close(v); return false; }
    void *dec_mem = phycont_alloc(di.frameMemSize, &im->blk_dec);
    im->es_buf = phycont_alloc(ES_BUF_SIZE, &im->blk_es);
    im->pic_buf = phycont_alloc((size_t)im->pitch * init.vertical * 4, &im->blk_pic);
    im->rgba = malloc((size_t)im->w * (size_t)im->h * 4);
    if (!dec_mem || !im->es_buf || !im->pic_buf || !im->rgba) {
        hwlog(im, "mémoire contiguë ou RGBA indisponible");
        video_h264_close(v);
        return false;
    }
    memset(&im->dec, 0, sizeof im->dec);
    im->dec.frameBuf.pBuf = dec_mem;
    im->dec.frameBuf.size = (uint32_t)((di.frameMemSize + BLOCK_ALIGN - 1) & ~(uint32_t)(BLOCK_ALIGN - 1));
    rc = sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC, &im->dec, &q);
    hwlog(im, "sceAvcdecCreateDecoder -> 0x%08X", (unsigned)rc);
    if (rc < 0) { video_h264_close(v); return false; }
    im->dec_up = true;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    for (int i = 0; i < 2; i++)
        im->tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, im->w, im->h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    im->mutex = SDL_CreateMutex();
    im->cond = SDL_CreateCond();
    if (!im->tex[0] || !im->tex[1] || !im->mutex || !im->cond) { video_h264_close(v); return false; }
    for (int i = 0; i < 2; i++) SDL_SetTextureBlendMode(im->tex[i], SDL_BLENDMODE_BLEND);   /* background.c ne règle que la première */
    v->tex = im->tex[0];
    v->w = im->w;
    v->h = im->h;
    im->thread = SDL_CreateThread(decode_thread, "video-hw", im);
    if (!im->thread) { video_h264_close(v); return false; }
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
        if (im->dec_up) sceAvcdecDeleteDecoder(&im->dec);
        if (im->lib_up) sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
        if (im->blk_dec >= 0) sceKernelFreeMemBlock(im->blk_dec);
        if (im->blk_es >= 0) sceKernelFreeMemBlock(im->blk_es);
        if (im->blk_pic >= 0) sceKernelFreeMemBlock(im->blk_pic);
        for (int i = 0; i < 2; i++) if (im->tex[i]) SDL_DestroyTexture(im->tex[i]);
        v->tex = NULL;
        if (im->cond) SDL_DestroyCond(im->cond);
        if (im->mutex) SDL_DestroyMutex(im->mutex);
        h264_index_free(&im->ix);
        free(im->file);
        free(im->rgba);
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
        SDL_Texture *t = im->tex[im->tex_next];
        im->tex_next ^= 1;
        SDL_UpdateTexture(t, NULL, im->rgba, im->w * 4);
        v->tex = t;
        im->ready = false;
        v->has_frame = true;
    }
    SDL_UnlockMutex(im->mutex);
}

#endif  /* __vita__ */
