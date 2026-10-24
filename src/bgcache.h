#ifndef BGCACHE_H
#define BGCACHE_H
#include <stdbool.h>
#include "sdl.h"
#include "background.h"

/* Cache des images de fond du menu : un fil d'exécution décode les JPEG (lent sur Vita) sans bloquer l'affichage,
   les textures prêtes sont gardées (les plus récemment utilisées) pour qu'un changement de chanson soit instantané.
   Le menu demande l'image sélectionnée et ses voisines : en faisant défiler, l'image est déjà prête. */

#define BGCACHE_SLOTS     7    /* textures gardées par défaut (~2 Mo de VRAM chacune en 960x544) */
#define BGCACHE_SLOTS_MAX 16   /* au plus (cache de vignettes) */
#define BGCACHE_QUEUE     12   /* images demandées : la sélectionnée puis ses voisines, par priorité */
#define BGCACHE_PATH  192

typedef struct {
    char       path[BGCACHE_PATH];   /* "" = emplacement libre */
    Background bg;
    unsigned   last_use;
} BgCacheSlot;

typedef struct {
    SDL_Renderer *renderer;
    SDL_Thread   *thread;
    SDL_mutex    *lock;
    SDL_cond     *cond;
    bool          quit;
    bool          grayscale;    /* fixé avant le premier bgcache_want : images converties en niveaux de gris au décodage */
    int           thumb_w, thumb_h;   /* idem : > 0 = vignettes, images recadrées et réduites à cette taille au décodage */
    int           slot_count;   /* textures gardées (BGCACHE_SLOTS par défaut, au plus BGCACHE_SLOTS_MAX) */
    /* partagé avec le fil de décodage (sous lock) */
    char           queue[BGCACHE_QUEUE][BGCACHE_PATH];
    int            queue_dim[BGCACHE_QUEUE];
    int            queue_n;
    char           busy[BGCACHE_PATH];        /* en cours de décodage */
    bool           done;                      /* une image décodée attend d'être téléversée */
    char           done_path[BGCACHE_PATH];
    int            done_dim, done_w, done_h;
    unsigned char *done_px;
    /* thread principal seulement */
    BgCacheSlot slots[BGCACHE_SLOTS_MAX];
    unsigned    clock;
} BgCache;

bool bgcache_init(BgCache *c, SDL_Renderer *r);
void bgcache_shutdown(BgCache *c);
/* Libère les textures (VRAM) et oublie les demandes ; le fil reste prêt */
void bgcache_clear(BgCache *c);
/* Remplace les demandes par cette liste (chemins complets, priorité décroissante ; "" ignoré) */
void bgcache_want(BgCache *c, const char *const *paths, const int *dims, int n);
/* Thread principal, une fois par image : téléverse l'image décodée s'il y en a une */
void bgcache_update(BgCache *c);
/* Image prête pour ce chemin, NULL sinon (la marque comme récemment utilisée) */
const Background *bgcache_get(BgCache *c, const char *path);
/* Comme bgcache_get, sans la marquer (dessin : ne modifie pas le cache) */
const Background *bgcache_peek(const BgCache *c, const char *path);

#endif
