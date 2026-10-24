#ifndef UI_H
#define UI_H
#include <stdbool.h>
#include <stddef.h>
#include "sdl.h"
#include "font.h"
#include "sprites.h"
#include "input.h"

/* Éléments d'interface communs (menus, HUD, résultats). Direction néon / arcade, palette tirée de
   l'atlas des icônes (voir GAME_DESIGN.md). Fonctions de dessin seulement : aucun état. */

#define UI_NAVY     ((SDL_Color){ 28,  30,  42, 255})   /* fond */
#define UI_PANEL    ((SDL_Color){ 36,  39,  56, 255})   /* panneaux, grille */
#define UI_SLATE    ((SDL_Color){175, 192, 208, 255})   /* texte secondaire */
#define UI_WHITE    ((SDL_Color){240, 244, 250, 255})   /* texte principal */
#define UI_TEAL     ((SDL_Color){ 16, 196, 182, 255})   /* Triangle */
#define UI_CORAL    ((SDL_Color){216,  90,  90, 255})   /* Rond : sélection */
#define UI_LAVENDER ((SDL_Color){136, 144, 192, 255})   /* Croix */
#define UI_NONE     ((SDL_Color){  0,   0,   0,   0})   /* pas de halo */

/* Palette « Cascade pop » (charte du 15/10/2026, art/charte/tokens.json), recalée sur le logo de l'accueil.
   Les écrans passent de la palette néon ci-dessus à celle-ci au fil de la refonte. */
#define CH_INK      ((SDL_Color){ 11,   7,  32, 255})   /* encre : contours, cartes, bandeaux */
#define CH_NIGHT    ((SDL_Color){ 14,   8,  36, 255})   /* fond de l'accueil et des facettes */
#define CH_VIOLET   ((SDL_Color){ 42,  47, 176, 255})   /* départ de la bichromie */
#define CH_DUO_PINK ((SDL_Color){200,  26, 168, 255})   /* arrivée de la bichromie */
#define CH_PINK     ((SDL_Color){247,  12, 147, 255})   /* accent : éclaboussure, ombres, onglet actif */
#define CH_GOLD     ((SDL_Color){251, 214,  63, 255})   /* titre sélectionné, score, PERFECT, étoiles */
#define CH_CYAN     ((SDL_Color){ 79, 200, 240, 255})   /* côté gauche, pastille active (token « mint ») */
#define CH_ROSE     ((SDL_Color){235, 134, 213, 255})   /* MISS, états atténués (token « lavender ») */
#define CH_WHITE    ((SDL_Color){255, 255, 255, 255})   /* texte sur encre */
#define CH_MUTED    ((SDL_Color){255, 255, 255, 199})   /* texte secondaire : blanc à 78 % */

/* Même couleur avec une autre opacité */
SDL_Color ui_alpha(SDL_Color c, Uint8 a);

/* Fond bleu nuit, quadrillé si grid ; alpha < 255 pour le poser en voile sur une image */
void ui_backdrop(SDL_Renderer *r, Uint8 alpha, bool grid);
/* Bichromie « Cascade pop » : dégradé violet -> rose posé en multiply sur tout l'écran (sur une image déjà en
   niveaux de gris, il la teinte ; voir bgcache.grayscale). */
void ui_duotone(SDL_Renderer *r);
/* Voile d'encre dégressif depuis la gauche (lisibilité de la liste) : opaque à 90 % jusqu'à 36 % de la largeur,
   55 % à mi-écran, nul aux trois quarts. */
void ui_veil_left(SDL_Renderer *r);
/* Voile d'encre en dégradé horizontal : n arrêts (n <= 8), abscisses 0..1 de l'écran et opacités 0..1 */
void ui_veil_gradient(SDL_Renderer *r, const float *stop_x, const float *stop_a, int n);
/* Lissage des bords des formes pleines en polygones (cartes, étiquettes, éclaboussures, tampon, étoiles) : le
   moteur de rendu de la Vita n'a pas d'anti-crénelage sur les triangles, un bord oblique y fait des marches.
   Chaque polygone est entouré d'une frange d'alpha dégradé de cette largeur (px), centrée sur le bord réel : la
   forme ne grossit pas. 0 = désactivé (bords francs), ~1 = lissage classique ; plus c'est large, plus c'est flou.
   Ne concerne que les polygones : le texte et les images (textures) ont leur propre lissage. */
#define UI_EDGE_FEATHER 0.8f
/* Ajoute le polygone p[0..n-1] (n <= 48, sommets dans un sens quelconque) rempli de la couleur c, avec sa frange,
   aux tableaux de sommets v / d'indices idx ; retourne false (rien ajouté) s'il n'y a plus de place. */
bool ui_poly_append(SDL_Vertex *v, int *nv, int vmax, int *idx, int *ni, int imax, const SDL_FPoint *p, int n,
                    SDL_Color c);
