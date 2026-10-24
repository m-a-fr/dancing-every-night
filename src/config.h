#ifndef CONFIG_H
#define CONFIG_H

/* Résolution native de la PS Vita. Toute la logique du jeu raisonne dans cet espace. */
#define SCREEN_W 960
#define SCREEN_H 544

/* Identité du jeu : définie dans CMakeLists.txt (seul endroit où l'écrire), reçue ici en macros */
#if !defined(GAME_NAME) || !defined(GAME_VERSION) || !defined(GAME_SAVE_SLUG)
#  error "GAME_NAME, GAME_VERSION et GAME_SAVE_SLUG viennent de CMakeLists.txt : compiler via CMake"
#endif

/* Chemins : sur Vita, app0: = dossier du jeu (lecture seule), ux0:data/ = écriture. */
#ifdef __vita__
#  define ASSET_DIR     "app0:assets/"
#  define SAVE_DIR      "ux0:data/" GAME_SAVE_SLUG "/"
#  define LEGACY_SAVE_DIR "ux0:data/vibegame/"   /* versions de dev d'avant 0.9.0 : repris une fois (settings.c) */
#else
#  define ASSET_DIR     "assets/"
#  define SAVE_DIR      "./save/"
#endif

/* Maps importées par le joueur : un dossier par map (voir chart.h), à côté des sauvegardes */
#define USER_SONGS_DIR SAVE_DIR "songs/"
#define IMPORT_DIR     SAVE_DIR "import/"

#define ASSET_PATH(p) ASSET_DIR p /* littéral concaténé : ASSET_PATH("a.png") */

#endif
