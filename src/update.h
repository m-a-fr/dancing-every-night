#ifndef UPDATE_H
#define UPDATE_H
#include <stdbool.h>
#include "sdl.h"

/* Annonce d'une nouvelle version. Au démarrage (Vita), un thread lit la fiche UPDATE_URL (latest.json, publiée avec le
   site à chaque release) ; si la version qui s'y trouve est plus récente que GAME_VERSION, le menu principal l'annonce.
   Rien n'est téléchargé ni installé : le joueur met à jour comme d'habitude (VitaDB, page de téléchargement).
   Hors Vita : pas de réseau ; VITAGAME_UPDATE_JSON=fichier.json lit une fiche locale (pour essayer l'affichage). */

typedef enum { UPDATE_IDLE, UPDATE_CHECKING, UPDATE_CURRENT, UPDATE_AVAILABLE, UPDATE_FAILED } UpdateState;

typedef struct {
    SDL_atomic_t state;     /* UpdateState : écrit en dernier par le thread, les champs ci-dessous sont alors prêts */
    char version[24];       /* version annoncée (« 0.9.1 ») */
    char notes[200];        /* nouveautés, une ligne (notes_en de la fiche) */
    char url[160];          /* page où la récupérer */
} Update;

/* Compare deux versions « majeure.mineure.correctif » (champs manquants = 0) : < 0, 0 ou > 0 */
int update_version_compare(const char *a, const char *b);
/* Lit la fiche JSON : true si elle annonce une version plus récente que `current` (champs de `out` remplis) */
bool update_parse(const char *json, const char *current, Update *out);

/* Lance la vérification (Vita : thread en arrière-plan, jamais bloquante). `u` doit vivre jusqu'à la fin du processus
   (allocation par update_create) : un thread encore en cours d'attente réseau à la fermeture l'utilise. */
Update *update_create(void);
void update_start(Update *u, const char *current_version);
UpdateState update_state(const Update *u);

#endif
