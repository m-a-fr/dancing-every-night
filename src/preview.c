#include "preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"

#define PREVIEW_FREQ     44100
#define SETTLE_S         0.35f   /* la sélection doit rester posée ce temps avant qu'on charge sa musique */
#define FADE_IN_MS       800     /* l'extrait démarre un fondu avant le refrain : son premier temps tombe à plein volume */
#define PLAY_MS          20000   /* durée de l'extrait avant le fondu de sortie */
#define FADE_OUT_MS      2000
#define GAP_MS           700     /* silence avant de reprendre l'extrait en boucle */
#define CUT_MS           250     /* fondu rapide quand la sélection change */
#define PREVIEW_VOLUME   0.9f

struct PreviewTrack {
    stb_vorbis    *v;
    unsigned char *data;
    char           path[PREVIEW_PATH];
    unsigned       start;    /* premier échantillon de l'extrait */
    int            pos;      /* frames jouées depuis le début de l'extrait (boucle) */
    float          kill;     /* 1 = joue, descend vers 0 quand stopping */
    bool           stopping;
};

static void track_free(PreviewTrack *t) {
    if (!t) return;
    if (t->v) stb_vorbis_close(t->v);
    free(t->data);
    free(t);
}

/* Fil de chargement : lecture du fichier, ouverture et positionnement au début de l'extrait */
static PreviewTrack *track_load(const char *path, int start_ms, int freq) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "preview: piste introuvable '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    PreviewTrack *t = calloc(1, sizeof *t);
    if (t) t->data = malloc((size_t)size);
    if (!t || !t->data || fread(t->data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        track_free(t);
        return NULL;
    }
    fclose(f);

    int err = 0;
    t->v = stb_vorbis_open_memory(t->data, (int)size, &err, NULL);
    if (!t->v) { fprintf(stderr, "preview: OGG illisible '%s' (erreur %d)\n", path, err); track_free(t); return NULL; }
    stb_vorbis_info info = stb_vorbis_get_info(t->v);
    if ((int)info.sample_rate != freq) {   /* le périphérique ne rééchantillonne pas : la musique serait faussée */
        fprintf(stderr, "preview: '%s' en %u Hz (attendu %d), pas d'aperçu\n", path, info.sample_rate, freq);
        track_free(t);
        return NULL;
    }
    long total = (long)stb_vorbis_stream_length_in_samples(t->v);
    long start = (long)(start_ms - FADE_IN_MS) * freq / 1000;
    long span = (long)(PLAY_MS + FADE_OUT_MS) * freq / 1000;
    if (start + span > total) start = total - span;   /* extrait entier dans le morceau */
    if (start < 0) start = 0;
    t->start = (unsigned)start;
    stb_vorbis_seek(t->v, t->start);
    t->kill = 1.0f;
    snprintf(t->path, sizeof t->path, "%s", path);
    return t;
}

