#include "ui.h"
#include "config.h"
#include "shapes.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GRID_STEP 48
#define HINT_ICON 28.0f

SDL_Color ui_alpha(SDL_Color c, Uint8 a) {
    c.a = a;
    return c;
}

void ui_backdrop(SDL_Renderer *r, Uint8 alpha, bool grid) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color bg = UI_NAVY, line = UI_PANEL;
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, alpha);
    SDL_RenderFillRect(r, NULL);
    if (!grid) return;
    SDL_SetRenderDrawColor(r, line.r, line.g, line.b, alpha);
    for (int x = 0; x < SCREEN_W; x += GRID_STEP) SDL_RenderDrawLine(r, x, 0, x, SCREEN_H);
    for (int y = 0; y < SCREEN_H; y += GRID_STEP) SDL_RenderDrawLine(r, 0, y, SCREEN_W, y);
}

void ui_panel(SDL_Renderer *r, SDL_FRect rect, float radius, SDL_Color fill, SDL_Color border, float glow) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (fill.a) shapes_rounded_rect(r, rect, radius, fill);
    if (border.a) shapes_rounded_rect_neon(r, rect, radius, 1.2f, 6.0f, glow, border, border);
}

/* Longueur en octets du caractère UTF-8 commençant en s */
static int utf8_len(const char *s) {
    unsigned char c = (unsigned char)*s;
    return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
}

void ui_fit_text(const Font *f, FontFace face, const char *text, int max_w, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", text);
    if (font_width(f, face, out) <= max_w) return;
    static const char ELLIPSIS[] = "\xE2\x80\xA6"; /* … */
    int ell_w = font_width(f, face, ELLIPSIS);
    /* on garde le plus long préfixe (coupé entre deux caractères) qui tient avec « … » */
    size_t keep = 0;
    char tmp[256];
    for (size_t i = 0; text[i] && i + 4 < sizeof tmp; i += (size_t)utf8_len(text + i)) {
        size_t next = i + (size_t)utf8_len(text + i);
        memcpy(tmp, text, next);
        tmp[next] = '\0';
        if (font_width(f, face, tmp) + ell_w > max_w) break;
        keep = next;
    }
    while (keep > 0 && text[keep - 1] == ' ') keep--;   /* pas d'espace avant « … » */
    snprintf(out, out_size, "%.*s%s", (int)keep, text, ELLIPSIS);
}

void ui_format_score(int score, char *out, size_t out_size) {
    char digits[16];
    snprintf(digits, sizeof digits, "%d", score < 0 ? 0 : score);
    size_t n = strlen(digits), k = 0;
    for (size_t i = 0; i < n && k + 2 < out_size; i++) {
        if (i > 0 && (n - i) % 3 == 0) out[k++] = ' ';
        out[k++] = digits[i];
    }
    out[k] = '\0';
}

SDL_Color ui_difficulty_color(const char *name) {
    if (SDL_strcasecmp(name, "EASY") == 0)   return UI_TEAL;
    if (SDL_strcasecmp(name, "NORMAL") == 0) return UI_LAVENDER;
    if (SDL_strcasecmp(name, "HARD") == 0)   return UI_CORAL;
    if (SDL_strcasecmp(name, "EXPERT") == 0) return (SDL_Color){206, 110, 214, 255};
    return UI_SLATE;
}

static const char *button_name(Button b) {
    switch (b) {
    case BTN_SELECT: return "SELECT";
    case BTN_START:  return "START";
    case BTN_L:      return "L";
    case BTN_R:      return "R";
    case BTN_SQUARE: return "SQUARE";
    default:         return "?";
    }
}

float ui_button_hint(SDL_Renderer *r, const Font *f, const Sprites *s, float x, float y,
                     const Button *buttons, int n, const char *label) {
    for (int i = 0; i < n; i++) {
        if (sprite_has_icon(buttons[i])) {
            sprite_draw_icon(s, r, buttons[i], x + HINT_ICON / 2.0f, y, HINT_ICON, 255);
            x += HINT_ICON + 2.0f;
        } else {
            /* pas d'icône dans l'atlas (Select, Start, L, R, Carré) : pastille avec le nom */
            const char *name = button_name(buttons[i]);
            float w = (float)font_width(f, FONT_SMALL, name) + 16.0f;
            ui_panel(r, (SDL_FRect){x, y - 12, w, 24}, 12, UI_SLATE, UI_NONE, 0.0f);
            font_draw(f, r, FONT_SMALL, x + w / 2, y - font_cap_height(f, FONT_SMALL) / 2.0f, ALIGN_CENTER, UI_NAVY, name);
            x += w + 2.0f;
        }
    }
    x += 6.0f;
    font_draw(f, r, FONT_SMALL, x, y - font_cap_height(f, FONT_SMALL) / 2.0f, ALIGN_LEFT, UI_SLATE, label);
    return x + (float)font_width(f, FONT_SMALL, label);
}

