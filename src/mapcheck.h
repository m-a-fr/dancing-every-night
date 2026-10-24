#ifndef MAPCHECK_H
#define MAPCHECK_H
#include <stdbool.h>

/* Validateur de map (desktop) : `vitagame --check <map.zip | dossier>` vérifie une map avec les règles du jeu
   (organisation du zip comme à l'import, format 1, ID, cartes lisibles, médias que le jeu sait lire, limites de la
   Vita) et affiche un rapport en anglais : ERROR = la map ne marchera pas, WARN = à corriger de préférence.
   Retourne le code de sortie du programme : 0 sans erreur, 1 sinon. Sert aux mappers et à la modération.
   json : un seul objet JSON sur stdout (métadonnées de la chanson, difficultés, messages) au lieu du rapport texte :
   c'est ce que lit la bibliothèque de chansons (tools/catalog.py). */
int mapcheck_run(const char *path, bool json);

#endif
