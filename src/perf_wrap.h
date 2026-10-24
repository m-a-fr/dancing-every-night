#ifndef PERF_WRAP_H
#define PERF_WRAP_H
/* Prototypes des fonctions SDL comptées par perf.c ; inclus par sdl.h, qui redirige les appels SDL vers elles. */
/* Remplaçants comptés des fonctions SDL (voir sdl.h) */
int perf_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d);
int perf_RenderCopyF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_FRect *d);
int perf_RenderCopyExF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_FRect *d,
                       double angle, const SDL_FPoint *c, SDL_RendererFlip flip);
int perf_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int nv, const int *idx, int ni);
int perf_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect);
int perf_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2);
SDL_Texture *perf_CreateTexture(SDL_Renderer *r, Uint32 fmt, int access, int w, int h);
SDL_Texture *perf_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s);
void perf_DestroyTexture(SDL_Texture *t);

#endif