void ui_approach(float *v, float target, float speed, float dt) {
    *v += (target - *v) * (1.0f - expf(-speed * dt));
    if (fabsf(target - *v) < 0.001f) *v = target;
}

float ui_button_hint_width(const Font *f, const Button *buttons, int n, const char *label) {
    float w = 0.0f;
    for (int i = 0; i < n; i++)
        w += sprite_has_icon(buttons[i]) ? HINT_ICON + 2.0f : (float)font_width(f, FONT_SMALL, button_name(buttons[i])) + 18.0f;
    return w + 6.0f + (float)font_width(f, FONT_SMALL, label);
}

void ui_duotone(SDL_Renderer *r) {
    /* dégradé à 115° (CSS) : violet en haut à gauche, rose en bas à droite */
    const SDL_Color a = CH_VIOLET, b = CH_DUO_PINK;
    SDL_Color mid1 = { (Uint8)(a.r + (b.r - a.r) * 0.3f), (Uint8)(a.g + (b.g - a.g) * 0.3f), (Uint8)(a.b + (b.b - a.b) * 0.3f), 255 };
    SDL_Color mid2 = { (Uint8)(a.r + (b.r - a.r) * 0.75f), (Uint8)(a.g + (b.g - a.g) * 0.75f), (Uint8)(a.b + (b.b - a.b) * 0.75f), 255 };
    SDL_Vertex v[4] = {
        { {0, 0}, a, {0, 0} }, { {SCREEN_W, 0}, mid2, {0, 0} },
        { {SCREEN_W, SCREEN_H}, b, {0, 0} }, { {0, SCREEN_H}, mid1, {0, 0} },
    };
    const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_MOD);
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

