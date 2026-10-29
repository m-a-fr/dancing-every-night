#ifndef FILES_H
#define FILES_H
#include <stdbool.h>
#include <stddef.h>

/* Opérations de fichiers portables (sceIo* sur Vita, POSIX ailleurs) pour les dossiers de maps et d'import.
   Chemins complets, avec ou sans '/' final pour les dossiers. */
bool files_mkdir(const char *dir);              /* true si le dossier existe à la sortie */
bool files_is_dir(const char *path);
bool files_remove(const char *file);
bool files_rename(const char *from, const char *to);
/* Taille et date de modification (valeur comparable, pas forcément en secondes) ; false si le fichier manque */
bool files_stat(const char *path, long long *size, long long *mtime);
/* Supprime un dossier et tout son contenu (sous-dossiers compris). true si plus rien n'existe à la sortie. */
bool files_remove_tree(const char *dir);
/* Crée un dossier temporaire neuf d'après `tmpl` (finissant par XXXXXX, remplacés) ; false en cas d'échec */
bool files_mkdtemp(char *tmpl);
/* Chemin réel d'un asset : custom/<rel> (dossier du joueur, CUSTOM_DIR) s'il existe, sinon assets/<rel>. rel sans '/' initial,
   ex. "ui/hostess/rank_s.png". Écrit dans buf et le renvoie. */
const char *asset_resolve(const char *rel, char *buf, size_t size);
/* PC : se place dans le dossier des données du jeu (assets/ et save/), quel que soit le dossier d'où il est lancé :
   le dossier du programme (Windows, Linux), ou Application Support avec un lien vers les assets du .app (macOS).
   Sans effet si assets/ est déjà là (make run) et sur Vita. */
void files_enter_game_dir(void);

#endif