static void preview_music(Preview *p, Uint8 *stream, int len) {
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (int)(2 * sizeof(Sint16));
    memset(stream, 0, (size_t)len);
    p->cb_ticks = SDL_GetTicks();
    p->cb_frames = frames;

    if (p->cur && p->next) p->cur->stopping = true;   /* une autre chanson attend : on coupe en fondu */
    if (!p->cur && p->next) { p->cur = p->next; p->next = NULL; }
    PreviewTrack *t = p->cur;
    if (!t) return;

    const int fade_in = p->freq * FADE_IN_MS / 1000, play = p->freq * PLAY_MS / 1000;
    const int fade_out = p->freq * FADE_OUT_MS / 1000, gap = p->freq * GAP_MS / 1000;
    const float cut_step = 1000.0f / (float)(p->freq * CUT_MS);
    int got = 0;
    if (t->pos < play + fade_out) got = stb_vorbis_get_samples_short_interleaved(t->v, 2, out, frames * 2);
    for (int i = 0; i < got; i++) {
        int pos = t->pos + i;
        float g = pos < fade_in ? (float)pos / (float)fade_in : 1.0f;
        if (pos > play) g = 1.0f - (float)(pos - play) / (float)fade_out;
        if (t->stopping && (t->kill -= cut_step) < 0.0f) t->kill = 0.0f;
        g *= t->kill * PREVIEW_VOLUME;
        if (g < 0.0f) g = 0.0f;
        out[i * 2]     = (Sint16)(out[i * 2] * g);
        out[i * 2 + 1] = (Sint16)(out[i * 2 + 1] * g);
    }
    if (t->stopping && got == 0) t->kill = 0.0f;   /* pendant le silence de la boucle : rien à couper */
    t->pos += frames;
    if (got < frames && t->pos < play + fade_out) t->pos = play + fade_out;   /* fin du fichier : silence puis boucle */

    if (t->stopping && t->kill <= 0.0f) {
        if (!p->retired) { p->retired = t; p->cur = NULL; }   /* sinon on attend que le thread principal ait libéré */
    } else if (t->pos >= play + fade_out + gap) {
        stb_vorbis_seek(t->v, t->start);   /* pendant le silence : un éventuel à-coup est inaudible */
        t->pos = 0;
    }
}

/* Callback du périphérique : l'extrait de chanson, puis par-dessus la musique de fond et les bruitages */
static void SDLCALL preview_callback(void *userdata, Uint8 *stream, int len) {
    Preview *p = userdata;
    preview_music(p, stream, len);
    if (p->sfx) sfx_mix(p->sfx, (Sint16 *)stream, len / (int)(2 * sizeof(Sint16)));
}

int preview_position_ms(Preview *p, const char *path) {
    if (!p->dev || !path[0]) return -1;
    int ms = -1;
    const int play_end = p->freq * (PLAY_MS + FADE_OUT_MS) / 1000;
    SDL_LockAudioDevice(p->dev);
    const PreviewTrack *t = p->cur;
    if (t && !t->stopping && t->pos < play_end && strcmp(t->path, path) == 0) {
        /* le bloc écrit au dernier appel ne sonnera qu'après celui que le périphérique joue encore */
        long frames = (long)t->start + t->pos - 2L * p->cb_frames;
        ms = (int)(frames * 1000 / p->freq) + (int)(SDL_GetTicks() - p->cb_ticks);
    }
    SDL_UnlockAudioDevice(p->dev);
    return ms;
}

static int loader_thread(void *arg) {
    Preview *p = arg;
    SDL_LockMutex(p->lock);
    for (;;) {
        while (!p->quit && p->loaded_gen == p->gen) SDL_CondWait(p->cond, p->lock);
        if (p->quit) break;
        unsigned gen = p->gen;
        char path[PREVIEW_PATH];
        snprintf(path, sizeof path, "%s", p->load_path);
        int ms = p->load_ms, freq = p->freq;
        p->loaded_gen = gen;
        SDL_UnlockMutex(p->lock);

        PreviewTrack *t = path[0] ? track_load(path, ms, freq) : NULL;   /* hors verrou : la partie lente */

        SDL_LockMutex(p->lock);
        if (t && gen == p->gen && p->dev) {
            SDL_LockAudioDevice(p->dev);
            PreviewTrack *old = p->next;
            p->next = t;
            SDL_UnlockAudioDevice(p->dev);
            track_free(old);
        } else {
            track_free(t);   /* demande périmée entre-temps */
        }
    }
    SDL_UnlockMutex(p->lock);
    return 0;
}

bool preview_init(Preview *p) {
    *p = (Preview){0};
    p->freq = PREVIEW_FREQ;
    p->lock = SDL_CreateMutex();
    p->cond = SDL_CreateCond();
    if (!p->lock || !p->cond) return false;
    p->thread = SDL_CreateThread(loader_thread, "preview-load", p);
    return p->thread != NULL;
}

