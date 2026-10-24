/* Inclusion portable de SDL2 (desktop / Vita). Toujours inclure ce fichier. */
#ifndef SDL_WRAPPER_H
#define SDL_WRAPPER_H
#if defined(__has_include)
#  if __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#  else
#    include <SDL.h>
#  endif
#else
#  include <SDL2/SDL.h>
#endif

/* Mesure de performance : les appels de rendu passent par les compteurs de perf.c (voir perf.h) */
#ifndef PERF_NO_WRAP
#define SDL_RenderCopy                 perf_RenderCopy
#define SDL_RenderCopyF                perf_RenderCopyF
#define SDL_RenderCopyExF              perf_RenderCopyExF
#define SDL_RenderGeometry             perf_RenderGeometry
#define SDL_RenderFillRect             perf_RenderFillRect
#define SDL_RenderDrawLine             perf_RenderDrawLine
#define SDL_CreateTexture              perf_CreateTexture
#define SDL_CreateTextureFromSurface   perf_CreateTextureFromSurface
#define SDL_DestroyTexture             perf_DestroyTexture
#include "perf_wrap.h"
#endif
#endif
