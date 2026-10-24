#ifndef FONT_H
#define FONT_H
#include <stdbool.h>
#include "sdl.h"

/* Polices du jeu : atlas assets/ui/font.png + mesures assets/ui/font.txt, générés par
   tools/font2atlas.py depuis art/fonts/ (néon : Rajdhani, Orbitron ; Cascade pop : Anton, Bricolage
   Grotesque, M PLUS Rounded 1c). Même ordre que FACES dans le script. Texte en UTF-8 (ASCII + Latin-1). */
typedef enum {
    FONT_SMALL,        /* Rajdhani 22 : aides de boutons, légendes */
    FONT_BODY,         /* Rajdhani 28 : texte courant */
    FONT_BODY_BOLD,    /* Rajdhani gras 34 : élément sélectionné */
    FONT_TITLE_SMALL,  /* Orbitron 28, halo */
    FONT_TITLE,        /* Orbitron 38, halo */
    FONT_DISPLAY,      /* Orbitron 56, halo, ASCII seulement : score */
    FONT_HUGE,         /* Orbitron 128, halo, chiffres + " SABCDGO!+x%" : rang, compte à rebours */
    /* Style « Cascade pop » (charte du 15/10/2026) : contour d'encre épais, sans halo -> font_draw_ink */
    FONT_POP_XL,       /* Anton 66, contour 6 px : titre sélectionné (réduit jusqu'à ~34 px) */
    FONT_POP,          /* Anton 38, contour 3 px + couche creuse : titres voisins de la cascade, en-têtes */
    FONT_CARD_NAME,    /* Bricolage Grotesque 800, 28 : nom d'artiste de la fiche */
    FONT_CARD,         /* Bricolage Grotesque 600, 16 : infos de la fiche */
    FONT_CARD_BOLD,    /* Bricolage Grotesque 800, 15 : pastilles de difficulté */
    FONT_LABEL,        /* M PLUS Rounded 1c 800, 14 : bannières, aides de boutons */
    FONT_POP_S,        /* Anton 16 : onglets */
    FONT_POP_HUGE,     /* Anton 148, contour 7 px, chiffres + « SABCD? » : rang, compte à rebours */
    FONT_FACE_COUNT
} FontFace;

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } TextAlign;

typedef struct FontGlyph FontGlyph;

typedef struct {
    int   cap_height;          /* hauteur des majuscules, en px */
    int   line_height;
    bool  has_glow;
    bool  has_outline;         /* couche contour présente (faces néon et faces « Cascade pop ») */
    bool  half_outline;        /* contour stocké à mi-résolution (grandes tailles) */
    bool  has_hollow;          /* couche creuse : anneau de 2 px à cheval sur le bord (lettres « en contour ») */
    short map[256];            /* codepoint (voir font.c) -> index dans glyphs, -1 = absent */
} FontFaceInfo;

typedef struct {
    SDL_Texture *atlas;
    FontGlyph   *glyphs;       /* alloué au chargement */
    FontFaceInfo faces[FONT_FACE_COUNT];
} Font;

bool font_init(Font *f, SDL_Renderer *r);
void font_shutdown(Font *f);
int  font_width(const Font *f, FontFace face, const char *text);
int  font_cap_height(const Font *f, FontFace face);
/* y = haut des majuscules ; x selon align (bord gauche, centre ou bord droit) */
void font_draw(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, TextAlign align,
               SDL_Color color, const char *text);
/* Comme font_draw, avec un halo néon additif de couleur `glow` dessous (glow.a = intensité) et un
   contour sombre fin entre le halo et la lettre, pour la lisibilité. Sans effet sur les faces sans halo.
   Pour un rendu « tube néon », prendre une couleur de texte plus claire que le halo (font_lighten). */
void font_draw_glow(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, TextAlign align,
                    SDL_Color color, SDL_Color glow, const char *text);
/* Version complète : scale agrandit autour du point d'ancrage (animations « pop »). */
void font_draw_ex(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, TextAlign align,
                  float scale, SDL_Color color, SDL_Color glow, const char *text);

/* Texte le long d'une ellipse (centre cx, cy ; rayons rx, ry = ligne médiane des majuscules), à partir de
   l'angle start_deg (0 = haut, sens horaire), lettres tournées pour que leur haut regarde vers l'extérieur.
   Retourne l'angle (degrés) atteint à la fin du texte. Couche nette seulement (pas de halo). */
float font_draw_on_ellipse(const Font *f, SDL_Renderer *r, FontFace face, float cx, float cy, float rx, float ry,
                           float start_deg, float scale, SDL_Color color, const char *text);
/* Angle (degrés) qu'occupe le texte sur une ellipse de rayon moyen (rx + ry) / 2 */
float font_angle_on_ellipse(const Font *f, FontFace face, float rx, float ry, float scale, const char *text);

/* Style « Cascade pop » : lettre `fill` cernée du contour d'encre `ink` (faces avec contour), avec une ombre
   décalée de (shadow_dx, shadow_dy) dans la couleur `shadow` (shadow.a = 0 : pas d'ombre ; c'est la silhouette
   contour comprise qui est décalée). Lettres « en contour » à intérieur transparent : font_draw_hollow_rot. */
void font_draw_ink(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, TextAlign align, float scale,
                   SDL_Color fill, SDL_Color ink, SDL_Color shadow, float shadow_dx, float shadow_dy,
                   const char *text);

/* Mise en place d'un texte dessiné par géométrie : rotation de angle_deg (sens horaire, comme SDL) autour du point
   (pivot_x, pivot_y), tracking = espacement ajouté après chaque caractère (px avant agrandissement), skew_deg =
   inclinaison façon skewX CSS appliquée avant la rotation (négatif : le haut penche vers la droite) */
typedef struct { float angle_deg, pivot_x, pivot_y, tracking, skew_deg; } FontRot;
/* Largeur d'un texte avec espacement (`tracking` px par caractère, sauf le dernier) */
int  font_width_tracked(const Font *f, FontFace face, const char *text, float tracking);
/* font_draw_ink tourné : (x, y) = crayon (bord gauche, haut des majuscules) avant rotation. Une seule commande de
   dessin par couche (ombre, contour, lettre), quelle que soit la longueur du texte (96 caractères au plus). */
void font_draw_ink_rot(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, float scale, FontRot rot,
                       SDL_Color fill, SDL_Color ink, SDL_Color shadow, float shadow_dx, float shadow_dy,
                       const char *text);
/* Lettres « en contour », intérieur transparent (faces creuses ; sinon lettre pleine), tournées : un seul dessin */
void font_draw_hollow_rot(const Font *f, SDL_Renderer *r, FontFace face, float x, float y, float scale, FontRot rot,
                          SDL_Color color, const char *text);

/* Mélange la couleur vers le blanc (t = 0 : inchangée, 1 : blanc). Remplissage d'un texte néon. */
SDL_Color font_lighten(SDL_Color c, float t);

#endif
