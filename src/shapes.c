#include "shapes.h"
#include <math.h>
#include <stdbool.h>

#define TAU_F 6.28318530717958647692f

/* Lissage des bords : chaque forme reçoit une frange de 2 x AA px (de AA vers l'intérieur à AA vers
   l'extérieur du bord géométrique) dont l'opacité passe de pleine à nulle, via les couleurs de sommets.
   Sans elle, un bord en diagonale ou en courbe fait des escaliers (pas d'anticrénelage matériel). */
#define AA 0.5f
#define MAX_POLY 64

static void draw_strip(SDL_Renderer *r, const SDL_Vertex *verts, int columns, int rows, bool closed);

static SDL_Color clear_of(SDL_Color c) {
    c.a = 0;
    return c;
}

/* Nombre de segments pour qu'un arc de `radius` px sur `span_rad` radians paraisse rond */
static int arc_segments(float radius, float span_rad) {
    int n = (int)ceilf(fabsf(span_rad) * radius / 4.0f);   /* un segment tous les ~4 px */
    return n < 6 ? 6 : n > MAX_POLY ? MAX_POLY : n;
}

void shapes_filled_polygon(SDL_Renderer *r, const SDL_FPoint *pts, int count, SDL_Color color) {
    if (count < 3 || count > MAX_POLY) return; /* polygone convexe */

    float gx = 0.0f, gy = 0.0f;
    for (int i = 0; i < count; i++) { gx += pts[i].x; gy += pts[i].y; }
    gx /= count; gy /= count;

    /* normale extérieure de chaque arête (i -> i+1) */
    SDL_FPoint en[MAX_POLY];
    for (int i = 0; i < count; i++) {
        SDL_FPoint a = pts[i], b = pts[(i + 1) % count];
        float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
        SDL_FPoint n = len > 1e-4f ? (SDL_FPoint){ dy / len, -dx / len } : (SDL_FPoint){ 0, 0 };
        if (n.x * ((a.x + b.x) * 0.5f - gx) + n.y * ((a.y + b.y) * 0.5f - gy) < 0.0f) { n.x = -n.x; n.y = -n.y; }
        en[i] = n;
    }
    /* sommets intérieurs (opaques) et extérieurs (transparents), décalés le long de la bissectrice */
    SDL_Vertex v[2 * MAX_POLY];
    for (int i = 0; i < count; i++) {
        SDL_FPoint n0 = en[(i + count - 1) % count], n1 = en[i];
        float mx = n0.x + n1.x, my = n0.y + n1.y, ml = sqrtf(mx * mx + my * my);
        if (ml < 1e-4f) { mx = n1.x; my = n1.y; ml = 1.0f; }
        mx /= ml; my /= ml;
        float d = mx * n1.x + my * n1.y;                 /* longueur de l'onglet, bornée pour les angles aigus */
        float k = AA / (d > 0.35f ? d : 0.35f);
        v[i]         = (SDL_Vertex){ { pts[i].x - mx * k, pts[i].y - my * k }, color, {0, 0} };
        v[count + i] = (SDL_Vertex){ { pts[i].x + mx * k, pts[i].y + my * k }, clear_of(color), {0, 0} };
    }
    int idx[(MAX_POLY - 2) * 3 + MAX_POLY * 6], n = 0;
    for (int i = 1; i < count - 1; i++) { idx[n++] = 0; idx[n++] = i; idx[n++] = i + 1; }
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        idx[n++] = i; idx[n++] = j; idx[n++] = count + j;
        idx[n++] = i; idx[n++] = count + j; idx[n++] = count + i;
    }
    SDL_RenderGeometry(r, NULL, v, 2 * count, idx, n);
}

void shapes_filled_circle(SDL_Renderer *r, float cx, float cy, float radius, SDL_Color color) {
    int segs = arc_segments(radius, TAU_F);
    SDL_FPoint pts[MAX_POLY];
    for (int i = 0; i < segs; i++) {
        float a = (float)i / segs * TAU_F;
        pts[i] = (SDL_FPoint){ cx + sinf(a) * radius, cy - cosf(a) * radius };
    }
    shapes_filled_polygon(r, pts, segs, color);
}

