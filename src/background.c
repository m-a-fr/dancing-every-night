#include "background.h"
#include "config.h"
#include <stdio.h>
#include "third_party/stb_image.h"

#define IMAGE_FADE_S 0.35f
#define VIDEO_FADE_S 0.50f

bool background_from_rgba(Background *b, SDL_Renderer *r, const unsigned char *px, int w, int h, int dim) {
    *b = (Background){0};
    b->dim = dim;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom((void *)px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (surf) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        b->tex = SDL_CreateTextureFromSurface(r, surf);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_FreeSurface(surf);
    }
    if (!b->tex) { fprintf(stderr, "background: texture: %s\n", SDL_GetError()); return false; }
    SDL_SetTextureBlendMode(b->tex, SDL_BLENDMODE_BLEND);
    b->tex_w = w;
    b->tex_h = h;
    return true;
}

bool background_load(Background *b, SDL_Renderer *r, const char *image_path, int dim) {
    *b = (Background){0};
    b->dim = dim;
    if (!image_path || !image_path[0]) return false;

    int w, h, n;
    unsigned char *px = stbi_load(image_path, &w, &h, &n, 4);
    if (!px) { fprintf(stderr, "background: impossible de charger '%s'\n", image_path); return false; }
    bool ok = background_from_rgba(b, r, px, w, h, dim);
    stbi_image_free(px);
    if (ok) fprintf(stderr, "background: '%s' (%dx%d)\n", image_path, w, h);
    return ok;
}

bool background_load_video(Background *b, SDL_Renderer *r, const char *video_path, int video_offset_ms) {
    b->video_offset_ms = video_offset_ms;
    if (!video_open(&b->video, r, video_path)) return false;
    SDL_SetTextureBlendMode(b->video.tex, SDL_BLENDMODE_BLEND);
    return true;
}

void background_free(Background *b) {
    video_close(&b->video);
    if (b->tex) SDL_DestroyTexture(b->tex);
    *b = (Background){0};
}

void background_seek(Background *b, float song_ms) {
    video_seek(&b->video, song_ms - (float)b->video_offset_ms);
}

void background_update(Background *b, float dt) {
    video_update(&b->video);
    if (b->tex && b->image_fade < 1.0f) {
        b->image_fade += dt / IMAGE_FADE_S;
        if (b->image_fade > 1.0f) b->image_fade = 1.0f;
    }
    if (b->video.has_frame && b->video_fade < 1.0f) {
        b->video_fade += dt / VIDEO_FADE_S;
        if (b->video_fade > 1.0f) b->video_fade = 1.0f;
    }
}

/* "cover" : on recadre la source pour remplir 960x544 sans déformer */
static void draw_cover(SDL_Renderer *r, SDL_Texture *tex, int tw, int th, float alpha) {
    SDL_Rect src = { 0, 0, tw, th };
    float tex_aspect = (float)tw / (float)th;
    float scr_aspect = (float)SCREEN_W / (float)SCREEN_H;
    if (tex_aspect > scr_aspect) {
        src.w = (int)((float)th * scr_aspect);
        src.x = (tw - src.w) / 2;
    } else {
        src.h = (int)((float)tw / scr_aspect);
        src.y = (th - src.h) / 2;
    }
    SDL_SetTextureAlphaMod(tex, (Uint8)(alpha * 255.0f));
    SDL_RenderCopy(r, tex, &src, NULL);
}

void background_draw(const Background *b, SDL_Renderer *r) {
    background_draw_ex(b, r, true);
}

void background_draw_faded(const Background *b, SDL_Renderer *r, float mult) {
    if (mult <= 0.0f) return;
    if (b->tex && b->image_fade > 0.0f) draw_cover(r, b->tex, b->tex_w, b->tex_h, b->image_fade * mult);
    if (b->video.has_frame && b->video_fade > 0.0f) draw_cover(r, b->video.tex, b->video.w, b->video.h, b->video_fade * mult);
}

void background_draw_ex(const Background *b, SDL_Renderer *r, bool dim) {
    bool any = false;
    if (b->tex && b->image_fade > 0.0f) {
        draw_cover(r, b->tex, b->tex_w, b->tex_h, b->image_fade);
        any = true;
    }
    if (b->video.has_frame && b->video_fade > 0.0f) {
        draw_cover(r, b->video.tex, b->video.w, b->video.h, b->video_fade);
        any = true;
    }
    if (any && dim && b->dim > 0) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)(b->dim * 255 / 100));
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderFillRect(r, &full);
    }
}