void ui_veil_gradient(SDL_Renderer *r, const float *stop_x, const float *stop_a, int n) {
    enum { MAX_STOPS = 8 };
    SDL_Vertex v[MAX_STOPS * 2];
    int idx[(MAX_STOPS - 1) * 6], k = 0;
    if (n > MAX_STOPS) n = MAX_STOPS;
    for (int i = 0; i < n; i++) {
        SDL_Color c = ui_alpha(CH_INK, (Uint8)(stop_a[i] * 255.0f));
        v[i * 2]     = (SDL_Vertex){ {stop_x[i] * SCREEN_W, 0}, c, {0, 0} };
        v[i * 2 + 1] = (SDL_Vertex){ {stop_x[i] * SCREEN_W, SCREEN_H}, c, {0, 0} };
        if (i + 1 < n) {
            int b = i * 2;
            idx[k++] = b; idx[k++] = b + 2; idx[k++] = b + 3;
            idx[k++] = b; idx[k++] = b + 3; idx[k++] = b + 1;
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, NULL, v, n * 2, idx, k);
}

void ui_veil_left(SDL_Renderer *r) {
    static const float STOP_X[5] = { 0.0f, 0.36f, 0.55f, 0.78f, 1.0f };
    static const float STOP_A[5] = { 0.9f, 0.9f, 0.55f, 0.0f, 0.0f };
    ui_veil_gradient(r, STOP_X, STOP_A, 5);
}

bool ui_poly_append(SDL_Vertex *v, int *nv, int vmax, int *idx, int *ni, int imax, const SDL_FPoint *p, int n,
                    SDL_Color c) {
    const float f = UI_EDGE_FEATHER * 0.5f;   /* de part et d'autre du bord : la moitié vers l'intérieur */
    if (n < 3 || n > 48) return false;
    if (*nv + 1 + (f > 0.0f ? 2 : 1) * n > vmax || *ni + (f > 0.0f ? 9 : 3) * n > imax) return false;
    float area = 0.0f, cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < n; i++) {
        const SDL_FPoint a = p[i], b = p[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
        cx += a.x; cy += a.y;
    }
    const float sign = area >= 0.0f ? 1.0f : -1.0f;   /* sens du polygone : la normale sortante en dépend */
    const int base = *nv;
    v[(*nv)++] = (SDL_Vertex){ { cx / n, cy / n }, c, {0, 0} };
    const int inner = *nv;
    const SDL_Color clear = { c.r, c.g, c.b, 0 };
    for (int i = 0; i < n; i++) {
        SDL_FPoint q = p[i];
        float ox = 0.0f, oy = 0.0f;   /* décalage sortant (bissectrice des deux arêtes) */
        if (f > 0.0f) {
            const SDL_FPoint a = p[(i + n - 1) % n], b = p[(i + 1) % n];
            float e1x = q.x - a.x, e1y = q.y - a.y, e2x = b.x - q.x, e2y = b.y - q.y;
            float l1 = sqrtf(e1x * e1x + e1y * e1y), l2 = sqrtf(e2x * e2x + e2y * e2y);
            if (l1 > 1e-4f && l2 > 1e-4f) {
                float n1x = sign * e1y / l1, n1y = -sign * e1x / l1, n2x = sign * e2y / l2, n2y = -sign * e2x / l2;
                float mx = n1x + n2x, my = n1y + n2y, ml = sqrtf(mx * mx + my * my);
                if (ml > 1e-4f) {
                    mx /= ml; my /= ml;
                    float k = mx * n1x + my * n1y;          /* cos du demi-angle : allonge le décalage aux angles aigus */
                    k = 1.0f / (k < 0.35f ? 0.35f : k);     /* plafonné : pas de pointe aux sommets très aigus */
                    ox = mx * f * k; oy = my * f * k;
                }
            }
        }
        v[(*nv)++] = (SDL_Vertex){ { q.x - ox, q.y - oy }, c, {0, 0} };
    }
    const int outer = *nv;
    if (f > 0.0f)
        for (int i = 0; i < n; i++) {
            const SDL_FPoint q = v[inner + i].position;
            const SDL_FPoint s = p[i];
            v[(*nv)++] = (SDL_Vertex){ { 2.0f * s.x - q.x, 2.0f * s.y - q.y }, clear, {0, 0} };   /* symétrique de l'intérieur */
        }
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        idx[(*ni)++] = base; idx[(*ni)++] = inner + i; idx[(*ni)++] = inner + j;
        if (f > 0.0f) {
            idx[(*ni)++] = inner + i; idx[(*ni)++] = outer + i; idx[(*ni)++] = outer + j;
            idx[(*ni)++] = inner + i; idx[(*ni)++] = outer + j; idx[(*ni)++] = inner + j;
        }
    }
    return true;
}

void ui_poly(SDL_Renderer *r, const SDL_FPoint *p, int n, SDL_Color c) {
    SDL_Vertex v[1 + 2 * 48];
    int idx[9 * 48], nv = 0, ni = 0;
    if (c.a == 0 || !ui_poly_append(v, &nv, (int)(sizeof v / sizeof v[0]), idx, &ni, (int)(sizeof idx / sizeof idx[0]), p, n, c))
        return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, NULL, v, nv, idx, ni);
}

void ui_skew_box(SDL_Renderer *r, SDL_FRect rect, float lean, SDL_Color color) {
    const SDL_FPoint p[4] = { {rect.x + lean, rect.y}, {rect.x + rect.w + lean, rect.y},
                              {rect.x + rect.w - lean, rect.y + rect.h}, {rect.x - lean, rect.y + rect.h} };
    ui_poly(r, p, 4, color);
}

void ui_fill_rect_rot(SDL_Renderer *r, SDL_FRect rect, FontRot rot, SDL_Color color) {
    const float rad = rot.angle_deg * (3.14159265f / 180.0f), cs = cosf(rad), sn = sinf(rad);
    const float px[4] = { rect.x, rect.x + rect.w, rect.x + rect.w, rect.x };
    const float py[4] = { rect.y, rect.y, rect.y + rect.h, rect.y + rect.h };
    SDL_FPoint p[4];
    for (int i = 0; i < 4; i++) {
        float dx = px[i] - rot.pivot_x, dy = py[i] - rot.pivot_y;
        p[i] = (SDL_FPoint){ rot.pivot_x + dx * cs - dy * sn, rot.pivot_y + dx * sn + dy * cs };
    }
    ui_poly(r, p, 4, color);
}

