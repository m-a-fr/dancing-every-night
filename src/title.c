#include "title.h"
#include "config.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include "third_party/stb_image.h"

#define TITLE_BPM     120.0f
#define FACET_SIZE    58.0f      /* = FACET dans tools/title_assets.py */
#define FACET_H       (FACET_SIZE * 0.866f)
/* Vitesses de défilement (px/s) et largeur affichée des bandes (image + son miroir) */
#define FAR_SPEED     28.0f
#define DOTS_SPEED    40.0f
#define NEAR_SPEED    64.0f
#define SPARK_SPEED   110.0f
#define DOTS_PITCH    12.0f
/* Position des calques (px écran), d'après la maquette */
#define FAR_Y         126.0f
#define DOTS_Y        364.0f
#define NEAR_Y        259.0f
#define LOGO_CX       480.0f     /* centre du battement du logo */
#define LOGO_CY       270.0f

static const SDL_Color FACET_PAL[8] = {
    {59, 31, 168, 255}, {106, 63, 209, 255}, {154, 124, 240, 255}, {201, 182, 255, 255},
    {69, 200, 232, 255}, {46, 143, 214, 255}, {255, 46, 138, 255}, {208, 75, 200, 255},
};
/* Étoiles : hauteur, taille */
static const float SPARKS[6][2] = { {90, 26}, {330, 18}, {150, 22}, {410, 20}, {60, 16}, {240, 24} };

static SDL_Texture *load_png(SDL_Renderer *r, const char *path) {
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) { fprintf(stderr, "title: impossible de charger '%s'\n", path); return NULL; }
    SDL_Texture *t = NULL;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   /* bilinéaire : battements = petites mises à l'échelle */
        t = SDL_CreateTextureFromSurface(r, s);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_FreeSurface(s);
    }
    stbi_image_free(px);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

bool title_init(Title *t, SDL_Renderer *r) {
    *t = (Title){0};
    t->facets   = load_png(r, ASSET_PATH("ui/title/facets.png"));
    t->far      = load_png(r, ASSET_PATH("ui/title/far.png"));
    t->near     = load_png(r, ASSET_PATH("ui/title/near.png"));
    t->dots     = load_png(r, ASSET_PATH("ui/title/dots.png"));
    t->vignette = load_png(r, ASSET_PATH("ui/title/vignette.png"));
    t->logo     = load_png(r, ASSET_PATH("ui/title/logo.png"));
    t->tri      = load_png(r, ASSET_PATH("ui/title/tri.png"));
    t->beam     = load_png(r, ASSET_PATH("ui/title/beam.png"));
    t->spark    = load_png(r, ASSET_PATH("ui/title/spark.png"));
    return t->facets && t->logo;
}

void title_shutdown(Title *t) {
    SDL_Texture **all[] = { &t->facets, &t->far, &t->near, &t->dots, &t->vignette, &t->logo,
                            &t->tri, &t->beam, &t->spark };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
        if (*all[i]) { SDL_DestroyTexture(*all[i]); *all[i] = NULL; }
}

void title_update(Title *t, float dt) {
    t->dist += dt;   /* les décalages sont pris modulo la période de chaque calque au dessin */
}

static float hash2(float a, float b) {
    float x = sinf(a * 127.1f + b * 311.7f) * 43758.5453f;
    return x - floorf(x);
}

static int tex_w(SDL_Texture *tx) { int w = 0; if (tx) SDL_QueryTexture(tx, NULL, NULL, &w, NULL); return w; }
static int tex_h(SDL_Texture *tx) { int h = 0; if (tx) SDL_QueryTexture(tx, NULL, NULL, NULL, &h); return h; }