/* Dessine le polygone p[0..n-1] (n <= 48) lissé, en un seul appel */
void ui_poly(SDL_Renderer *r, const SDL_FPoint *p, int n, SDL_Color c);
/* Voile d'encre radial de l'écran de jeu (maquette validée) : léger au centre (25 %), plus dense vers les bords
   (62 % aux trois quarts du rayon, puis constant). Remplace l'assombrissement uniforme ; les couleurs de la vidéo
   ou de l'image restent d'origine (pas de bichromie en jeu, décision du 2026-10-15). */
void ui_veil_radial(SDL_Renderer *r);
/* Panneau arrondi : fond `fill` + bord néon `border` (glow = intensité du halo, 0 = trait simple) */
void ui_panel(SDL_Renderer *r, SDL_FRect rect, float radius, SDL_Color fill, SDL_Color border, float glow);

/* Copie `text` dans out en le coupant avec « … » pour tenir dans max_w px */
void ui_fit_text(const Font *f, FontFace face, const char *text, int max_w, char *out, size_t out_size);
/* Score groupé par milliers : 1234567 -> « 1 234 567 » */
void ui_format_score(int score, char *out, size_t out_size);
/* Couleur d'une difficulté : EASY turquoise, NORMAL lavande, HARD corail, EXPERT magenta, autres gris-bleu */
SDL_Color ui_difficulty_color(const char *name);

/* Aide de boutons : icônes (n boutons) puis libellé, à partir de x ; y = centre vertical.
   Retourne la position x de fin (pour enchaîner plusieurs aides). */
float ui_button_hint(SDL_Renderer *r, const Font *f, const Sprites *s, float x, float y,
                     const Button *buttons, int n, const char *label);

/* Largeur qu'occuperait ui_button_hint (pour aligner une aide à droite) */
float ui_button_hint_width(const Font *f, const Button *buttons, int n, const char *label);

/* Aide de boutons « Cascade pop » (icônes 22 px, libellé FONT_LABEL blanc) ; même usage que ui_button_hint */
float ui_button_hint_pop(SDL_Renderer *r, const Font *f, const Sprites *s, float x, float y,
                         const Button *buttons, int n, const char *label);

/* Parallélogramme plein (cartes « Cascade pop ») : rect dont le haut est décalé de +lean px et le bas de -lean
   (penche vers la droite, comme un skewX(-8deg) CSS : lean = h/2 * tan 8°) */
void ui_skew_box(SDL_Renderer *r, SDL_FRect rect, float lean, SDL_Color color);
/* Rectangle plein tourné (rot.tracking ignoré), en un seul dessin : bandeaux posés sur la cascade */
void ui_fill_rect_rot(SDL_Renderer *r, SDL_FRect rect, FontRot rot, SDL_Color color);

/* Texte « Cascade pop » centré en (cx, cy = milieu des majuscules), tourné de angle_deg autour de ce point et
   incliné de skew_deg ; scale par rapport à la face, tracking_px = espacement à l'écran. Ombre : silhouette
   décalée (shadow.a = 0 : aucune) ; fill.a = 0 et ink.a = 0 : ne dessine que l'ombre (ombres multiples). */
void ui_pop_text(SDL_Renderer *r, const Font *f, FontFace face, float cx, float cy, float scale, float tracking_px,
                 float angle_deg, float skew_deg, SDL_Color fill, SDL_Color ink, SDL_Color shadow, float sdx, float sdy,
                 const char *text);
/* Largeur qu'occuperait ui_button_hint_pop (pour aligner une aide à droite) */
float ui_button_hint_pop_width(const Font *f, const Button *buttons, int n, const char *label);
/* Rectangle arrondi plein, tourné selon rot (tracking et skew ignorés), en un seul dessin */
void ui_round_rect_rot(SDL_Renderer *r, SDL_FRect rect, float radius, FontRot rot, SDL_Color c);
/* Éclaboussure anguleuse : polygone pts (fractions 0..1 de rect, comme un clip-path CSS), tourné, un seul dessin */
void ui_splash_rect(SDL_Renderer *r, SDL_FRect rect, FontRot rot, const float (*pts)[2], int n, SDL_Color c);
/* Étiquettes accolées inclinées (skewX -8°, Anton 15 espacé, texte blanc) : « NIGHT 02 » rose + titre d'encre */
void ui_tags(SDL_Renderer *r, const Font *f, float x, float y, const char *const *texts, const SDL_Color *bgs, int n,
             Uint8 alpha);
/* Étoile à quatre branches pleine (étincelles), taille = diamètre en px, un seul dessin */
void ui_star(SDL_Renderer *r, float cx, float cy, float size, float angle_deg, SDL_Color c);
/* Copie en capitales (ASCII et Latin-1 en UTF-8 : à..þ -> À..Þ, sauf ÷) */
void ui_upper(const char *in, char *out, size_t size);

/* Rapproche *v de target (lissage exponentiel, `speed` en 1/s) : animations de curseur, défilement */
void ui_approach(float *v, float target, float speed, float dt);

#endif
