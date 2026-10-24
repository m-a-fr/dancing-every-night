#ifndef BACKGROUND_H
#define BACKGROUND_H
#include <stdbool.h>
#include "sdl.h"
#include "video.h"

/* Fond d'une chanson : image (PNG/JPEG, idéalement 960x544) et/ou vidéo, recadrés pour remplir
   l'écran et assombris par un voile noir. L'image apparaît en fondu ; la vidéo la remplace en
   fondu dès que sa première image est décodée. */
typedef struct {
    SDL_Texture *tex;
    int          tex_w, tex_h;
    int          dim;           /* 0..100, part de noir ajoutée par-dessus */
    float        image_fade;    /* 0..1 */
    Video        video;         /* optionnelle */
    int          video_offset_ms;
    float        video_fade;    /* 0..1, avance dès que la première image est prête */
} Background;

/* Crée l'image depuis des pixels RGBA déjà décodés (thread principal ; px reste à l'appelant) */
bool background_from_rgba(Background *b, SDL_Renderer *r, const unsigned char *px, int w, int h, int dim);
/* Charge l'image (chemin complet ; vide = aucune). Retourne true si chargée. */
bool background_load(Background *b, SDL_Renderer *r, const char *image_path, int dim);
/* Ouvre la vidéo (peut prendre un moment : lecture de l'index du fichier). */
bool background_load_video(Background *b, SDL_Renderer *r, const char *video_path, int video_offset_ms);
void background_free(Background *b);
/* song_ms : temps de la piste audio (négatif pendant le lead-in) ; à appeler quand le jeu avance */
void background_seek(Background *b, float song_ms);
/* Thread principal, une fois par frame : téléverse l'image vidéo décodée et avance les fondus */
void background_update(Background *b, float dt);
/* Dessine le fond (ou rien : l'appelant a déjà rempli l'écran) */
void background_draw(const Background *b, SDL_Renderer *r);
/* Idem ; dim = false : sans le voile noir de la carte (l'appelant pose son propre voile) */
void background_draw_ex(const Background *b, SDL_Renderer *r, bool dim);
/* Sans voile, opacité multipliée par mult (0..1) : fondu d'un écran par-dessus le jeu */
void background_draw_faded(const Background *b, SDL_Renderer *r, float mult);

#endif