/* Annule le chargement en cours ou demandé (sous lock) */
static void cancel_load(Preview *p) {
    p->load_path[0] = '\0';
    p->gen++;
    SDL_CondSignal(p->cond);
}

void preview_close(Preview *p) {
    if (p->lock) {
        SDL_LockMutex(p->lock);
        cancel_load(p);
        if (p->dev) SDL_CloseAudioDevice(p->dev);   /* le callback ne tourne plus après ce retour */
        p->dev = 0;
        if (p->sfx) sfx_detach(p->sfx);
        SDL_UnlockMutex(p->lock);
    }
    track_free(p->cur);
    track_free(p->next);
    track_free(p->retired);
    p->cur = p->next = p->retired = NULL;
    p->want_path[0] = p->sent_path[0] = '\0';
    p->settle = 0.0f;
}

void preview_shutdown(Preview *p) {
    if (p->thread) {
        SDL_LockMutex(p->lock);
        p->quit = true;
        SDL_CondSignal(p->cond);
        SDL_UnlockMutex(p->lock);
        SDL_WaitThread(p->thread, NULL);
        p->thread = NULL;
    }
    preview_close(p);
    if (p->cond) SDL_DestroyCond(p->cond);
    if (p->lock) SDL_DestroyMutex(p->lock);
    p->cond = NULL;
    p->lock = NULL;
}

void preview_want(Preview *p, const char *path, int start_ms) {
    if (strcmp(path, p->want_path) == 0) return;
    snprintf(p->want_path, sizeof p->want_path, "%s", path);
    p->want_ms = start_ms;
    p->settle = 0.0f;
    if (strcmp(path, p->sent_path) == 0) return;   /* retour sur la chanson qui joue déjà */

    /* la sélection a changé : la musique en cours s'efface tout de suite, le chargement en cours est abandonné */
    p->sent_path[0] = '\0';
    if (p->lock) { SDL_LockMutex(p->lock); cancel_load(p); SDL_UnlockMutex(p->lock); }
    if (!p->dev) return;
    SDL_LockAudioDevice(p->dev);
    if (p->cur) p->cur->stopping = true;
    PreviewTrack *stale = p->next;
    p->next = NULL;
    SDL_UnlockAudioDevice(p->dev);
    track_free(stale);
}

static void open_device(Preview *p) {
    SDL_AudioSpec want = {0}, have = {0};
    want.freq = PREVIEW_FREQ;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = preview_callback;
    want.userdata = p;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) { fprintf(stderr, "preview: pas de périphérique audio (%s)\n", SDL_GetError()); return; }
    SDL_LockMutex(p->lock);
    p->dev = dev;
    SDL_UnlockMutex(p->lock);
    if (p->sfx) sfx_attach(p->sfx, dev);
    SDL_PauseAudioDevice(dev, 0);
}

void preview_open(Preview *p) {
    if (p->lock && !p->dev) open_device(p);
}

void preview_update(Preview *p, float dt) {
    if (!p->thread) return;
    p->settle += dt;
    if (p->want_path[0] && p->settle >= SETTLE_S && strcmp(p->sent_path, p->want_path) != 0) {
        snprintf(p->sent_path, sizeof p->sent_path, "%s", p->want_path);
        if (!p->dev) open_device(p);
        if (!p->dev) return;   /* pas de son : on ne réessaie qu'à la prochaine chanson */
        SDL_LockMutex(p->lock);
        snprintf(p->load_path, sizeof p->load_path, "%s", p->want_path);
        p->load_ms = p->want_ms;
        p->gen++;
        SDL_CondSignal(p->cond);
        SDL_UnlockMutex(p->lock);
    }
    if (p->dev) {
        SDL_LockAudioDevice(p->dev);
        PreviewTrack *done = p->retired;
        p->retired = NULL;
        SDL_UnlockAudioDevice(p->dev);
        track_free(done);
    }
}