/* Aides « Cascade pop » : icônes de 22 px, pastille blanche pour les boutons sans icône, libellé FONT_LABEL */
#define POP_ICON 22.0f

float ui_button_hint_pop(SDL_Renderer *r, const Font *f, const Sprites *s, float x, float y,
                         const Button *buttons, int n, const char *label) {
    for (int i = 0; i < n; i++) {
        if (sprite_has_icon(buttons[i])) {
            sprite_draw_icon(s, r, buttons[i], x + POP_ICON / 2.0f, y, POP_ICON, 255);
            x += POP_ICON + 2.0f;
        } else {
            const char *name = button_name(buttons[i]);
            float w = (float)font_width(f, FONT_LABEL, name) + 12.0f;
            if (w < POP_ICON - 2.0f) w = POP_ICON - 2.0f;
            ui_panel(r, (SDL_FRect){x, y - 10, w, 20}, 10, CH_WHITE, UI_NONE, 0.0f);
            font_draw(f, r, FONT_LABEL, x + w / 2, y - font_cap_height(f, FONT_LABEL) / 2.0f, ALIGN_CENTER, CH_INK, name);
            x += w + 2.0f;
        }
    }
    x += 4.0f;
    font_draw(f, r, FONT_LABEL, x, y - font_cap_height(f, FONT_LABEL) / 2.0f, ALIGN_LEFT, CH_WHITE, label);
    return x + (float)font_width(f, FONT_LABEL, label);
}

void ui_veil_radial(SDL_Renderer *r) {
    /* ellipse « farthest-corner » du CSS : demi-axes = demi-écran x racine de 2 ; arrêt à 75 %, puis un anneau
       extérieur au-delà des coins pour garder 62 % jusqu'au bord */
    enum { SEG = 32 };
    const float cx = SCREEN_W * 0.5f, cy = SCREEN_H * 0.5f;
    const float rx = cx * 1.4142f * 0.75f, ry = cy * 1.4142f * 0.75f;
    const SDL_Color in = ui_alpha(CH_INK, 64), out = ui_alpha(CH_INK, 158);
    SDL_Vertex v[1 + SEG * 2];
    int idx[SEG * 9], n = 0;
    v[0] = (SDL_Vertex){ {cx, cy}, in, {0, 0} };
    for (int i = 0; i < SEG; i++) {
        float a = (float)i / SEG * 6.2831853f, cs = cosf(a), sn = sinf(a);
        v[1 + i]       = (SDL_Vertex){ {cx + cs * rx, cy + sn * ry}, out, {0, 0} };
        v[1 + SEG + i] = (SDL_Vertex){ {cx + cs * rx * 1.8f, cy + sn * ry * 1.8f}, out, {0, 0} };
    }
    for (int i = 0; i < SEG; i++) {
        int j = (i + 1) % SEG;
        idx[n++] = 0; idx[n++] = 1 + i; idx[n++] = 1 + j;
        idx[n++] = 1 + i; idx[n++] = 1 + SEG + i; idx[n++] = 1 + SEG + j;
        idx[n++] = 1 + i; idx[n++] = 1 + SEG + j; idx[n++] = 1 + j;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, NULL, v, 1 + SEG * 2, idx, n);
}

void ui_pop_text(SDL_Renderer *r, const Font *f, FontFace face, float cx, float cy, float scale, float tracking_px,
                 float angle_deg, float skew_deg, SDL_Color fill, SDL_Color ink, SDL_Color shadow, float sdx, float sdy,
                 const char *text) {
    float trk = scale > 0.0f ? tracking_px / scale : 0.0f;
    float w = (float)font_width_tracked(f, face, text, trk) * scale;
    const FontRot rot = { angle_deg, cx, cy, trk, skew_deg };
    font_draw_ink_rot(f, r, face, cx - w * 0.5f, cy - font_cap_height(f, face) * scale * 0.5f, scale, rot, fill, ink,
                      shadow, sdx, sdy, text);
}

void ui_star(SDL_Renderer *r, float cx, float cy, float size, float angle_deg, SDL_Color c) {
    static const float PTS[8][2] = { {.5f, 0}, {.6f, .4f}, {1, .5f}, {.6f, .6f}, {.5f, 1}, {.4f, .6f}, {0, .5f}, {.4f, .4f} };
    if (c.a == 0 || size <= 0.0f) return;
    const float rad = angle_deg * (3.14159265f / 180.0f), cs = cosf(rad), sn = sinf(rad);
    SDL_FPoint p[8];
    for (int i = 0; i < 8; i++) {
        float x = (PTS[i][0] - 0.5f) * size, y = (PTS[i][1] - 0.5f) * size;
        p[i] = (SDL_FPoint){ cx + x * cs - y * sn, cy + x * sn + y * cs };
    }
    ui_poly(r, p, 8, c);
}

