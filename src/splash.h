#ifndef SPLASH_H
#define SPLASH_H
#include "sdl.h"
#include "font.h"

/* Écran de démarrage : affiché dès que le moteur de rendu existe, pendant les chargements du lancement (polices,
   images, liste des cartes, sons), qui laissaient l'écran noir plusieurs secondes sur Vita. Une seule image
   (assets/ui/splash.jpg, la scène de l'écran titre figée, générée par tools/splash_image.py) et une barre de
   progression à la place de « PRESS ✕ TO START » ; l'écran titre prend ensuite le relais à l'identique. */
typedef struct {
    SDL_Texture *image;   /* NULL si l'image manque : la barre s'affiche seule sur fond uni */
} Splash;

void splash_init(Splash *s, SDL_Renderer *r);
/* Dessine l'écran avec la barre remplie à `progress` (0..1) et l'affiche tout de suite */
void splash_show(const Splash *s, SDL_Renderer *r, float progress);
/* Même chose avec une ligne de texte au-dessus de la barre (ex. « IMPORTING MAPS 1 / 3 ») */
void splash_show_label(const Splash *s, SDL_Renderer *r, float progress, const Font *font, const char *label);
void splash_shutdown(Splash *s);

#endif