/* Bande elliptique entre les rayons (rx - thickness) et rx, bords lissés ; closed = tour complet */
/* Bande de 4 rangées de sommets le long d'un arc d'ellipse : décalages radiaux off[] (de l'intérieur vers
   l'extérieur) et couleurs col[] ; la couleur est interpolée entre les rangées. */
static void band4(SDL_Renderer *r, float cx, float cy, float rx, float ry, float a0, float a1, bool closed,
                  const float off[4], const SDL_Color col[4]);

static void band(SDL_Renderer *r, float cx, float cy, float rx, float ry, float thickness,
                 float a0, float a1, bool closed, SDL_Color color) {
    float off[4] = { -thickness - AA, -thickness + AA, -AA, AA };   /* de l'intérieur vers l'extérieur */
    SDL_Color col[4] = { clear_of(color), color, color, clear_of(color) };
    if (thickness < 2.0f * AA) {   /* trait plus fin que la frange : centré, opacité au prorata */
        float m = -thickness * 0.5f;
        off[0] = m - AA; off[1] = off[2] = m; off[3] = m + AA;
        col[1].a = col[2].a = (Uint8)(color.a * thickness / (2.0f * AA));
    }
    band4(r, cx, cy, rx, ry, a0, a1, closed, off, col);
}

/* Trait doux centré sur l'arc : cœur plein de `core` px, puis fondu vers le transparent sur `blur` px de
   chaque côté. Même nombre de sommets qu'un contour simple (4 rangées) : seule la largeur change, donc
   coût GPU par arc identique à shapes_arc_outline (contrairement au profil néon à 12 rangées). */
void shapes_soft_arc(SDL_Renderer *r, float cx, float cy, float rx, float ry, float core, float blur,
                     float start_deg, float end_deg, SDL_Color color) {
    float h = core * 0.5f;
    const float off[4] = { -h - blur, -h, h, h + blur };
    const SDL_Color col[4] = { clear_of(color), color, color, clear_of(color) };
    band4(r, cx, cy, rx, ry, start_deg * (TAU_F / 360.0f), end_deg * (TAU_F / 360.0f), false, off, col);
}

static void band4(SDL_Renderer *r, float cx, float cy, float rx, float ry, float a0, float a1, bool closed,
                  const float off[4], const SDL_Color col[4]) {
    int segs = arc_segments(rx > ry ? rx : ry, a1 - a0);
    int cols = closed ? segs : segs + 1;
    SDL_Vertex v[(MAX_POLY + 1) * 4];
    for (int i = 0; i < cols; i++) {
        float a = a0 + (a1 - a0) * ((float)i / segs);
        float sn = sinf(a), cs = cosf(a);
        for (int k = 0; k < 4; k++)
            v[i * 4 + k] = (SDL_Vertex){ { cx + sn * (rx + off[k]), cy - cs * (ry + off[k]) }, col[k], {0, 0} };
    }
    draw_strip(r, v, cols, 4, closed);
}

void shapes_circle_outline(SDL_Renderer *r, float cx, float cy, float radius, float thickness, SDL_Color color) {
    band(r, cx, cy, radius, radius, thickness, 0.0f, TAU_F, true, color);
}

void shapes_arc_outline(SDL_Renderer *r, float cx, float cy, float rx, float ry, float thickness, float start_deg, float end_deg, SDL_Color color) {
    band(r, cx, cy, rx, ry, thickness, start_deg * (TAU_F / 360.0f), end_deg * (TAU_F / 360.0f), false, color);
}

