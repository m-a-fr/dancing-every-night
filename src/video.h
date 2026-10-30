#ifndef VIDEO_H
#define VIDEO_H
#include <stdbool.h>
#include "sdl.h"

/* Vidéo de fond au format MPEG-1 (.mpg sans son, produit par tools/media.py), décodée par
   pl_mpeg dans un thread séparé ; la conversion YUV -> RGB est faite par le GPU (texture IYUV).
   Si le décodage prend trop de retard, on saute à l'image clé suivante au lieu de ralentir
   le jeu. L'interface est volontairement minimale (image pour un temps donné) pour pouvoir
   changer de décodeur sans toucher au reste : les fichiers .h264 passent par le décodeur matériel
   de la Vita (video_h264_vita.c) ou par h264bsd sur PC (video_h264_pc.c), les .mpg par pl_mpeg. */
typedef struct {
    void        *impl;      /* état interne (NULL = pas de vidéo) */
    SDL_Texture *tex;       /* dernière image reçue */
    int          w, h;
    bool         has_frame;
    bool         h264;      /* vidéo H.264 (.h264) : décodeur matériel sur Vita (video_h264_vita.c), logiciel ailleurs (video_h264_pc.c) */
} Video;

bool video_open(Video *v, SDL_Renderer *r, const char *path);
void video_close(Video *v);
/* Demande l'image correspondant à t_ms (temps depuis le début de la vidéo, borné aux limites) */
void video_seek(Video *v, float t_ms);
/* À appeler dans le thread principal : téléverse la dernière image décodée dans la texture */
void video_update(Video *v);

#endif
