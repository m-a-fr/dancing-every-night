#ifndef HOSTESS_H
#define HOSTESS_H
#include <stdbool.h>
#include "sdl.h"

/* L'hôtesse : une image par moment du jeu (rangs de fin de partie, chargement, pause), chargées au démarrage depuis
   assets/ui/hostess/<nom>.png. Le joueur peut en remplacer une en posant son PNG (RGBA, côté ≤ 2048 px) dans
   custom/ui/hostess/ à côté de ses sauvegardes (CUSTOM_DIR, voir asset_resolve) ; une image absente n'est pas dessinée. */
typedef enum { HOST_RANK_S, HOST_RANK_A, HOST_RANK_B, HOST_RANK_CD, HOST_LOADING, HOST_PAUSE, HOST_COUNT } HostPose;

typedef struct {
    SDL_Texture *tex[HOST_COUNT];
    int w[HOST_COUNT], h[HOST_COUNT];
} Hostess;

void hostess_init(Hostess *h, SDL_Renderer *r);
void hostess_shutdown(Hostess *h);
/* Dessine la pose ancrée en bas de l'écran, centrée en cx, de hauteur height (px écran) ; pop : 0..1 de l'apparition
   (0 = hors cadre à droite et transparente, 1 = en place) ; no-op si l'image manque */
void hostess_draw(const Hostess *h, SDL_Renderer *r, HostPose pose, float cx, float height, float pop);

#endif