void shapes_filled_triangle(SDL_Renderer *r, float cx, float cy, float radius, float rotation_rad, SDL_Color color) {
    SDL_FPoint pts[3];
    for (int i = 0; i < 3; i++) {
        float a = rotation_rad + (float)i / 3.0f * TAU_F;
        pts[i] = (SDL_FPoint){ cx + sinf(a) * radius, cy - cosf(a) * radius };
    }
    shapes_filled_polygon(r, pts, 3, color);
}

void shapes_thick_line(SDL_Renderer *r, float x1, float y1, float x2, float y2, float thickness, SDL_Color color) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len * thickness * 0.5f;
    float ny =  dx / len * thickness * 0.5f;
    SDL_FPoint quad[4] = {
        { x1 + nx, y1 + ny }, { x2 + nx, y2 + ny },
        { x2 - nx, y2 - ny }, { x1 - nx, y1 - ny },
    };
    shapes_filled_polygon(r, quad, 4, color);
}

/* Profil néon en travers d'un trait : NEON_ROWS décalages (du côté intérieur vers l'extérieur) et leurs
   couleurs. Bord net du cœur (adouci sur 1 px) puis échantillons d'un halo gaussien. */
enum { NEON_HALF_BANDS = 6, NEON_ROWS = 2 * NEON_HALF_BANDS };
static void neon_profile(float core_half, float glow_width, float glow_peak, SDL_Color core, SDL_Color glow,
                         float offset[NEON_ROWS], SDL_Color color[NEON_ROWS]) {
    const float outer[NEON_HALF_BANDS] = {
        core_half - 0.5f, core_half + 0.5f, glow_width * 0.7f, glow_width * 1.2f, glow_width * 1.8f, glow_width * 3.0f
    };
    for (int i = 0; i < NEON_HALF_BANDS; i++) {
        float d = outer[i], g = d / glow_width;
        SDL_Color c = glow;
        c.a = (Uint8)(glow.a * glow_peak * expf(-g * g));
        if (i == 0) c = core;                       /* intérieur du trait : opaque */
        offset[NEON_HALF_BANDS - 1 - i] = -d; color[NEON_HALF_BANDS - 1 - i] = c;
        offset[NEON_HALF_BANDS + i]     =  d; color[NEON_HALF_BANDS + i]     = c;
    }
}

/* Relie des colonnes de `rows` sommets (colonne i = sommets i*rows .. i*rows+rows-1) en bandes */
static void draw_strip(SDL_Renderer *r, const SDL_Vertex *verts, int columns, int rows, bool closed) {
    enum { MAX_INDICES = 64 * (NEON_ROWS - 1) * 6 };
    int indices[MAX_INDICES], n = 0;
    int segs = closed ? columns : columns - 1;
    for (int i = 0; i < segs && n + (rows - 1) * 6 <= MAX_INDICES; i++)
        for (int k = 0; k < rows - 1; k++) {
            int p = i * rows + k, q = ((i + 1) % columns) * rows + k;
            indices[n++] = p; indices[n++] = q;     indices[n++] = q + 1;
            indices[n++] = p; indices[n++] = q + 1; indices[n++] = p + 1;
        }
    SDL_RenderGeometry(r, NULL, verts, columns * rows, indices, n);
}

void shapes_neon_arc(SDL_Renderer *r, float cx, float cy, float rx, float ry, float start_deg, float end_deg,
                     float core_half, float glow_width, float glow_peak, SDL_Color core, SDL_Color glow) {
    enum { ROWS = NEON_ROWS };
    float offset[ROWS];
    SDL_Color color[ROWS];
    neon_profile(core_half, glow_width, glow_peak, core, glow, offset, color);
    const int SEGMENTS = arc_segments((rx > ry ? rx : ry) + glow_width, (end_deg - start_deg) * (TAU_F / 360.0f));

    SDL_Vertex verts[ROWS * (MAX_POLY + 1)];
    float a0 = start_deg * (TAU_F / 360.0f), a1 = end_deg * (TAU_F / 360.0f);
    for (int i = 0; i <= SEGMENTS; i++) {
        float a = a0 + (a1 - a0) * ((float)i / SEGMENTS);
        float s = sinf(a), c = cosf(a);
        for (int k = 0; k < ROWS; k++) {
            SDL_Vertex *v = &verts[i * ROWS + k];
            v->position = (SDL_FPoint){ cx + s * (rx + offset[k]), cy - c * (ry + offset[k]) };
            v->color = color[k];
            v->tex_coord = (SDL_FPoint){0, 0};
        }
    }
    draw_strip(r, verts, SEGMENTS + 1, ROWS, false);
}

