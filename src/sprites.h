#ifndef SPRITES_H
#define SPRITES_H
#include <stdbool.h>
#include "sdl.h"
#include "input.h"

/* Sprites des cibles et des notes : atlas assets/ui/pad_icons.png (+ _64 et _32, mêmes icônes plus petites),
   générés par tools/svg2atlas.py depuis art/pad_icons/. Ligne 0 = cibles, ligne 1 = notes,
   colonnes = lanes (TRIANGLE, CIRCLE, CROSS, UP, LEFT, DOWN). */
/* Deux néons : turquoise pour la croix directionnelle (arc gauche), magenta pour les boutons face
   (arc droit), pour distinguer les deux côtés d'un coup d'œil. */
typedef enum { NEON_TEAL, NEON_MAGENTA, NEON_COLOR_COUNT } NeonColor;

typedef struct {
    SDL_Texture *atlas[3];      /* cellules 128, 64, 32 px : on dessine avec la plus proche de la taille affichée */
    SDL_Texture *ring[NEON_COLOR_COUNT];   /* liseré néon des cibles, généré par code à l'init (pas un asset) */
    SDL_Texture *note_disc;     /* disque plein des notes, bords lissés (blanc, teinté au dessin) */
    SDL_Texture *note_border;   /* contour des notes, bords lissés (blanc, teinté au dessin) */
    SDL_Texture *arc[2];        /* arcs « Cascade pop » (0 = gauche, 1 = droite), générés par sprites_build_arcs */
    SDL_FRect    arc_dst[2];    /* leur place à l'écran (1 texel = 1 px) */
} Sprites;

/* Disque de fond des notes (px écran, taille finale) : isole le symbole de la traîne et des liaisons */
#define NOTE_DISC_RADIUS  27.0f
#define NOTE_BORDER_WIDTH  5.5f   /* contour, ajouté autour du disque (qui garde ses 54 px) : ~65 px au total */

/* Néon des cibles et des arcs qui les relient (turquoise de la palette #10C4B6, cœur éclairci).
   Profil commun : trait net de demi-épaisseur NEON_CORE_HALF_WIDTH + halo gaussien. */
#define TARGET_RING_RADIUS   32.0f   /* rayon du liseré autour d'une cible, px écran */
#define NEON_CORE_HALF_WIDTH  3.0f
#define NEON_GLOW_WIDTH       8.0f
#define NEON_GLOW_PEAK        0.6f   /* opacité du halo au ras du trait */
/* Couleurs de la charte « Cascade pop » (cyan CH_CYAN, rose CH_PINK), cœur éclairci */
#define NEON_GLOW_COLOR ((SDL_Color){79, 200, 240, 255})
#define NEON_CORE_COLOR ((SDL_Color){205, 240, 252, 255})
#define NEON_MAGENTA_GLOW_COLOR ((SDL_Color){247, 12, 147, 255})
#define NEON_MAGENTA_CORE_COLOR ((SDL_Color){255, 170, 215, 255})

/* Couleurs (halo, cœur) d'un néon */
SDL_Color neon_glow_color(NeonColor n);
SDL_Color neon_core_color(NeonColor n);
/* Néon d'une lane : magenta pour Triangle / Rond / Croix, turquoise sinon */
NeonColor neon_for_lane(Button lane);

/* Taille d'affichage d'un sprite (px). Le disque d'une cible fait 24/32 de la
   cellule : 70 * 24/32 ~ 52 px, soit le diamètre historique des icônes. */
#define SPRITE_DRAW_SIZE 70.0f

bool sprites_init(Sprites *s, SDL_Renderer *r);
/* Arcs de l'écran de jeu (aplat cyan à gauche, rose à droite, cerclés d'encre, bouts ronds), rastérisés une fois
   pour toutes : ils sont fixes et dessinés à chaque image. Arcs elliptiques de centres (left_cx, cy) et
   (right_cx, cy), demi-axes rx, ry ; angles 0 = midi, sens horaire : 198..342° à gauche, 18..162° à droite. */
bool sprites_build_arcs(Sprites *s, SDL_Renderer *r, float left_cx, float right_cx, float cy, float rx, float ry);
void sprite_draw_arcs(const Sprites *s, SDL_Renderer *r);
void sprites_shutdown(Sprites *s);
/* alpha : 255 = opaque */
void sprite_draw_target(const Sprites *s, SDL_Renderer *r, Button lane, float cx, float cy, Uint8 alpha);
/* Liseré néon turquoise autour d'une cible (à dessiner avant la cible). boost > 0 : surbrillance
   additive en plus (appui sur la lane), 0..1. */
void sprite_draw_target_ring(const Sprites *s, SDL_Renderer *r, NeonColor color, float cx, float cy, float boost);
/* Icône de bouton pour l'UI (aides de boutons) : sprite « cible » à la taille `size` px.
   BTN_RIGHT = icône Gauche retournée. Sans effet pour les boutons absents de l'atlas. */
bool sprite_has_icon(Button b);
void sprite_draw_icon(const Sprites *s, SDL_Renderer *r, Button b, float cx, float cy, float size, Uint8 alpha);
/* Symbole de la touche en blanc pur, sans disque ni contour (notes liées : le symbole sert de bout à l'os) */
void sprite_draw_note_white(const Sprites *s, SDL_Renderer *r, Button lane, float cx, float cy, float scale, Uint8 alpha);
/* scale : 1 = taille normale, < 1 pour rentrer dans un disque de fond */
void sprite_draw_note(const Sprites *s, SDL_Renderer *r, Button lane, float cx, float cy, float scale, Uint8 alpha);
/* Disque de fond d'une note (fill) et son contour (border), à la taille `size` (1 = taille finale).
   Textures lissées : pas d'escalier sur les bords, contrairement à un cercle en polygones. */
void sprite_draw_note_disc(const Sprites *s, SDL_Renderer *r, float cx, float cy, float size,
                           SDL_Color fill, SDL_Color border);

#endif