void ui_upper(const char *in, char *out, size_t size) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 1 < size; p++) {
        unsigned char c = *p;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        else if (c == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7 && n + 2 < size) {
            out[n++] = (char)c; out[n++] = (char)(p[1] - 0x20); p++; continue;
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
}

float ui_button_hint_pop_width(const Font *f, const Button *buttons, int n, const char *label) {
    float w = 0.0f;
    for (int i = 0; i < n; i++) {
        if (sprite_has_icon(buttons[i])) { w += POP_ICON + 2.0f; continue; }
        float bw = (float)font_width(f, FONT_LABEL, button_name(buttons[i])) + 12.0f;
        w += (bw < POP_ICON - 2.0f ? POP_ICON - 2.0f : bw) + 2.0f;
    }
    return w + 4.0f + (float)font_width(f, FONT_LABEL, label);
}

/* Point (x, y) tourné selon rot (rotation autour du pivot ; tracking et skew ignorés) */
static SDL_FPoint rot_point(float x, float y, FontRot rot) {
    const float rad = rot.angle_deg * (3.14159265f / 180.0f), cs = cosf(rad), sn = sinf(rad);
    float dx = x - rot.pivot_x, dy = y - rot.pivot_y;
    return (SDL_FPoint){ rot.pivot_x + dx * cs - dy * sn, rot.pivot_y + dx * sn + dy * cs };
}

void ui_round_rect_rot(SDL_Renderer *r, SDL_FRect rc, float radius, FontRot rot, SDL_Color c) {
    if (c.a == 0) return;
    enum { SEG = 6 };
    SDL_FPoint p[4 * (SEG + 1)];
    if (radius > rc.w * 0.5f) radius = rc.w * 0.5f;
    if (radius > rc.h * 0.5f) radius = rc.h * 0.5f;
    const float cx[4] = { rc.x + rc.w - radius, rc.x + rc.w - radius, rc.x + radius, rc.x + radius };
    const float cy[4] = { rc.y + radius, rc.y + rc.h - radius, rc.y + rc.h - radius, rc.y + radius };
    int n = 0;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= SEG; i++) {
            float a = (-90.0f + 90.0f * k + 90.0f * i / SEG) * (3.14159265f / 180.0f);
            p[n++] = rot_point(cx[k] + cosf(a) * radius, cy[k] + sinf(a) * radius, rot);
        }
    ui_poly(r, p, n, c);
}

void ui_splash_rect(SDL_Renderer *r, SDL_FRect rc, FontRot rot, const float (*pts)[2], int n, SDL_Color c) {
    if (c.a == 0) return;
    SDL_FPoint p[48];
    if (n > 48) n = 48;
    for (int i = 0; i < n; i++) p[i] = rot_point(rc.x + rc.w * pts[i][0], rc.y + rc.h * pts[i][1], rot);
    ui_poly(r, p, n, c);
}

void ui_tags(SDL_Renderer *r, const Font *f, float x, float y, const char *const *texts, const SDL_Color *bgs, int n,
             Uint8 alpha) {
    const float sc = 15.0f / 16.0f, trk = 2.0f / sc, pad = 10.0f, h = 28.0f, lean = h * 0.5f * 0.1405f;
    for (int i = 0; i < n; i++) {
        float w = ((float)font_width_tracked(f, FONT_POP_S, texts[i], trk) + trk) * sc + 2.0f * pad;
        ui_skew_box(r, (SDL_FRect){x, y, w, h}, lean, ui_alpha(bgs[i], (Uint8)(bgs[i].a * alpha / 255)));
        const FontRot rot = { 0.0f, x + w * 0.5f, y + h * 0.5f, trk, -8.0f };
        font_draw_ink_rot(f, r, FONT_POP_S, x + pad, y + (h - font_cap_height(f, FONT_POP_S) * sc) * 0.5f, sc, rot,
                          ui_alpha(CH_WHITE, alpha), UI_NONE, UI_NONE, 0, 0, texts[i]);
        x += w;
    }
}
