#ifndef MAPIMPORT_H
#define MAPIMPORT_H
#include <stdbool.h>
#include <stddef.h>

/* Import des maps déposées par le joueur : chaque .zip ou dossier de IMPORT_DIR (ux0:data/<jeu>/import/) est installé au
   démarrage dans USER_SONGS_DIR/<ID>/ (une map = un dossier, voir chart.h), puis supprimé. Une map de même ID est
   remplacée (les scores, rattachés à l'ID, restent). Un import refusé part dans import/failed/ avec un .txt qui dit
   pourquoi.

   Contenu accepté : les fichiers de la map à la racine du zip, ou tous dans un seul dossier (zip d'un dossier).
   Un dossier déjà décompressé est aussi accepté, directement ou dans un unique dossier enveloppe.
   ID : ligne ID d'une carte, sinon nom de ce dossier, sinon nom du zip. */

/* ID de chanson acceptable pour un nom de dossier sur tous les systèmes : lettres, chiffres, _ et - (47 au plus), et pas un
   nom réservé de Windows (CON, NUL, COM1...) */
bool mapimport_valid_id(const char *id);

#define MAPIMPORT_MAX 64   /* imports traités par lancement (les suivants au lancement d'après) */

typedef struct {
    char zips[MAPIMPORT_MAX][96];   /* noms des ZIP ou dossiers trouvés dans IMPORT_DIR */
    int  count;
    int  imported, failed;
} MapImport;

/* Crée les dossiers d'import et de maps au besoin, puis liste les ZIP et dossiers en attente. Retourne leur nombre. */
int  mapimport_scan(MapImport *mi);
/* Importe le ZIP ou dossier n° i ; false s'il a été refusé (déplacé dans import/failed/) */
bool mapimport_run(MapImport *mi, int i);
/* Vérifie l'organisation du zip `zip_path` (nommé `zip_name`), trouve l'ID de la map et extrait ses fichiers dans le
   dossier `dest` (vidé d'abord, '/' final). Retourne NULL si tout va bien, sinon la raison du refus. Utilisé aussi
   par le validateur (mapcheck.h). */
const char *mapimport_unpack(const char *zip_path, const char *zip_name, const char *dest, char *id, size_t id_size);

/* Installe le zip `zip_path` téléchargé depuis la bibliothèque dans USER_SONGS_DIR/<expect_id>/ (remplace la version
   installée). Refuse un zip dont l'ID n'est pas expect_id. Retourne NULL si tout va bien, sinon la raison du refus. */
const char *mapimport_install(const char *zip_path, const char *expect_id);

#endif
