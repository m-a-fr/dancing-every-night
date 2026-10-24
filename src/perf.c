#define PERF_NO_WRAP   /* ce fichier appelle les vraies fonctions SDL */
#include "perf.h"
#include "perf_wrap.h"
#include <stdio.h>
#include <stdlib.h>

/* Compteurs de l'instrumentation : globaux au fichier, car les appels SDL comptés viennent de tout le code
   de rendu (ce n'est pas de l'état de jeu). */
static int  frame_draws;
static long frame_verts;
static long texture_bytes;

#define HOLD_TOGGLE_S 1.0f

void perf_init(Perf *p) {
    *p = (Perf){0};
#ifndef __vita__
    const char *e = getenv("VITAGAME_PERF");
    if (e && *e && *e != '0') p->visible = p->print = true;
#endif
}

void perf_frame_begin(void) { frame_draws = 0; frame_verts = 0; }

int  perf_draw_calls(void) { return frame_draws; }
long perf_texture_bytes(void) { return texture_bytes; }

void perf_frame_end(Perf *p, float dt, float cpu_ms, float present_ms) {
    p->frames++;
    p->acc_s += dt;
    p->cpu_sum_ms += cpu_ms;
    p->present_sum_ms += present_ms;
    if (cpu_ms > p->cpu_max_ms) p->cpu_max_ms = cpu_ms;
    p->draws_sum += frame_draws;
    p->verts_sum += frame_verts;
    if (frame_draws > p->draws_max) p->draws_max = frame_draws;
    if (p->acc_s < 1.0f) return;
    p->fps = p->frames / p->acc_s;
    p->cpu_avg_ms = p->cpu_sum_ms / p->frames;
    p->cpu_peak_ms = p->cpu_max_ms;
    p->present_avg_ms = p->present_sum_ms / p->frames;
    p->draws_avg = (int)(p->draws_sum / p->frames);
    p->draws_peak = p->draws_max;
    p->verts_avg = (int)(p->verts_sum / p->frames);
    if (p->print)
        printf("perf fps=%.1f cpu=%.2fms (max %.2f) present=%.2fms draws=%d (max %d) verts=%d tex=%.1fMB\n",
               p->fps, p->cpu_avg_ms, p->cpu_peak_ms, p->present_avg_ms, p->draws_avg, p->draws_peak,
               p->verts_avg, texture_bytes / 1048576.0);
    p->acc_s = 0; p->frames = 0;
    p->cpu_sum_ms = p->cpu_max_ms = p->present_sum_ms = 0;
    p->draws_sum = p->verts_sum = 0; p->draws_max = 0;
}

void perf_toggle_input(Perf *p, bool l_held, bool r_held, float dt) {
    if (!(l_held && r_held)) { p->hold_s = 0; return; }
    float before = p->hold_s;
    p->hold_s += dt;
    if (before < HOLD_TOGGLE_S && p->hold_s >= HOLD_TOGGLE_S) p->visible = !p->visible;
}

void perf_draw(const Perf *p, SDL_Renderer *r, const Font *font) {
    if (!p->visible) return;
    char line[3][96];
    snprintf(line[0], sizeof line[0], "%.0f FPS   CPU %.1f MS (MAX %.1f)   PRESENT %.1f MS",
             p->fps, p->cpu_avg_ms, p->cpu_peak_ms, p->present_avg_ms);
    snprintf(line[1], sizeof line[1], "DRAWS %d (MAX %d)   VERTS %d", p->draws_avg, p->draws_peak, p->verts_avg);
    snprintf(line[2], sizeof line[2], "TEXTURES %.1f MB", texture_bytes / 1048576.0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 190);
    SDL_Rect bg = { 6, 6, 430, 74 };
    SDL_RenderFillRect(r, &bg);
    SDL_Color c = { 160, 255, 200, 255 };
    for (int i = 0; i < 3; i++) font_draw(font, r, FONT_SMALL, 14, 14 + i * 22, ALIGN_LEFT, c, line[i]);
}

/* --- remplaçants comptés --- */

int perf_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d) {
    frame_draws++; frame_verts += 4;
    return SDL_RenderCopy(r, t, s, d);
}

int perf_RenderCopyF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_FRect *d) {
    frame_draws++; frame_verts += 4;
    return SDL_RenderCopyF(r, t, s, d);
}

int perf_RenderCopyExF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_FRect *d,
                       double angle, const SDL_FPoint *c, SDL_RendererFlip flip) {
    frame_draws++; frame_verts += 4;
    return SDL_RenderCopyExF(r, t, s, d, angle, c, flip);
}

int perf_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int nv, const int *idx, int ni) {
    frame_draws++; frame_verts += idx ? ni : nv;
    return SDL_RenderGeometry(r, t, v, nv, idx, ni);
}

int perf_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect) {
    frame_draws++; frame_verts += 4;
    return SDL_RenderFillRect(r, rect);
}

int perf_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2) {
    frame_draws++; frame_verts += 2;
    return SDL_RenderDrawLine(r, x1, y1, x2, y2);
}

static long texture_size(SDL_Texture *t) {
    Uint32 fmt; int w, h;
    if (!t || SDL_QueryTexture(t, &fmt, NULL, &w, &h) != 0) return 0;
    if (fmt == SDL_PIXELFORMAT_IYUV || fmt == SDL_PIXELFORMAT_YV12) return (long)w * h * 3 / 2;
    return (long)w * h * SDL_BYTESPERPIXEL(fmt);
}

SDL_Texture *perf_CreateTexture(SDL_Renderer *r, Uint32 fmt, int access, int w, int h) {
    SDL_Texture *t = SDL_CreateTexture(r, fmt, access, w, h);
    texture_bytes += texture_size(t);
    return t;
}

SDL_Texture *perf_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s) {
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    texture_bytes += texture_size(t);
    return t;
}

void perf_DestroyTexture(SDL_Texture *t) {
    texture_bytes -= texture_size(t);
    SDL_DestroyTexture(t);
}
