#include "font.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "third_party/stb_image.h"

/* Rectangle d'atlas + décalage par rapport au crayon (x courant, haut des majuscules) */
typedef struct { short x, y, w, h, xoff, yoff; } GlyphRect;

struct FontGlyph {
    GlyphRect sharp;
    GlyphRect glow;     /* à mi-résolution dans l'atlas : dessiné à 2x ; w = 0 si pas de halo.
                           Faces creuses : anneau de la lettre « en contour », à pleine résolution */
    GlyphRect outline;  /* glyphe épaissi, dessiné en sombre sous la lettre (faces avec halo) */
    short     advance;
};

/* Contour sombre entre le halo et la lettre : garde le texte lisible quand le halo est de la même
   couleur, ou sur une image de fond claire. Bleu nuit de la palette. */
static const SDL_Color OUTLINE_COLOR = {28, 30, 42, 200};

typedef enum { PASS_SHARP, PASS_GLOW, PASS_OUTLINE, PASS_HOLLOW } Pass;

/* Caractères de Windows-1252 rangés sur 0x80..0x9F (même table que tools/font2atlas.py) */
static int slot_for(unsigned cp) {
    if (cp < 0x100) return (int)cp;
    switch (cp) {
    case 0x20AC: return 0x80;  /* € */
    case 0x2026: return 0x85;  /* … */
    case 0x2018: return 0x91;
    case 0x2019: return 0x92;  /* ’ */
    case 0x201C: return 0x93;
    case 0x201D: return 0x94;
    case 0x2022: return 0x95;  /* • */
    case 0x2013: return 0x96;  /* – */
    case 0x2014: return 0x97;  /* — */
    default:     return '?';
    }
}

/* Décode un caractère UTF-8 et avance *p. Octet invalide : lu tel quel (Latin-1). */
static unsigned next_codepoint(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    unsigned cp = s[0];
    int len = 1;
    if (cp >= 0xC0 && cp < 0xE0 && (s[1] & 0xC0) == 0x80) {
        cp = ((cp & 0x1F) << 6) | (s[1] & 0x3F); len = 2;
    } else if (cp >= 0xE0 && cp < 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((cp & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); len = 3;
    } else if (cp >= 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        cp = '?'; len = 4;
    }
    *p += len;
    return cp;
}

static const FontGlyph *find_glyph(const Font *f, FontFace face, unsigned cp) {
    const FontFaceInfo *fi = &f->faces[face];
    int idx = fi->map[slot_for(cp)];
    if (idx < 0) idx = fi->map['?'];
    return idx >= 0 ? &f->glyphs[idx] : NULL;
}

static bool load_metrics(Font *f, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "font: impossible d'ouvrir '%s'\n", path); return false; }

    /* Premier passage : compter les glyphes pour une seule allocation */
    char line[200];
    int count = 0;
    while (fgets(line, sizeof line, fp)) if (line[0] == 'G') count++;
    f->glyphs = calloc((size_t)(count > 0 ? count : 1), sizeof *f->glyphs);
    if (!f->glyphs) { fclose(fp); return false; }
    rewind(fp);

    int face = -1, n = 0;
    while (fgets(line, sizeof line, fp) && n < count) {
        if (line[0] == 'F') {
            int idx, cap, lh, glow, half_outline, outline = -1, hollow = 0;
            if (sscanf(line, "FACE %d %d %d %d %d %d %d", &idx, &cap, &lh, &glow, &half_outline, &outline, &hollow) >= 5
                && idx >= 0 && idx < FONT_FACE_COUNT) {
                face = idx;
                f->faces[face].cap_height = cap;
                f->faces[face].line_height = lh;
                f->faces[face].has_glow = glow != 0;
                f->faces[face].half_outline = half_outline != 0;
                f->faces[face].has_outline = outline < 0 ? glow != 0 : outline != 0;   /* ancien format : halo => contour */
                f->faces[face].has_hollow = hollow != 0 && glow == 0;
            } else {
                face = -1;
            }
        } else if (line[0] == 'G' && face >= 0) {
            int v[20];
            if (sscanf(line, "G %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9],
                       &v[10], &v[11], &v[12], &v[13], &v[14], &v[15], &v[16], &v[17], &v[18], &v[19]) != 20
                || v[0] < 0 || v[0] > 255)
                continue;
            FontGlyph *g = &f->glyphs[n];
            g->sharp = (GlyphRect){ (short)v[1], (short)v[2], (short)v[3], (short)v[4], (short)v[5], (short)v[6] };
            g->advance = (short)v[7];
            g->glow = (GlyphRect){ (short)v[8], (short)v[9], (short)v[10], (short)v[11], (short)v[12], (short)v[13] };
            g->outline = (GlyphRect){ (short)v[14], (short)v[15], (short)v[16], (short)v[17], (short)v[18], (short)v[19] };
            f->faces[face].map[v[0]] = (short)n++;
        }
    }
    fclose(fp);
    return n > 0;
}

