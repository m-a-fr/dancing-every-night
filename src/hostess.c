#include "hostess.h"
#include "config.h"
#include "files.h"
#include <stdio.h>
#include "third_party/stb_image.h"

#define HOST_MAX_SIDE 2048   /* limite de texture de la Vita */

static const char *const HOST_FILES[HOST_COUNT] = {
    "rank_s", "rank_a", "rank_b", "rank_cd", "loading", "pause",
};

static SDL_Texture *load_pose(SDL_Renderer *r, const char *name, int *ow, int *oh) {
    char rel[64], path[192];
    snprintf(rel, sizeof rel, "ui/hostess/%s.png", name);
    asset_resolve(rel, path, sizeof path);
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) return NULL;
    if (w > HOST_MAX_SIDE || h > HOST_MAX_SIDE) {
        fprintf(stderr, "hostess: '%s' ignorée (%dx%d, maximum %d)\n", path, w, h, HOST_MAX_SIDE);
        stbi_image_free(px);
        return NULL;
    }
    SDL_Texture *t = NULL;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   /* bilinéaire : l'image est réduite à l'affichage */
        t = SDL_CreateTextureFromSurface(r, s);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_FreeSurface(s);
    }
    stbi_image_free(px);
    if (t) { SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); *ow = w; *oh = h; }
    return t;
}

void hostess_init(Hostess *h, SDL_Renderer *r) {
    *h = (Hostess){0};
    for (int i = 0; i < HOST_COUNT; i++) h->tex[i] = load_pose(r, HOST_FILES[i], &h->w[i], &h->h[i]);
}

void hostess_shutdown(Hostess *h) {
    for (int i = 0; i < HOST_COUNT; i++)
        if (h->tex[i]) { SDL_DestroyTexture(h->tex[i]); h->tex[i] = NULL; }
}

void hostess_draw(const Hostess *h, SDL_Renderer *r, HostPose pose, float cx, float height, float pop) {
    if (pose < 0 || pose >= HOST_COUNT || !h->tex[pose]) return;
    float w = height * (float)h->w[pose] / (float)h->h[pose];
    float x = cx - w * 0.5f + (1.0f - pop) * 80.0f;
    SDL_FRect d = { x, (float)SCREEN_H - height, w, height };
    SDL_SetTextureAlphaMod(h->tex[pose], (Uint8)(255.0f * (pop < 0.0f ? 0.0f : pop > 1.0f ? 1.0f : pop)));
    SDL_RenderCopyF(r, h->tex[pose], NULL, &d);
}
