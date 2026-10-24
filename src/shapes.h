#ifndef SHAPES_H
#define SHAPES_H
#include "sdl.h"

/* Primitives de dessin en formes pleines (pas de sprites en v0.1).
   Coordonnées écran, espace logique 960x544 (voir config.h). */

void shapes_filled_polygon(SDL_Renderer *r, const SDL_FPoint *pts, int count, SDL_Color color);
void shapes_filled_circle(SDL_Renderer *r, float cx, float cy, float radius, SDL_Color color);
void shapes_circle_outline(SDL_Renderer *r, float cx, float cy, float radius, float thickness, SDL_Color color);
/* Portion d'anneau elliptique entre deux angles en degrés (0 = midi, sens
   horaire). rx/ry permettent un arc étiré (ex. plus grand en hauteur). */
void shapes_arc_outline(SDL_Renderer *r, float cx, float cy, float rx, float ry, float thickness, float start_deg, float end_deg, SDL_Color color);
/* Trait doux centré sur l'arc : cœur plein de `core` px + fondu de `blur` px de chaque côté. Aussi peu coûteux
   que shapes_arc_outline (4 rangées de sommets) : utilisable pour des formes redessinées à chaque note. */
void shapes_soft_arc(SDL_Renderer *r, float cx, float cy, float rx, float ry, float core, float blur, float start_deg, float end_deg, SDL_Color color);
/* Arc elliptique néon centré sur (rx, ry) : trait net de demi-épaisseur core_half (couleur core) et
   halo gaussien de largeur glow_width (couleur glow, opacité glow_peak au ras du trait) des deux côtés.
   Même profil que le liseré des cibles (sprites.c). Angles comme shapes_arc_outline. */
void shapes_neon_arc(SDL_Renderer *r, float cx, float cy, float rx, float ry, float start_deg, float end_deg,
                     float core_half, float glow_width, float glow_peak, SDL_Color core, SDL_Color glow);
/* Rectangle à coins arrondis, plein */
void shapes_rounded_rect(SDL_Renderer *r, SDL_FRect rect, float radius, SDL_Color color);
/* Bord néon d'un rectangle arrondi, centré sur le bord du rectangle (même profil que shapes_neon_arc).
   glow_peak = 0 : simple trait net d'épaisseur 2 * core_half. */
void shapes_rounded_rect_neon(SDL_Renderer *r, SDL_FRect rect, float radius, float core_half, float glow_width,
                              float glow_peak, SDL_Color core, SDL_Color glow);
/* rotation_rad : 0 = la pointe regarde vers le haut, augmente dans le sens horaire */
void shapes_filled_triangle(SDL_Renderer *r, float cx, float cy, float radius, float rotation_rad, SDL_Color color);
void shapes_thick_line(SDL_Renderer *r, float x1, float y1, float x2, float y2, float thickness, SDL_Color color);

#endif