bool font_init(Font *f, SDL_Renderer *r) {
    *f = (Font){0};
    for (int i = 0; i < FONT_FACE_COUNT; i++)
        for (int c = 0; c < 256; c++) f->faces[i].map[c] = -1;
    if (!load_metrics(f, ASSET_PATH("ui/font.txt"))) { font_shutdown(f); return false; }

    const char *path = ASSET_PATH("ui/font.png");
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) { fprintf(stderr, "font: impossible de charger '%s'\n", path); font_shutdown(f); return false; }
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (surf) {
        /* bilinéaire : les halos sont agrandis 2x et le texte peut être animé en taille */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        f->atlas = SDL_CreateTextureFromSurface(r, surf);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_FreeSurface(surf);
    }
    stbi_image_free(px);
    if (!f->atlas) { fprintf(stderr, "font: texture: %s\n", SDL_GetError()); font_shutdown(f); return false; }
    return true;
}

void font_shutdown(Font *f) {
    if (f->atlas) SDL_DestroyTexture(f->atlas);
    free(f->glyphs);
    f->atlas = NULL;
    f->glyphs = NULL;
}

int font_width(const Font *f, FontFace face, const char *text) {
    if (!f->glyphs) return 0;
    int w = 0;
    for (const char *p = text; *p;) {
        const FontGlyph *g = find_glyph(f, face, next_codepoint(&p));
        if (g) w += g->advance;
    }
    return w;
}

int font_cap_height(const Font *f, FontFace face) {
    return f->faces[face].cap_height;
}

/* Dessine une couche (nette, halo ou contour) de tout le texte, crayon partant de (x, y) */
static void draw_pass(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, float scale,
                      Pass pass, const char *text) {
    /* le halo, et le contour des grandes tailles, sont stockés à mi-résolution */
    const float k = (pass == PASS_GLOW || (pass == PASS_OUTLINE && f->faces[face].half_outline)) ? 2.0f : 1.0f;
    for (const char *p = text; *p;) {
        const FontGlyph *g = find_glyph(f, face, next_codepoint(&p));
        if (!g) continue;
        const GlyphRect *gr = pass == PASS_GLOW ? &g->glow : pass == PASS_OUTLINE ? &g->outline : &g->sharp;
        if (gr->w > 0) {
            SDL_Rect src = { gr->x, gr->y, gr->w, gr->h };
            SDL_FRect dst = { x + gr->xoff * scale, y + gr->yoff * scale, gr->w * k * scale, gr->h * k * scale };
            if (pass == PASS_SHARP && scale == 1.0f) { dst.x = floorf(dst.x + 0.5f); dst.y = floorf(dst.y + 0.5f); } /* net au pixel */
            SDL_RenderCopyF(r, f->atlas, &src, &dst);
        }
        x += g->advance * scale;
    }
}

