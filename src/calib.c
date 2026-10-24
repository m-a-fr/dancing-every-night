#include "calib.h"
#include "config.h"
#include "settings.h"
#include "shapes.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SCALE_MS     200.0f    /* l'échelle des frappes va de -SCALE_MS (en avance) à +SCALE_MS (en retard) */
#define SCALE_X0     180.0f
#define SCALE_X1     780.0f
#define SCALE_Y      372.0f

void calib_restart(Calib *c) {
    c->phase = CALIB_RUNNING;
    c->beat_ms = 60000.0f / CALIB_BPM;
    c->n = 0;
    c->last_beat = -1;
    c->median_ms = 0.0f;
    c->result_ms = 0;
    c->tap_flash = 10.0f;
    c->time_ms = -(float)CALIB_LEAD_IN_MS;
    audio_set_calibration(&c->audio, 0);   /* horloge brute : c'est elle qu'on mesure */
    audio_start(&c->audio, CALIB_BPM, 0, CALIB_LEAD_IN_MS);
}

void calib_start(Calib *c, Sfx *sfx) {
    *c = (Calib){0};
    audio_init(&c->audio, NULL);   /* pas de piste : métronome, un clic par temps calé à l'échantillon */
    if (sfx) audio_attach_sfx(&c->audio, sfx);
    calib_restart(c);
}

void calib_stop(Calib *c) {
    audio_shutdown(&c->audio);
}

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float median(const float *v, int n) {
    float s[CALIB_TAPS];
    for (int i = 0; i < n; i++) s[i] = v[i];
    qsort(s, (size_t)n, sizeof s[0], cmp_float);
    return n % 2 ? s[n / 2] : 0.5f * (s[n / 2 - 1] + s[n / 2]);
}

static void finish(Calib *c) {
    audio_set_paused(&c->audio, true);   /* plus de clic ; les bruitages du menu restent audibles */
    if (c->n < CALIB_MIN_TAPS) { c->phase = CALIB_FAILED; return; }
    c->median_ms = median(c->err, c->n);
    c->result_ms = settings_clamp_offset(-(int)lroundf(c->median_ms));
    c->phase = CALIB_RESULT;
}

void calib_update(Calib *c, const Input *in, float dt) {
    c->tap_flash += dt;
    if (c->phase != CALIB_RUNNING) return;
    if (c->audio.dev) c->time_ms = audio_song_time_ms(&c->audio);
    else              c->time_ms += dt * 1000.0f;   /* sans son : la mesure n'a pas de sens, mais l'écran avance */

    if (in->pressed[BTN_CROSS] && c->time_ms > -c->beat_ms * 0.5f) {
        int beat = (int)lroundf(c->time_ms / c->beat_ms);
        float err = c->time_ms - (float)beat * c->beat_ms;
        bool measured = beat >= CALIB_COUNT_IN && beat < CALIB_COUNT_IN + CALIB_TAPS;
        if (measured && beat != c->last_beat && fabsf(err) < c->beat_ms * 0.4f && c->n < CALIB_TAPS) {
            c->err[c->n++] = err;
            c->last_beat = beat;
            c->tap_flash = 0.0f;
        }
    }
    if (c->time_ms > ((float)(CALIB_COUNT_IN + CALIB_TAPS) - 0.5f) * c->beat_ms) finish(c);
}

/* ---------------------------------------------------------------- Dessin */

static float scale_x(float ms) {
    if (ms < -SCALE_MS) ms = -SCALE_MS;
    if (ms > SCALE_MS) ms = SCALE_MS;
    return SCALE_X0 + (ms + SCALE_MS) / (2.0f * SCALE_MS) * (SCALE_X1 - SCALE_X0);
}

static void label(const Font *f, SDL_Renderer *r, FontFace face, float x, float cy, TextAlign al, SDL_Color c,
                  const char *text) {
    font_draw(f, r, face, x, cy - font_cap_height(f, face) * 0.5f, al, c, text);
}

/* Échelle des frappes : EARLY à gauche, LATE à droite, 0 au milieu ; un trait par frappe, la dernière en citron */
static void draw_scale(const Calib *c, SDL_Renderer *r, const Font *font) {
    ui_round_rect_rot(r, (SDL_FRect){SCALE_X0 - 24.0f, SCALE_Y - 34.0f, SCALE_X1 - SCALE_X0 + 48.0f, 68.0f}, 18.0f,
                      (FontRot){0}, ui_alpha(CH_INK, 220));
    shapes_thick_line(r, SCALE_X0, SCALE_Y, SCALE_X1, SCALE_Y, 2.0f, ui_alpha(CH_WHITE, 90));
    shapes_thick_line(r, scale_x(0.0f), SCALE_Y - 22.0f, scale_x(0.0f), SCALE_Y + 22.0f, 2.0f, ui_alpha(CH_WHITE, 160));
    for (int ms = -150; ms <= 150; ms += 50)
        if (ms) shapes_thick_line(r, scale_x((float)ms), SCALE_Y - 6.0f, scale_x((float)ms), SCALE_Y + 6.0f, 2.0f,
                                  ui_alpha(CH_WHITE, 90));
    label(font, r, FONT_LABEL, SCALE_X0, SCALE_Y + 52.0f, ALIGN_LEFT, ui_alpha(CH_WHITE, 178), "EARLY");
    label(font, r, FONT_LABEL, scale_x(0.0f), SCALE_Y + 52.0f, ALIGN_CENTER, ui_alpha(CH_WHITE, 178), "0");
    label(font, r, FONT_LABEL, SCALE_X1, SCALE_Y + 52.0f, ALIGN_RIGHT, ui_alpha(CH_WHITE, 178), "LATE");
    for (int i = 0; i < c->n; i++) {
        bool last = (i == c->n - 1) && c->phase == CALIB_RUNNING;
        float x = scale_x(c->err[i]), h = last ? 26.0f : 18.0f;
        shapes_thick_line(r, x, SCALE_Y - h, x, SCALE_Y + h, last ? 5.0f : 3.0f, last ? CH_GOLD : ui_alpha(CH_PINK, 220));
    }
    if (c->phase == CALIB_RESULT) {   /* médiane : le retard mesuré */
        float x = scale_x(c->median_ms);
        shapes_thick_line(r, x, SCALE_Y - 30.0f, x, SCALE_Y + 30.0f, 5.0f, CH_CYAN);
    }
}