/* Contour d'un rectangle arrondi décalé de `offset` vers l'extérieur : RR_POINTS points, sens horaire */
enum { RR_CORNER_STEPS = 6, RR_POINTS = 4 * (RR_CORNER_STEPS + 1) };
static SDL_FPoint rounded_rect_point(SDL_FRect rc, float radius, int i, float offset) {
    int corner = i / (RR_CORNER_STEPS + 1), step = i % (RR_CORNER_STEPS + 1);
    /* centres des coins : haut-droit, bas-droit, bas-gauche, haut-gauche */
    const float cxs[4] = { rc.x + rc.w - radius, rc.x + rc.w - radius, rc.x + radius, rc.x + radius };
    const float cys[4] = { rc.y + radius, rc.y + rc.h - radius, rc.y + rc.h - radius, rc.y + radius };
    float a = (corner + (float)step / RR_CORNER_STEPS) * (TAU_F / 4.0f); /* 0 = haut, sens horaire */
    float rr = radius + offset;
    if (rr < 0.0f) rr = 0.0f;
    return (SDL_FPoint){ cxs[corner] + sinf(a) * rr, cys[corner] - cosf(a) * rr };
}

void shapes_rounded_rect(SDL_Renderer *r, SDL_FRect rect, float radius, SDL_Color color) {
    /* éventail plein jusqu'à AA à l'intérieur du bord, puis frange qui s'efface jusqu'à AA à l'extérieur */
    SDL_Vertex verts[1 + 2 * RR_POINTS];
    int indices[RR_POINTS * 9], n = 0;
    verts[0] = (SDL_Vertex){ { rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f }, color, {0, 0} };
    for (int i = 0; i < RR_POINTS; i++) {
        verts[1 + i] = (SDL_Vertex){ rounded_rect_point(rect, radius, i, -AA), color, {0, 0} };
        verts[1 + RR_POINTS + i] = (SDL_Vertex){ rounded_rect_point(rect, radius, i, AA), clear_of(color), {0, 0} };
    }
    for (int i = 0; i < RR_POINTS; i++) {
        int a = 1 + i, b = 1 + (i + 1) % RR_POINTS, oa = a + RR_POINTS, ob = b + RR_POINTS;
        indices[n++] = 0; indices[n++] = a;  indices[n++] = b;
        indices[n++] = a; indices[n++] = b;  indices[n++] = ob;
        indices[n++] = a; indices[n++] = ob; indices[n++] = oa;
    }
    SDL_RenderGeometry(r, NULL, verts, 1 + 2 * RR_POINTS, indices, n);
}

void shapes_rounded_rect_neon(SDL_Renderer *r, SDL_FRect rect, float radius, float core_half, float glow_width,
                              float glow_peak, SDL_Color core, SDL_Color glow) {
    float offset[NEON_ROWS];
    SDL_Color color[NEON_ROWS];
    neon_profile(core_half, glow_width, glow_peak, core, glow, offset, color);
    SDL_Vertex verts[RR_POINTS * NEON_ROWS];
    for (int i = 0; i < RR_POINTS; i++)
        for (int k = 0; k < NEON_ROWS; k++)
            verts[i * NEON_ROWS + k] = (SDL_Vertex){ rounded_rect_point(rect, radius, i, offset[k]), color[k], {0, 0} };
    draw_strip(r, verts, RR_POINTS, NEON_ROWS, true);
}

