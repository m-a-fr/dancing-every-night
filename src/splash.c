#include "splash.h"
#include "config.h"
#include "shapes.h"
#include "ui.h"
#include <stdio.h>
#include "third_party/stb_image.h"

/* Barre : à la place et à la hauteur de la pastille « PRESS ✕ TO START » de l'écran titre */
#define BAR_W   240.0f
#define BAR_H    34.0f
#define BAR_CY  (SCREEN_H - 14.0f - 17.0f)
#define BAR_PAD   7.0f

void splash_init(Splash *s, SDL_Renderer *r) {
    *s = (Splash){0};
    int w, h, n;
    unsigned char *px = stbi_load(ASSET_PATH("ui/splash.jpg"), &w, &h, &n, 4);
    if (!px) { fprintf(stderr, "splash: image introuvable\n"); return; }
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (surf) {
        s->image = SDL_CreateTextureFromSurface(r, surf);
        SDL_FreeSurface(surf);
    }
    stbi_image_free(px);
}

void splash_show(const Splash *s, SDL_Renderer *r, float progress) {
    splash_show_label(s, r, progress, NULL, NULL);
}

void splash_show_label(const Splash *s, SDL_Renderer *r, float progress, const Font *font, const char *label) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, 255);
    SDL_RenderClear(r);
    if (s->image) SDL_RenderCopy(r, s->image, NULL, NULL);

    /* pastille d'encre cerclée de blanc (comme celle du titre), remplie de rose au fil des chargements */
    const float x = SCREEN_W * 0.5f - BAR_W * 0.5f, y = BAR_CY - BAR_H * 0.5f;
    shapes_rounded_rect(r, (SDL_FRect){x - 1.5f, y - 1.5f, BAR_W + 3.0f, BAR_H + 3.0f}, BAR_H * 0.5f + 1.5f,
                        ui_alpha(CH_WHITE, 102));
    shapes_rounded_rect(r, (SDL_FRect){x, y, BAR_W, BAR_H}, BAR_H * 0.5f, CH_INK);
    const float inner_h = BAR_H - 2.0f * BAR_PAD, inner_w = BAR_W - 2.0f * BAR_PAD;
    float fill = inner_h + (inner_w - inner_h) * progress;   /* au moins une pastille ronde dès le départ */
    shapes_rounded_rect(r, (SDL_FRect){x + BAR_PAD, y + BAR_PAD, fill, inner_h}, inner_h * 0.5f, CH_PINK);
    if (font && label)
        font_draw(font, r, FONT_LABEL, SCREEN_W * 0.5f, y - 12.0f - (float)font_cap_height(font, FONT_LABEL),
                  ALIGN_CENTER, CH_WHITE, label);
    SDL_RenderPresent(r);
}

void splash_shutdown(Splash *s) {
    if (s->image) SDL_DestroyTexture(s->image);
    s->image = NULL;
}
