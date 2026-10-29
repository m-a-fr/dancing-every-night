#include "sfx.h"
#include "config.h"
#include "files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"

#define SFX_FREQ        44100
#define BGM_FADE_S      0.5f
#define BGM_BLOCK       512     /* frames décodées par tour dans le callback */

static const char *const SFX_NAMES[SFX_COUNT] = {
#define X(id, name) name,
    SFX_LIST(X)
#undef X
};

static inline Sint16 clamp16(int v) { return (Sint16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* Ajoute la musique de fond et les bruitages aux `frames` frames stéréo de `out` (déjà remplies par l'appelant).
   Appelé par le callback du périphérique qui est ouvert (aperçu du menu ou audio de jeu), donc avec son verrou. */
void sfx_mix(Sfx *s, Sint16 *out, int frames) {
    /* Musique de fond : décodée en continu, boucle au début du fichier. Gain en fondu vers la cible. */
    if (s->bgm && (s->bgm_gain > 0.0f || s->bgm_target > 0.0f)) {
        Sint16 buf[BGM_BLOCK * 2];
        const float step = 1.0f / ((float)s->freq * BGM_FADE_S);
        for (int done = 0; done < frames;) {
            int want = frames - done < BGM_BLOCK ? frames - done : BGM_BLOCK;
            int got = stb_vorbis_get_samples_short_interleaved(s->bgm, 2, buf, want * 2);
            if (got == 0) {   /* fin du fichier : on reprend au début */
                stb_vorbis_seek_start(s->bgm);
                got = stb_vorbis_get_samples_short_interleaved(s->bgm, 2, buf, want * 2);
                if (got == 0) break;
            }
            for (int i = 0; i < got; i++) {
                if (s->bgm_gain < s->bgm_target) { s->bgm_gain += step; if (s->bgm_gain > s->bgm_target) s->bgm_gain = s->bgm_target; }
                else if (s->bgm_gain > s->bgm_target) { s->bgm_gain -= step; if (s->bgm_gain < s->bgm_target) s->bgm_gain = s->bgm_target; }
                float g = s->bgm_gain * s->bgm_vol;
                out[(done + i) * 2]     = clamp16(out[(done + i) * 2]     + (int)(buf[i * 2] * g));
                out[(done + i) * 2 + 1] = clamp16(out[(done + i) * 2 + 1] + (int)(buf[i * 2 + 1] * g));
            }
            done += got;
        }
    }

    /* Bruitages : somme des voix en cours, avec écrêtage */
    for (int v = 0; v < SFX_VOICES; v++) {
        SfxVoice *vc = &s->voices[v];
        if (vc->id < 0) continue;
        const Sint16 *pcm = s->pcm[vc->id];
        int n = s->len[vc->id] - vc->pos;
        if (n > frames) n = frames;
        for (int i = 0; i < n; i++) {
            int m = (int)(pcm[vc->pos + i] * s->sfx_vol);
            out[i * 2]     = clamp16(out[i * 2] + m);
            out[i * 2 + 1] = clamp16(out[i * 2 + 1] + m);
        }
        vc->pos += n;
        if (vc->pos >= s->len[vc->id]) vc->id = -1;
    }
}

void sfx_init(Sfx *s) {
    *s = (Sfx){0};
    s->freq = SFX_FREQ;
    s->sfx_vol = 1.0f;
    s->bgm_vol = 0.6f;
    s->bgm_target = 1.0f;
    for (int v = 0; v < SFX_VOICES; v++) s->voices[v].id = -1;
    for (int i = 0; i < SFX_COUNT; i++) {
        char rel[64], path[192];
        snprintf(rel, sizeof rel, "audio/sfx/%s.wav", SFX_NAMES[i]);
        asset_resolve(rel, path, sizeof path);   /* custom/ du joueur d'abord */
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint32 bytes = 0;
        if (!SDL_LoadWAV(path, &spec, &buf, &bytes)) continue;   /* absent : bruitage muet */
        if (spec.format != AUDIO_S16LSB || spec.channels != 1 || spec.freq != SFX_FREQ) {
            fprintf(stderr, "sfx: '%s' ignoré (attendu 16 bits mono %d Hz)\n", path, SFX_FREQ);
            SDL_FreeWAV(buf);
            continue;
        }
        s->pcm[i] = malloc(bytes);
        if (s->pcm[i]) { memcpy(s->pcm[i], buf, bytes); s->len[i] = (int)(bytes / sizeof(Sint16)); }
        SDL_FreeWAV(buf);
    }
}

static void bgm_free(Sfx *s) {
    if (s->bgm) stb_vorbis_close(s->bgm);
    s->bgm = NULL;
    free(s->bgm_data);
    s->bgm_data = NULL;
}

void sfx_shutdown(Sfx *s) {
    sfx_detach(s);
    bgm_free(s);
    for (int i = 0; i < SFX_COUNT; i++) { free(s->pcm[i]); s->pcm[i] = NULL; }
}

void sfx_attach(Sfx *s, SDL_AudioDeviceID dev) {
    s->dev = dev;
}

void sfx_detach(Sfx *s) {
    if (!s->dev) return;
    s->dev = 0;
    for (int v = 0; v < SFX_VOICES; v++) s->voices[v].id = -1;
    s->bgm_gain = 0.0f;             /* la musique reprendra en fondu */
    if (s->bgm) stb_vorbis_seek_start(s->bgm);
}

void sfx_play(Sfx *s, SfxId id) {
    if (!s->dev || id < 0 || id >= SFX_COUNT || !s->pcm[id]) return;
    SDL_LockAudioDevice(s->dev);
    int slot = -1;
    for (int v = 0; v < SFX_VOICES && slot < 0; v++) if (s->voices[v].id < 0) slot = v;
    if (slot < 0) {   /* toutes occupées : on reprend la plus avancée */
        int best = 0;
        for (int v = 1; v < SFX_VOICES; v++) if (s->voices[v].pos > s->voices[best].pos) best = v;
        slot = best;
    }
    s->voices[slot] = (SfxVoice){ .id = (int)id, .pos = 0 };
    SDL_UnlockAudioDevice(s->dev);
}

void sfx_bgm_load(Sfx *s, const char *path) {
    if (!path) path = "";
    if (strcmp(path, s->bgm_path) == 0) return;
    if (s->dev) SDL_LockAudioDevice(s->dev);
    bgm_free(s);
    snprintf(s->bgm_path, sizeof s->bgm_path, "%s", path);
    if (path[0]) {
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            s->bgm_data = malloc((size_t)size);
            if (s->bgm_data && fread(s->bgm_data, 1, (size_t)size, f) == (size_t)size) {
                int err = 0;
                s->bgm = stb_vorbis_open_memory(s->bgm_data, (int)size, &err, NULL);
                if (s->bgm && (int)stb_vorbis_get_info(s->bgm).sample_rate != s->freq) {
                    fprintf(stderr, "sfx: '%s' n'est pas en %d Hz, pas de musique\n", path, s->freq);
                    bgm_free(s);
                }
            } else {
                free(s->bgm_data);
                s->bgm_data = NULL;
            }
            fclose(f);
        } else {
            fprintf(stderr, "sfx: musique introuvable '%s'\n", path);
        }
    }
    s->bgm_gain = 0.0f;
    if (s->dev) SDL_UnlockAudioDevice(s->dev);
}

void sfx_bgm_target(Sfx *s, float target) {
    if (s->dev) SDL_LockAudioDevice(s->dev);
    s->bgm_target = target < 0.0f ? 0.0f : target > 1.0f ? 1.0f : target;
    if (s->dev) SDL_UnlockAudioDevice(s->dev);
}
