#ifndef MAPIMPORT_H
#define MAPIMPORT_H
#include <stdbool.h>
#include <stddef.h>

/* Import des maps déposées par le joueur : chaque .zip de IMPORT_DIR (ux0:data/<jeu>/import/) est décompressé au
   démarrage dans USER_SONGS_DIR/<ID>/ (une map = un dossier, voir chart.h), puis supprimé. Une map de même ID est
   remplacée (les scores, rattachés à l'ID, restent). Un zip refusé part dans import/failed/ avec un .txt qui dit
   pourquoi.

   Contenu accepté : les fichiers de la map à la racine du zip, ou tous dans un seul dossier (zip d'un dossier).
   ID : ligne ID d'une carte, sinon nom de ce dossier, sinon nom du zip. */

#define MAPIMPORT_MAX 64   /* zips traités par lancement (les suivants au lancement d'après) */

typedef struct {
    char zips[MAPIMPORT_MAX][96];   /* noms des fichiers .zip trouvés dans IMPORT_DIR */
    int  count;
    int  imported, failed;
} MapImport;

/* Crée les dossiers d'import et de maps au besoin, puis liste les zips en attente. Retourne leur nombre. */
int  mapimport_scan(MapImport *mi);
/* Importe le zip n° i ; false s'il a été refusé (déplacé dans import/failed/) */
bool mapimport_run(MapImport *mi, int i);
/* Vérifie l'organisation du zip `zip_path` (nommé `zip_name`), trouve l'ID de la map et extrait ses fichiers dans le
   dossier `dest` (vidé d'abord, '/' final). Retourne NULL si tout va bien, sinon la raison du refus. Utilisé aussi
   par le validateur (mapcheck.h). */
const char *mapimport_unpack(const char *zip_path, const char *zip_name, const char *dest, char *id, size_t id_size);

#endif
