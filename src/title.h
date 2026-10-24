#ifndef TITLE_H
#define TITLE_H
#include <stdbool.h>
#include "sdl.h"
#include "font.h"
#include "sprites.h"

/* Écran d'accueil « Cascade pop » (maquette validée le 15/10/2026, voir GAME_DESIGN.md) : fond de facettes fixe
   qui scintille, deux plans de ville et une trame qui défilent à l'infini, logo en trois couches (boule disco,
   hôtesse, bandeau-titre) qui battent à 120 BPM. Images préparées par tools/title_assets.py. */

typedef struct {
    SDL_Texture *facets, *far, *near, *dots, *vignette;
    SDL_Texture *ball, *girl, *logo;
    SDL_Texture *tri, *beam, *spark;
    float dist;                  /* secondes de défilement écoulées (multipliées par la vitesse de chaque calque) */
} Title;

bool title_init(Title *t, SDL_Renderer *r);
void title_shutdown(Title *t);
void title_update(Title *t, float dt);
/* time : horloge des animations (secondes), cale les battements ; prompt = false : scène seule, sans « PRESS ✕ TO
   START » (fond du menu principal et des options) */
void title_draw(const Title *t, SDL_Renderer *r, const Font *font, const Sprites *sprites, float time, bool prompt);

#endif