void font_draw_ex(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, TextAlign align,
                  float scale, SDL_Color color, SDL_Color glow, const char *text) {
    if (!f->atlas || !text[0]) return;
    float w = (float)font_width(f, face, text);
    float ax = align == ALIGN_CENTER ? w * 0.5f : align == ALIGN_RIGHT ? w : 0.0f;
    /* agrandissement autour du point d'ancrage, centré verticalement sur les majuscules */
    float half_cap = f->faces[face].cap_height * 0.5f;
    float px = x - ax * scale, py = y + half_cap - half_cap * scale;

    if (glow.a > 0 && f->faces[face].has_glow) {
        SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(f->atlas, glow.r, glow.g, glow.b);
        SDL_SetTextureAlphaMod(f->atlas, (Uint8)(glow.a * color.a / 255));
        draw_pass(f, r, face, px, py, scale, PASS_GLOW, text);
        SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(f->atlas, OUTLINE_COLOR.r, OUTLINE_COLOR.g, OUTLINE_COLOR.b);
        SDL_SetTextureAlphaMod(f->atlas, (Uint8)(OUTLINE_COLOR.a * color.a / 255));
        draw_pass(f, r, face, px, py, scale, PASS_OUTLINE, text);
    }
    SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(f->atlas, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(f->atlas, color.a);
    draw_pass(f, r, face, px, py, scale, PASS_SHARP, text);
}

void font_draw(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, TextAlign align,
               SDL_Color color, const char *text) {
    font_draw_ex(f, r, face, x, y, align, 1.0f, color, (SDL_Color){0, 0, 0, 0}, text);
}

void font_draw_glow(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, TextAlign align,
                    SDL_Color color, SDL_Color glow, const char *text) {
    font_draw_ex(f, r, face, x, y, align, 1.0f, color, glow, text);
}

void font_draw_ink(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, TextAlign align, float scale,
                   SDL_Color fill, SDL_Color ink, SDL_Color shadow, float shadow_dx, float shadow_dy,
                   const char *text) {
    if (!f->atlas || !text[0]) return;
    float w = (float)font_width(f, face, text);
    float ax = align == ALIGN_CENTER ? w * 0.5f : align == ALIGN_RIGHT ? w : 0.0f;
    float half_cap = f->faces[face].cap_height * 0.5f;
    float px = x - ax * scale, py = y + half_cap - half_cap * scale;
    bool outline = f->faces[face].has_outline;
    Pass body = outline ? PASS_OUTLINE : PASS_SHARP;   /* silhouette : contour compris */
    SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_BLEND);
    if (shadow.a > 0) {
        SDL_SetTextureColorMod(f->atlas, shadow.r, shadow.g, shadow.b);
        SDL_SetTextureAlphaMod(f->atlas, shadow.a);
        draw_pass(f, r, face, px + shadow_dx, py + shadow_dy, scale, body, text);
    }
    if (outline) {
        SDL_SetTextureColorMod(f->atlas, ink.r, ink.g, ink.b);
        SDL_SetTextureAlphaMod(f->atlas, ink.a);
        draw_pass(f, r, face, px, py, scale, PASS_OUTLINE, text);
    }
    if (fill.a > 0) {
        SDL_SetTextureColorMod(f->atlas, fill.r, fill.g, fill.b);
        SDL_SetTextureAlphaMod(f->atlas, fill.a);
        draw_pass(f, r, face, px, py, scale, PASS_SHARP, text);
    }
}

/* --- texte tourné, une commande de dessin par couche --- */

#define ROT_MAX_GLYPHS 96

/* Couche `pass` de tout le texte en un seul SDL_RenderGeometry : crayon parti de (x, y) dans le repère du texte,
   puis tout est tourné de rot autour de son pivot. Couleur portée par les sommets. */
static void draw_pass_rot(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, float scale, FontRot rot,
                          Pass pass, SDL_Color color, const char *text) {
    SDL_Vertex v[ROT_MAX_GLYPHS * 4];
    int idx[ROT_MAX_GLYPHS * 6], n = 0;
    const float k = (pass == PASS_GLOW || (pass == PASS_OUTLINE && f->faces[face].half_outline)) ? 2.0f : 1.0f;
    const float rad = rot.angle_deg * (3.14159265f / 180.0f), cs = cosf(rad), sn = sinf(rad);
    const float shear = tanf(rot.skew_deg * (3.14159265f / 180.0f));
    int aw, ah;
    if (SDL_QueryTexture(f->atlas, NULL, NULL, &aw, &ah) != 0) return;
    for (const char *p = text; *p && n < ROT_MAX_GLYPHS;) {
        const FontGlyph *g = find_glyph(f, face, next_codepoint(&p));
        if (!g) continue;
        const GlyphRect *gr = pass == PASS_OUTLINE ? &g->outline : pass == PASS_SHARP ? &g->sharp : &g->glow;
        if (gr->w > 0) {
            float x0 = x + gr->xoff * scale, y0 = y + gr->yoff * scale;
            float x1 = x0 + gr->w * k * scale, y1 = y0 + gr->h * k * scale;
            float u0 = gr->x / (float)aw, v0 = gr->y / (float)ah;
            float u1 = (gr->x + gr->w) / (float)aw, v1 = (gr->y + gr->h) / (float)ah;
            const float qx[4] = { x0, x1, x1, x0 }, qy[4] = { y0, y0, y1, y1 };
            const float qu[4] = { u0, u1, u1, u0 }, qv[4] = { v0, v0, v1, v1 };
            for (int c = 0; c < 4; c++) {
                float dx = qx[c] - rot.pivot_x, dy = qy[c] - rot.pivot_y;
                dx += shear * dy;
                v[n * 4 + c] = (SDL_Vertex){ { rot.pivot_x + dx * cs - dy * sn, rot.pivot_y + dx * sn + dy * cs },
                                             color, { qu[c], qv[c] } };
            }
            const int q[6] = { 0, 1, 2, 0, 2, 3 };
            for (int c = 0; c < 6; c++) idx[n * 6 + c] = n * 4 + q[c];
            n++;
        }
        x += (g->advance + rot.tracking) * scale;
    }
    if (n > 0) SDL_RenderGeometry(r, f->atlas, v, n * 4, idx, n * 6);
}