/* Bande infinie : l'image puis son miroir, répétés ; off = décalage en px dans la période (2 x largeur) */
static void draw_strip(SDL_Renderer *r, SDL_Texture *tx, float y, float off, Uint8 bright, Uint8 alpha) {
    if (!tx) return;
    float w = (float)tex_w(tx), h = (float)tex_h(tx);
    SDL_SetTextureColorMod(tx, bright, bright, bright);
    SDL_SetTextureAlphaMod(tx, alpha);
    float x = -fmodf(off, 2.0f * w);
    for (int i = 0; x < SCREEN_W; i++, x += w) {
        SDL_FRect d = { x, y, w, h };
        SDL_RenderCopyExF(r, tx, NULL, &d, 0.0, NULL, (i & 1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }
}

void title_draw(const Title *t, SDL_Renderer *r, const Font *font, const Sprites *sprites, float time, bool prompt) {
    /* Battement : montée de 40 ms puis retombée douce ; temps fort 1 temps sur 4 */
    float beats = time * TITLE_BPM / 60.0f;
    int   n = (int)floorf(beats);
    float ph = beats - (float)n;
    float env = ph < 0.08f ? sinf(ph / 0.08f * 1.5707963f) : expf(-(ph - 0.08f) * 4.5f);
    float acc = env * (n % 4 == 0 ? 1.0f : 0.55f);

    /* 1. Facettes fixes */
    if (t->facets) SDL_RenderCopy(r, t->facets, NULL, NULL);

    /* 2. Scintillement : quelques facettes s'allument sur le temps (même découpage que l'image) */
    if (t->tri) {
        SDL_SetTextureBlendMode(t->tri, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(t->tri, (Uint8)(env * 0.35f * 255.0f));
        for (int row = -1; row < (int)(SCREEN_H / FACET_H) + 2; row++)
            for (int col = -1; col < (int)(SCREEN_W / FACET_SIZE) + 3; col++)
                for (int k = 0; k < 2; k++) {
                    if (hash2((float)(col * 2 + k + n), (float)row) <= 0.95f) continue;
                    SDL_Color c = FACET_PAL[(int)(hash2((float)(col * 2 + k), (float)row) * 8.0f) & 7];
                    SDL_SetTextureColorMod(t->tri, c.r, c.g, c.b);
                    float x0 = col * FACET_SIZE + ((row % 2 + 2) % 2) * FACET_SIZE * 0.5f, y0 = row * FACET_H;
                    SDL_FRect d = { k ? x0 + FACET_SIZE * 0.5f : x0, y0, FACET_SIZE, FACET_H };
                    SDL_RenderCopyExF(r, t->tri, NULL, &d, 0.0, NULL, k ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
                }
    }

    /* 3. Faisceau qui balaie, autour de son sommet en haut de l'écran */
    if (t->beam) {
        SDL_FRect d = { LOGO_CX - tex_w(t->beam) * 0.5f, -40.0f, (float)tex_w(t->beam), (float)tex_h(t->beam) };
        SDL_FPoint c = { tex_w(t->beam) * 0.5f, 0.0f };
        SDL_RenderCopyExF(r, t->beam, NULL, &d, sinf(time * 0.35f) * 0.6f * 57.29578f, &c, SDL_FLIP_NONE);
    }

    /* 4-6. Ville lointaine, trame, ville proche : défilement infini ; les villes s'éclairent sur le temps fort */
    draw_strip(r, t->far, FAR_Y, t->dist * FAR_SPEED, (Uint8)(225.0f + 30.0f * acc), 230);
    if (t->dots) {
        SDL_FRect d = { -fmodf(t->dist * DOTS_SPEED, DOTS_PITCH), DOTS_Y, (float)tex_w(t->dots), (float)tex_h(t->dots) };
        SDL_RenderCopyF(r, t->dots, NULL, &d);
    }
    draw_strip(r, t->near, NEAR_Y, t->dist * NEAR_SPEED, (Uint8)(235.0f + 20.0f * acc), 255);
    if (t->vignette) SDL_RenderCopy(r, t->vignette, NULL, NULL);

    /* 7. Étoiles qui traversent l'écran */
    if (t->spark) {
        SDL_SetTextureColorMod(t->spark, CH_GOLD.r, CH_GOLD.g, CH_GOLD.b);
        SDL_SetTextureAlphaMod(t->spark, (Uint8)((0.55f + 0.45f * acc) * 255.0f));
        for (int i = 0; i < 6; i++) {
            float span = 1100.0f, speed = SPARK_SPEED * (0.8f + (i % 3) * 0.2f);
            float x = fmodf(fmodf(i * 197.0f - t->dist * speed, span) + span, span) - 70.0f;
            float s = SPARKS[i][1] * (0.75f + acc * 0.35f);
            SDL_FRect d = { x + SPARKS[i][1] * 0.5f - s * 0.5f, SPARKS[i][0] + SPARKS[i][1] * 0.5f - s * 0.5f, s, s };
            SDL_RenderCopyExF(r, t->spark, NULL, &d, acc * 15.0, NULL, SDL_FLIP_NONE);
        }
    }

    /* 8. Logo : le bandeau-titre bat, centré sur l'écran */
    float s = 1.0f + acc * 0.018f;
    if (t->logo) {
        float w = (float)tex_w(t->logo), h = (float)tex_h(t->logo), ls = s * (1.0f + acc * 0.014f);
        float x = ((float)SCREEN_W - w) * 0.5f, y = ((float)SCREEN_H - h) * 0.5f, ox = x + w * 0.5f, oy = y + h * 0.55f;   /* son propre centre de battement */
        float gx = LOGO_CX + (ox - LOGO_CX) * s, gy = LOGO_CY + (oy - LOGO_CY) * s;
        SDL_FRect d = { gx - w * 0.5f * ls, gy - h * 0.55f * ls, w * ls, h * ls };
        SDL_RenderCopyF(r, t->logo, NULL, &d);
    }

    /* 9. Numéro de version, discret en bas à droite (les signalements de bug le demandent) */
    const FontFace face = FONT_LABEL;
    const float cap = (float)font_cap_height(font, face);
    font_draw(font, r, face, SCREEN_W - 14.0f, SCREEN_H - 14.0f - cap, ALIGN_RIGHT, ui_alpha(CH_WHITE, 140),
              "v" GAME_VERSION);

    /* 10. Invitation : pastille d'encre, « PRESS ✕ TO START » */
    if (!prompt) return;
    const float icon = 22.0f, gap = 10.0f;
    float w1 = (float)font_width(font, face, "PRESS"), w2 = (float)font_width(font, face, "TO START");
    float total = w1 + gap + icon + gap + w2, cy = SCREEN_H - 14.0f - 17.0f;
    SDL_FRect pill = { SCREEN_W * 0.5f - total * 0.5f - 18.0f, cy - 17.0f, total + 36.0f, 34.0f };
    ui_panel(r, pill, 17.0f, CH_INK, ui_alpha(CH_WHITE, 102), 0.0f);
    float x = SCREEN_W * 0.5f - total * 0.5f;
    font_draw(font, r, face, x, cy - cap * 0.5f, ALIGN_LEFT, CH_WHITE, "PRESS");
    sprite_draw_icon(sprites, r, BTN_CROSS, x + w1 + gap + icon * 0.5f, cy, icon, 255);
    font_draw(font, r, face, x + w1 + gap + icon + gap, cy - cap * 0.5f, ALIGN_LEFT, CH_WHITE, "TO START");
}