void calib_draw(const Calib *c, SDL_Renderer *r, const Font *font, const Sprites *sprites, int current_ms) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, 255);
    SDL_RenderClear(r);

    const float hsc = 64.0f / 66.0f, hw = (float)font_width(font, FONT_POP_XL, "CALIBRATION") * hsc;
    ui_pop_text(r, font, FONT_POP_XL, 40.0f + hw * 0.5f, 81.0f, hsc, 0.0f, -4.0f, 0.0f, CH_GOLD, CH_INK, CH_PINK, 5.0f, 5.0f,
                "CALIBRATION");
    label(font, r, FONT_CARD, 46.0f, 139.0f, ALIGN_LEFT, ui_alpha(CH_WHITE, 204),
          "TAP ON EACH CLICK  \xC2\xB7  LISTEN, DON'T WATCH");

    char buf[48];
    const float cx = SCREEN_W * 0.5f, cy = 250.0f;
    if (c->phase == CALIB_RUNNING) {
        int beat = (int)floorf(c->time_ms / c->beat_ms + 0.5f);
        if (beat < CALIB_COUNT_IN) {   /* décompte (non mesuré) : seul moment où l'écran suit le temps */
            if (beat < 0) snprintf(buf, sizeof buf, "READY");
            else          snprintf(buf, sizeof buf, "%d", CALIB_COUNT_IN - beat);
            ui_pop_text(r, font, FONT_POP_XL, cx, cy, 1.0f, 2.0f, 0.0f, 0.0f, CH_WHITE, CH_INK, CH_PINK, 5.0f, 5.0f, buf);
        } else {
            snprintf(buf, sizeof buf, "%d / %d", c->n, CALIB_TAPS);
            float pop = c->tap_flash < 0.15f ? 1.0f - c->tap_flash / 0.15f : 0.0f;
            ui_pop_text(r, font, FONT_POP_XL, cx, cy, 1.0f + 0.08f * pop, 2.0f, 0.0f, 0.0f, CH_GOLD, CH_INK, CH_PINK, 5.0f,
                        5.0f, buf);
        }
        draw_scale(c, r, font);
    } else if (c->phase == CALIB_RESULT) {
        label(font, r, FONT_LABEL, cx, 186.0f, ALIGN_CENTER, ui_alpha(CH_WHITE, 178), "SUGGESTED AUDIO OFFSET");
        snprintf(buf, sizeof buf, "%+d MS", c->result_ms);
        ui_pop_text(r, font, FONT_POP_XL, cx, cy, 1.0f, 2.0f, 0.0f, 0.0f, CH_GOLD, CH_INK, CH_PINK, 5.0f, 5.0f, buf);
        snprintf(buf, sizeof buf, "CURRENT %+d MS  \xC2\xB7  %d TAPS", current_ms, c->n);
        label(font, r, FONT_CARD, cx, 314.0f, ALIGN_CENTER, ui_alpha(CH_WHITE, 204), buf);
        draw_scale(c, r, font);
    } else {
        ui_pop_text(r, font, FONT_POP_XL, cx, cy, 0.8f, 2.0f, 0.0f, 0.0f, CH_WHITE, CH_INK, CH_PINK, 5.0f, 5.0f,
                    "NOT ENOUGH TAPS");
        snprintf(buf, sizeof buf, "%d / %d TAPS ON TIME  \xC2\xB7  TRY AGAIN", c->n, CALIB_MIN_TAPS);
        label(font, r, FONT_CARD, cx, 310.0f, ALIGN_CENTER, ui_alpha(CH_WHITE, 204), buf);
    }

    const float hy = SCREEN_H - 26.0f;
    float x = 34.0f;
    if (c->phase == CALIB_RUNNING) {
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CROSS}, 1, "TAP") + 18.0f;
    } else {
        if (c->phase == CALIB_RESULT)
            x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CROSS}, 1, "APPLY") + 18.0f;
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_TRIANGLE}, 1, "RETRY") + 18.0f;
    }
    ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CIRCLE}, 1, "CANCEL");
}