int font_width_tracked(const Font *f, FontFace face, const char *text, float tracking) {
    int chars = 0;
    for (const char *p = text; *p;) { next_codepoint(&p); chars++; }
    return font_width(f, face, text) + (chars > 1 ? (int)(tracking * (chars - 1) + 0.5f) : 0);
}

/* Réglages de texture neutres : la couleur vient des sommets */
static void rot_begin(const Font *f) {
    SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(f->atlas, 255, 255, 255);
    SDL_SetTextureAlphaMod(f->atlas, 255);
}

void font_draw_ink_rot(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, float scale, FontRot rot,
                       SDL_Color fill, SDL_Color ink, SDL_Color shadow, float shadow_dx, float shadow_dy,
                       const char *text) {
    if (!f->atlas || !text[0]) return;
    rot_begin(f);
    Pass body = f->faces[face].has_outline ? PASS_OUTLINE : PASS_SHARP;
    if (shadow.a > 0) draw_pass_rot(f, r, face, x + shadow_dx, y + shadow_dy, scale, rot, body, shadow, text);
    if (f->faces[face].has_outline && ink.a > 0) draw_pass_rot(f, r, face, x, y, scale, rot, PASS_OUTLINE, ink, text);
    if (fill.a > 0) draw_pass_rot(f, r, face, x, y, scale, rot, PASS_SHARP, fill, text);
}

void font_draw_hollow_rot(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, float scale, FontRot rot,
                          SDL_Color color, const char *text) {
    if (!f->atlas || !text[0] || color.a == 0) return;
    rot_begin(f);
    draw_pass_rot(f, r, face, x, y, scale, rot, f->faces[face].has_hollow ? PASS_HOLLOW : PASS_SHARP, color, text);
}

SDL_Color font_lighten(SDL_Color c, float t) {
    return (SDL_Color){ (Uint8)(c.r + (255 - c.r) * t), (Uint8)(c.g + (255 - c.g) * t),
                        (Uint8)(c.b + (255 - c.b) * t), c.a };
}

float font_angle_on_ellipse(const Font *f, FontFace face, float rx, float ry, float scale, const char *text) {
    float radius = (rx + ry) * 0.5f;
    return radius > 0.0f ? font_width(f, face, text) * scale / radius * (180.0f / 3.14159265f) : 0.0f;
}

float font_draw_on_ellipse(const Font *f, SDL_Renderer *r, FontFace face, float cx, float cy, float rx, float ry,
                           float start_deg, float scale, SDL_Color color, const char *text) {
    if (!f->atlas || !f->glyphs) return start_deg;
    const float deg = 180.0f / 3.14159265f, radius = (rx + ry) * 0.5f;
    const float half_cap = f->faces[face].cap_height * 0.5f;
    float a = start_deg;
    SDL_SetTextureBlendMode(f->atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(f->atlas, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(f->atlas, color.a);
    for (const char *p = text; *p;) {
        const FontGlyph *g = find_glyph(f, face, next_codepoint(&p));
        if (!g || radius <= 0.0f) continue;
        float adv = g->advance * scale;
        float mid = a + adv * 0.5f / radius * deg;           /* milieu de la case du caractère */
        float rad = mid / deg, sn = sinf(rad), cs = cosf(rad);
        float px = cx + sn * rx, py = cy - cs * ry;          /* point de la ligne médiane */
        const GlyphRect *gr = &g->sharp;
        if (gr->w > 0) {
            /* centre du glyphe dans le repère local (u : le long de la courbe, v : vers l'intérieur) */
            float u = (gr->xoff + gr->w * 0.5f) * scale - adv * 0.5f;
            float v = (gr->yoff + gr->h * 0.5f) * scale - half_cap * scale;
            float gx = px + u * cs - v * sn, gy = py + u * sn + v * cs;
            float w = gr->w * scale, h = gr->h * scale;
            SDL_Rect src = { gr->x, gr->y, gr->w, gr->h };
            SDL_FRect dst = { gx - w * 0.5f, gy - h * 0.5f, w, h };
            SDL_RenderCopyExF(r, f->atlas, &src, &dst, mid, NULL, SDL_FLIP_NONE);
        }
        a += adv / radius * deg;
    }
    return a;
}
