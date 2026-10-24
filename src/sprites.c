#include "sprites.h"
#include "config.h"
#include <stdio.h>
#include <math.h>
#include "third_party/stb_image.h"

/* Tailles de cellule des atlas (voir tools/svg2atlas.py). Réduire une image de plus de 2x en bilinéaire
   la crénelle : on choisit la plus petite cellule au moins aussi grande que l'affichage (comme un mipmap). */
static const int CELLS[3] = { 128, 64, 32 };
static const char *const ATLAS_FILES[3] = {
    ASSET_PATH("ui/pad_icons.png"), ASSET_PATH("ui/pad_icons_64.png"), ASSET_PATH("ui/pad_icons_32.png")
};

/* Liseré des cibles : anneau net + halo, en px écran. Le disque d'une cible fait ~26 px de rayon. */
#define RING_TEX        128
#define RING_DRAW_SIZE 116.0f   /* anneau + halo : 2 x (TARGET_RING_RADIUS + ~3 x NEON_GLOW_WIDTH) */

static SDL_Texture *make_ring_texture(SDL_Renderer *r, NeonColor color);
static SDL_Texture *make_disc_texture(SDL_Renderer *r, float inner_radius, float outer_radius);

/* Textures de disque des notes : 1 texel = 1 px écran à la taille finale (agrandies/réduites peu) ; assez
   grandes pour le contour posé autour du disque */
#define NOTE_TEX 72

static int lane_column(Button lane) {
    switch (lane) {
    case BTN_TRIANGLE: return 0;
    case BTN_CIRCLE:   return 1;
    case BTN_CROSS:    return 2;
    case BTN_UP:       return 3;
    case BTN_LEFT:     return 4;
    case BTN_DOWN:     return 5;
    default:           return -1;
    }
}

static SDL_Texture *load_atlas(SDL_Renderer *r, const char *path) {
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) { fprintf(stderr, "sprites: impossible de charger '%s'\n", path); return NULL; }
    SDL_Texture *t = NULL;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (surf) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   /* bilinéaire */
        t = SDL_CreateTextureFromSurface(r, surf);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_FreeSurface(surf);
    }
    stbi_image_free(px);
    if (!t) { fprintf(stderr, "sprites: texture: %s\n", SDL_GetError()); return NULL; }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

/* Atlas à utiliser pour une cellule affichée à `display` px ; *cell = taille de sa cellule */
static SDL_Texture *atlas_for(const Sprites *s, float display, int *cell) {
    for (int i = 2; i >= 0; i--)
        if (s->atlas[i] && (CELLS[i] >= display || i == 0)) { *cell = CELLS[i]; return s->atlas[i]; }
    return NULL;
}

bool sprites_init(Sprites *s, SDL_Renderer *r) {
    *s = (Sprites){0};
    for (int i = 0; i < 3; i++) s->atlas[i] = load_atlas(r, ATLAS_FILES[i]);
    if (!s->atlas[0]) return false;   /* les petites tailles sont facultatives */
    for (int i = 0; i < NEON_COLOR_COUNT; i++)
        s->ring[i] = make_ring_texture(r, (NeonColor)i);   /* facultatif : sans lui, pas de liseré */
    s->note_disc = make_disc_texture(r, 0.0f, NOTE_DISC_RADIUS);
    s->note_border = make_disc_texture(r, NOTE_DISC_RADIUS, NOTE_DISC_RADIUS + NOTE_BORDER_WIDTH);
    return true;
}

/* Texture de l'anneau néon (profil NEON_* de sprites.h, identique aux arcs de rhythm.c) */
SDL_Color neon_glow_color(NeonColor n) { return n == NEON_MAGENTA ? NEON_MAGENTA_GLOW_COLOR : NEON_GLOW_COLOR; }
SDL_Color neon_core_color(NeonColor n) { return n == NEON_MAGENTA ? NEON_MAGENTA_CORE_COLOR : NEON_CORE_COLOR; }

NeonColor neon_for_lane(Button lane) {
    return (lane == BTN_TRIANGLE || lane == BTN_CIRCLE || lane == BTN_CROSS) ? NEON_MAGENTA : NEON_TEAL;
}

