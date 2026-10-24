#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "third_party/stb_vorbis.c"

#define CLICK_MS       40
#define CLICK_FREQ_HZ  1200
#define CLICK_AMP      6000
#define DEFAULT_FREQ   44100

static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len) {
    Audio *a = (Audio *)userdata;
    Sint16 *out = (Sint16 *)stream;
    int frames = len / (int)(2 * sizeof(Sint16));
    memset(stream, 0, (size_t)len);

    if (a->paused) {   /* musique et horloge figées ; les bruitages (pause, compte à rebours) restent audibles */
        if (a->sfx) sfx_mix(a->sfx, out, frames);
        return;
    }

    if (a->music) {
        int silent = 0; /* lead-in : silence avant t=0 */
        if (a->samples_played < 0)
            silent = (int)(-a->samples_played < frames ? -a->samples_played : frames);
        int want = frames - silent;
        if (want > 0)
            stb_vorbis_get_samples_short_interleaved(a->music, 2, out + silent * 2, want * 2);
        if (a->fade_start >= 0) {   /* fondu de sortie : gain linéaire jusqu'au silence */
            for (int i = 0; i < frames; i++) {
                Sint64 k = a->samples_played + i - a->fade_start;
                int gain = k <= 0 ? 256 : k >= a->fade_frames ? 0 : (int)(256 - k * 256 / a->fade_frames);
                out[i * 2] = (Sint16)(out[i * 2] * gain / 256);
                out[i * 2 + 1] = (Sint16)(out[i * 2 + 1] * gain / 256);
            }
        }
    } else if (a->beat_samples > 0) {
        int click_len = a->freq * CLICK_MS / 1000;
        int half_period = a->freq / (2 * CLICK_FREQ_HZ);
        if (half_period < 1) half_period = 1;
        for (int i = 0; i < frames; i++) {
            Sint64 pos = a->samples_played + i - a->offset_samples;
            if (pos < 0) continue;
            int p = (int)(pos % a->beat_samples);
            if (p >= click_len) continue;
            int sign = ((p / half_period) & 1) ? -1 : 1;
            Sint16 v = (Sint16)(sign * CLICK_AMP * (click_len - p) / click_len);
            out[i * 2] = v;
            out[i * 2 + 1] = v;
        }
    }
    a->samples_played += frames;
    if (a->sfx) sfx_mix(a->sfx, out, frames);
}

static bool load_music(Audio *a, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "audio: piste introuvable '%s'\n", path); return false; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    a->music_data = malloc((size_t)size);
    if (!a->music_data || fread(a->music_data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(a->music_data);
        a->music_data = NULL;
        return false;
    }
    fclose(f);

    int err = 0;
    a->music = stb_vorbis_open_memory(a->music_data, (int)size, &err, NULL);
    if (!a->music) {
        fprintf(stderr, "audio: OGG illisible '%s' (erreur %d)\n", path, err);
        free(a->music_data);
        a->music_data = NULL;
        return false;
    }
    stb_vorbis_info info = stb_vorbis_get_info(a->music);
    a->freq = (int)info.sample_rate;
    fprintf(stderr, "audio: piste '%s' (%d Hz, %d canaux, %.1f s)\n", path,
            a->freq, info.channels, stb_vorbis_stream_length_in_seconds(a->music));
    return true;
}

void audio_init(Audio *a, const char *music_path) {
    *a = (Audio){0};
    a->fade_start = -1;
    a->freq = DEFAULT_FREQ;
    if (music_path && music_path[0]) load_music(a, music_path);

    SDL_AudioSpec want = {0}, have = {0};
    want.freq = a->freq;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = audio_callback;
    want.userdata = a;

    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!a->dev) {
        fprintf(stderr, "audio: pas de périphérique audio (%s), jeu sans son\n", SDL_GetError());
        if (a->music) { stb_vorbis_close(a->music); a->music = NULL; }
        free(a->music_data);
        a->music_data = NULL;
        return;
    }
    a->paused = true;   /* démarre en pause logicielle : audio_start le lance */
    SDL_PauseAudioDevice(a->dev, 0);
}

void audio_attach_sfx(Audio *a, Sfx *sfx) {
    if (!a->dev) return;
    SDL_LockAudioDevice(a->dev);
    a->sfx = sfx;
    SDL_UnlockAudioDevice(a->dev);
    sfx_attach(sfx, a->dev);
}

void audio_shutdown(Audio *a) {
    if (a->dev) SDL_CloseAudioDevice(a->dev);
    a->dev = 0;
    if (a->sfx) sfx_detach(a->sfx);
    a->sfx = NULL;
    if (a->music) stb_vorbis_close(a->music);
    a->music = NULL;
    free(a->music_data);
    a->music_data = NULL;
}

void audio_start(Audio *a, float bpm, int offset_ms, int lead_in_ms) {
    if (!a->dev) return;
    SDL_LockAudioDevice(a->dev);
    a->samples_played = -(Sint64)a->freq * lead_in_ms / 1000;
    if (bpm > 0.0f) a->beat_samples = (int)((double)a->freq * 60.0 / (double)bpm);
    a->chart_offset_ms = offset_ms;
    a->offset_samples = (Sint64)a->freq * (offset_ms + a->calib_ms) / 1000;
    if (a->music) stb_vorbis_seek_start(a->music);
    a->fade_start = -1;   /* nouvelle partie (recommencer) : plus de fondu en cours */
    a->paused = false;
    SDL_UnlockAudioDevice(a->dev);
}

void audio_set_paused(Audio *a, bool paused) {
    if (!a->dev) return;
    SDL_LockAudioDevice(a->dev);
    a->paused = paused;
    SDL_UnlockAudioDevice(a->dev);
}

void audio_adjust_calibration(Audio *a, int delta_ms) {
    if (!a->dev) return;
    SDL_LockAudioDevice(a->dev);
    a->calib_ms += delta_ms;
    a->offset_samples = (Sint64)a->freq * (a->chart_offset_ms + a->calib_ms) / 1000;
    SDL_UnlockAudioDevice(a->dev);
    fprintf(stderr, "audio: calibration = %d ms\n", a->calib_ms);
}

void audio_set_calibration(Audio *a, int ms) {
    if (!a->dev) return;
    audio_adjust_calibration(a, ms - a->calib_ms);
}

bool audio_has_music(const Audio *a) { return a->music != NULL; }

float audio_song_time_ms(Audio *a) {
    if (!a->dev) return 0.0f;
    SDL_LockAudioDevice(a->dev);
    Sint64 played = a->samples_played;
    SDL_UnlockAudioDevice(a->dev);
    return (float)played * 1000.0f / (float)a->freq + (float)a->calib_ms;
}

void audio_fade_out(Audio *a, int ms) {
    if (!a->dev) return;
    SDL_LockAudioDevice(a->dev);
    if (a->fade_start < 0) {
        a->fade_start = a->samples_played;
        a->fade_frames = a->freq * ms / 1000 > 0 ? a->freq * ms / 1000 : 1;
    }
    SDL_UnlockAudioDevice(a->dev);
}
