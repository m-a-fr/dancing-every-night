#ifndef PREVIEW_H
#define PREVIEW_H
#include <stdbool.h>
#include "sdl.h"
#include "sfx.h"

/* Aperçu musical du menu : joue en boucle un extrait de la chanson sélectionnée (à partir de son refrain,
   Chart.preview_ms), avec fondus. Le fichier OGG est lu et positionné par un fil d'exécution (pas d'à-coup
   à l'écran), et seulement quand la sélection s'arrête un instant (pas de chargement à chaque ligne survolée).
   Le périphérique audio est ouvert à la demande et fermé par preview_close (avant de lancer une chanson). */

#define PREVIEW_PATH 192

typedef struct PreviewTrack PreviewTrack;

typedef struct {
    Sfx         *sfx;           /* bruitages et musique de fond mixés dans ce périphérique (NULL = aucun) */
    SDL_AudioDeviceID dev;      /* 0 = fermé */
    int          freq;
    SDL_Thread  *thread;
    SDL_mutex   *lock;
    SDL_cond    *cond;
    /* demande au fil de chargement (sous lock) */
    bool         quit;
    unsigned     gen;           /* change à chaque nouvelle demande : un chargement d'une génération passée est jeté */
    unsigned     loaded_gen;
    char         load_path[PREVIEW_PATH];
    int          load_ms;
    /* morceaux partagés avec le callback audio (sous SDL_LockAudioDevice) */
    PreviewTrack *cur;          /* en cours de lecture */
    PreviewTrack *next;         /* prêt, joué quand cur s'est tu */
    PreviewTrack *retired;      /* terminé, libéré par le thread principal */
    Uint32       cb_ticks;      /* SDL_GetTicks du dernier appel du callback audio */
    int          cb_frames;     /* frames fournies par appel */
    /* thread principal seulement */
    char         want_path[PREVIEW_PATH];   /* chanson voulue par le menu ("" = silence) */
    int          want_ms;
    float        settle;        /* secondes depuis que want_path a changé */
    char         sent_path[PREVIEW_PATH];   /* dernière chanson demandée au fil */
} Preview;

bool preview_init(Preview *p);
void preview_shutdown(Preview *p);
/* Chanson voulue (chemin complet du .ogg, "" = aucune) et début de l'extrait en ms ; à appeler chaque image */
void preview_want(Preview *p, const char *path, int start_ms);
/* Thread principal, chaque image où le menu est affiché : lance les chargements, libère les morceaux finis */
void preview_update(Preview *p, float dt);
/* Position audible (ms dans la piste) de l'extrait `path` s'il est en train de jouer, -1 sinon (chargement,
   autre morceau, silence entre deux boucles) : cale les animations du menu sur la musique */
int  preview_position_ms(Preview *p, const char *path);
/* Ouvre le périphérique tout de suite (sinon il s'ouvre à la première chanson voulue) : il porte aussi les
   bruitages et la musique de fond du titre et du menu */
void preview_open(Preview *p);
/* Coupe le son, libère les morceaux et ferme le périphérique audio (rouvert au prochain preview_update) */
void preview_close(Preview *p);

#endif