static SDL_Texture *make_ring_texture(SDL_Renderer *r, NeonColor color) {
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, RING_TEX, RING_TEX, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return NULL;
    const float px = RING_DRAW_SIZE / RING_TEX;   /* taille d'un texel en px écran */
    const SDL_Color gc = neon_glow_color(color), cc = neon_core_color(color);
    for (int y = 0; y < RING_TEX; y++) {
        Uint8 *row = (Uint8 *)surf->pixels + y * surf->pitch;
        for (int x = 0; x < RING_TEX; x++) {
            float dx = (x + 0.5f - RING_TEX / 2.0f) * px, dy = (y + 0.5f - RING_TEX / 2.0f) * px;
            float dist = fabsf(sqrtf(dx * dx + dy * dy) - TARGET_RING_RADIUS);
            float core = NEON_CORE_HALF_WIDTH + 0.5f - dist;     /* bord adouci sur ~1 px */
            core = core < 0.0f ? 0.0f : core > 1.0f ? 1.0f : core;
            float g = dist / NEON_GLOW_WIDTH;
            float glow = NEON_GLOW_PEAK * expf(-g * g);
            float a = core > glow ? core : glow;
            Uint8 *p = row + x * 4;
            p[0] = (Uint8)(gc.r + (cc.r - gc.r) * core);
            p[1] = (Uint8)(gc.g + (cc.g - gc.g) * core);
            p[2] = (Uint8)(gc.b + (cc.b - gc.b) * core);
            p[3] = (Uint8)(a * 255.0f + 0.5f);
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_FreeSurface(surf);
    return t;
}

void sprites_shutdown(Sprites *s) {
    for (int i = 0; i < 3; i++) {
        if (s->atlas[i]) SDL_DestroyTexture(s->atlas[i]);
        s->atlas[i] = NULL;
    }
    for (int i = 0; i < NEON_COLOR_COUNT; i++) {
        if (s->ring[i]) SDL_DestroyTexture(s->ring[i]);
        s->ring[i] = NULL;
    }
    if (s->note_disc) SDL_DestroyTexture(s->note_disc);
    if (s->note_border) SDL_DestroyTexture(s->note_border);
    s->note_disc = s->note_border = NULL;
    for (int i = 0; i < 2; i++) {
        if (s->arc[i]) SDL_DestroyTexture(s->arc[i]);
        s->arc[i] = NULL;
    }
}

void sprite_draw_target_ring(const Sprites *s, SDL_Renderer *r, NeonColor color, float cx, float cy, float boost) {
    SDL_Texture *ring = s->ring[color];
    if (!ring) return;
    SDL_FRect dst = { cx - RING_DRAW_SIZE / 2.0f, cy - RING_DRAW_SIZE / 2.0f, RING_DRAW_SIZE, RING_DRAW_SIZE };
    SDL_SetTextureBlendMode(ring, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(ring, 255);
    SDL_RenderCopyF(r, ring, NULL, &dst);
    if (boost > 0.0f) {
        SDL_SetTextureBlendMode(ring, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(ring, (Uint8)(255.0f * (boost > 1.0f ? 1.0f : boost)));
        SDL_RenderCopyF(r, ring, NULL, &dst);
    }
}

static void draw_cell(const Sprites *s, SDL_Renderer *r, int row, Button lane, float cx, float cy, Uint8 alpha, float scale) {
    int col = lane_column(lane), cell;
    float size = SPRITE_DRAW_SIZE * scale;
    SDL_Texture *atlas = atlas_for(s, size, &cell);
    if (!atlas || col < 0) return;
    SDL_Rect src = { col * cell, row * cell, cell, cell };
    SDL_FRect dst = { cx - size / 2.0f, cy - size / 2.0f, size, size };
    SDL_SetTextureAlphaMod(atlas, alpha);
    SDL_RenderCopyF(r, atlas, &src, &dst);
}

void sprite_draw_target(const Sprites *s, SDL_Renderer *r, Button lane, float cx, float cy, Uint8 alpha) {
    draw_cell(s, r, 0, lane, cx, cy, alpha, 1.0f);
}

void sprite_draw_note_white(const Sprites *s, SDL_Renderer *r, Button lane, float cx, float cy, float scale, Uint8 alpha) {
    draw_cell(s, r, 2, lane, cx, cy, alpha, scale);
}

void sprite_draw_note(const Sprites *s, SDL_Renderer *r, Button lane, float cx, float cy, float scale, Uint8 alpha) {
    draw_cell(s, r, 1, lane, cx, cy, alpha, scale);
}

void sprite_draw_icon(const Sprites *s, SDL_Renderer *r, Button b, float cx, float cy, float size, Uint8 alpha) {
    bool flip = (b == BTN_RIGHT);
    int col = lane_column(flip ? BTN_LEFT : b), cell;
    /* le sprite occupe 24/32 de sa cellule (voir SPRITE_DRAW_SIZE) : on agrandit la cellule d'autant */
    float shown = size * 32.0f / 24.0f;
    SDL_Texture *atlas = atlas_for(s, shown, &cell);
    if (!atlas || col < 0) return;
    SDL_Rect src = { col * cell, 0, cell, cell };
    SDL_FRect dst = { cx - shown / 2.0f, cy - shown / 2.0f, shown, shown };
    SDL_SetTextureAlphaMod(atlas, alpha);
    SDL_RenderCopyExF(r, atlas, &src, &dst, 0.0, NULL, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

bool sprite_has_icon(Button b) {
    return b == BTN_RIGHT || lane_column(b) >= 0;
}

/* Anneau blanc entre inner_radius et outer_radius (inner_radius = 0 : disque plein), couverture
   calculée par sur-échantillonnage 4x4 : bords lissés. Teinté au dessin par SDL_SetTextureColorMod. */
static SDL_Texture *make_disc_texture(SDL_Renderer *r, float inner_radius, float outer_radius) {
    enum { SS = 4 };
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, NOTE_TEX, NOTE_TEX, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return NULL;
    const float c = NOTE_TEX / 2.0f, r2 = outer_radius * outer_radius, i2 = inner_radius * inner_radius;
    for (int y = 0; y < NOTE_TEX; y++) {
        Uint8 *row = (Uint8 *)surf->pixels + y * surf->pitch;
        for (int x = 0; x < NOTE_TEX; x++) {
            int inside = 0;
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++) {
                    float dx = x + (sx + 0.5f) / SS - c, dy = y + (sy + 0.5f) / SS - c;
                    float d2 = dx * dx + dy * dy;
                    if (d2 <= r2 && d2 >= i2) inside++;
                }
            Uint8 *p = row + x * 4;
            p[0] = p[1] = p[2] = 255;
            p[3] = (Uint8)(inside * 255 / (SS * SS));
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_FreeSurface(surf);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

static void draw_tinted(SDL_Renderer *r, SDL_Texture *t, float cx, float cy, float size, SDL_Color c) {
    if (!t || c.a == 0) return;
    float w = NOTE_TEX * size;
    SDL_FRect dst = { cx - w / 2.0f, cy - w / 2.0f, w, w };
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_RenderCopyF(r, t, NULL, &dst);
}

void sprite_draw_note_disc(const Sprites *s, SDL_Renderer *r, float cx, float cy, float size,
                           SDL_Color fill, SDL_Color border) {
    draw_tinted(r, s->note_disc, cx, cy, size, fill);
    draw_tinted(r, s->note_border, cx, cy, size, border);
}

/* --- arcs de l'écran de jeu --- */

#define ARC_INK_HALF    8.0f    /* trait d'encre de 16 px */
#define ARC_COLOR_HALF  4.5f    /* aplat de couleur de 9 px, centré dessus */
#define ARC_STEPS      180
#define ARC_EDGE_PX      1.5f   /* largeur du dégradé de bord (px) : les arcs sont presque verticaux, un bord à 1 px
                                   de large y fait des marches visibles ; 1,5 les efface sans flouter l'ensemble */

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float qx = ax + dx * t - px, qy = ay + dy * t - py;
    return sqrtf(qx * qx + qy * qy);
}

static SDL_Texture *make_arc_texture(SDL_Renderer *r, float cx, float cy, float rx, float ry, float a0, float a1,
                                     SDL_Color color, SDL_FRect *dst) {
    float pts[ARC_STEPS + 1][2], x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i <= ARC_STEPS; i++) {
        float a = (a0 + (a1 - a0) * i / ARC_STEPS) * 3.14159265f / 180.0f;
        pts[i][0] = cx + sinf(a) * rx;
        pts[i][1] = cy - cosf(a) * ry;
        if (pts[i][0] < x0) x0 = pts[i][0];
        if (pts[i][0] > x1) x1 = pts[i][0];
        if (pts[i][1] < y0) y0 = pts[i][1];
        if (pts[i][1] > y1) y1 = pts[i][1];
    }
    int ox = (int)floorf(x0 - ARC_INK_HALF - 3.0f), oy = (int)floorf(y0 - ARC_INK_HALF - 3.0f);
    int w = (int)ceilf(x1 + ARC_INK_HALF + 3.0f) - ox, h = (int)ceilf(y1 + ARC_INK_HALF + 3.0f) - oy;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return NULL;
    const SDL_Color ink = { 11, 7, 32, 255 };   /* CH_INK */
    for (int y = 0; y < h; y++) {
        Uint8 *row = (Uint8 *)surf->pixels + y * surf->pitch;
        float py = (float)(oy + y) + 0.5f;
        for (int x = 0; x < w; x++) {
            float px = (float)(ox + x) + 0.5f, d = 1e9f;
            for (int i = 0; i < ARC_STEPS; i++) {
                /* l'arc est monotone en y : on ne teste que les segments assez proches de la ligne */
                float ya = pts[i][1], yb = pts[i + 1][1];
                float lo = ya < yb ? ya : yb, hi = ya < yb ? yb : ya;
                if (py < lo - ARC_INK_HALF - 3.0f || py > hi + ARC_INK_HALF + 3.0f) continue;
                float di = seg_dist(px, py, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]);
                if (di < d) d = di;
            }
            /* couverture du bord, rampe centrée sur le vrai bord et large de ARC_EDGE_PX */
            float ci = (ARC_INK_HALF - d) / ARC_EDGE_PX + 0.5f, cc = (ARC_COLOR_HALF - d) / ARC_EDGE_PX + 0.5f;
            ci = ci < 0.0f ? 0.0f : ci > 1.0f ? 1.0f : ci;
            cc = cc < 0.0f ? 0.0f : cc > 1.0f ? 1.0f : cc;
            ci = ci * ci * (3.0f - 2.0f * ci);   /* lissé en S : plus de marches aux extrémités de la rampe */
            cc = cc * cc * (3.0f - 2.0f * cc);
            Uint8 *p = row + x * 4;
            p[0] = (Uint8)(ink.r + (color.r - ink.r) * cc);
            p[1] = (Uint8)(ink.g + (color.g - ink.g) * cc);
            p[2] = (Uint8)(ink.b + (color.b - ink.b) * cc);
            p[3] = (Uint8)(ci * 255.0f + 0.5f);
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   /* bilinéaire, comme les autres images du jeu */
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_FreeSurface(surf);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    *dst = (SDL_FRect){ (float)ox, (float)oy, (float)w, (float)h };
    return t;
}

bool sprites_build_arcs(Sprites *s, SDL_Renderer *r, float left_cx, float right_cx, float cy, float rx, float ry) {
    const SDL_Color cyan = { 79, 200, 240, 255 }, pink = { 247, 12, 147, 255 };   /* CH_CYAN, CH_PINK */
    s->arc[0] = make_arc_texture(r, left_cx, cy, rx, ry, 198.0f, 342.0f, cyan, &s->arc_dst[0]);
    s->arc[1] = make_arc_texture(r, right_cx, cy, rx, ry, 18.0f, 162.0f, pink, &s->arc_dst[1]);
    return s->arc[0] && s->arc[1];
}

void sprite_draw_arcs(const Sprites *s, SDL_Renderer *r) {
    for (int i = 0; i < 2; i++)
        if (s->arc[i]) SDL_RenderCopyF(r, s->arc[i], NULL, &s->arc_dst[i]);
}
