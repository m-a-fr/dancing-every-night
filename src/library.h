#ifndef LIBRARY_H
#define LIBRARY_H
#include <stdbool.h>
#include "sdl.h"
#include "font.h"
#include "sprites.h"
#include "input.h"
#include "chart.h"
#include "net.h"
#include "bgcache.h"

/* Bibliothèque de chansons dans le jeu : le catalogue en ligne (catalog.json du site, voir tools/catalog.py), parcouru
   dans une liste dense avec la fiche de la chanson sélectionnée. Le catalogue est lu à chaque ouverture de l'écran ;
   sans réseau, il n'y a pas de catalogue (message « connexion requise », pas de copie gardée).
   Chaque chanson porte un indice de son état dans la bibliothèque du joueur : absente, installée, mise à jour
   disponible (comparaison de l'ID et de la VERSION du catalogue avec les dossiers songs/<ID>/ installés).
   Hors Vita, VITAGAME_LIBRARY_JSON=fichier.json lit un catalogue local (essai de l'affichage, sans réseau). */

#define LIB_MAX_SONGS  2000
#define LIB_MAX_DIFFS  6

typedef struct { char name[24]; int notes; } LibDiff;

enum { LIBTH_NONE, LIBTH_LOADING, LIBTH_READY, LIBTH_FAILED };

typedef enum { LIB_NEW, LIB_INSTALLED, LIB_UPDATE } LibState;   /* absente / installée à jour / version plus récente */

typedef struct {
    char  id[CHART_ID_MAX];        /* identifiant de publication = nom du dossier songs/<ID>/ */
    char  local_id[CHART_ID_MAX];  /* ID d'origine de l'auteur (chanson déjà installée sous ce nom) ; peut être vide */
    int   version;
    char  title[64], artist[64], pack[64], mapper[40];
    char  added[12];               /* « 2026-10-25 » */
    LibDiff diffs[LIB_MAX_DIFFS];
    int   diff_count;
    int   duration_s;
    int   zip_bytes;
    char  zip[96];                 /* clé relative au serveur de téléchargement */
    char  sha256[65];
    char  thumb[96];
    /* état local */
    int   thumb_state;             /* LIBTH_* : la vignette est-elle téléchargée (thread principal seulement) */
    LibState state;
    int   installed_version;       /* 0 = inconnue (installée sous son ID d'origine) */
} LibSong;

typedef enum { LIBST_IDLE, LIBST_LOADING, LIBST_READY, LIBST_OFFLINE, LIBST_FAILED, LIBST_UNSUPPORTED } LibStatus;
typedef enum { LIBF_ALL, LIBF_NEW, LIBF_INSTALLED, LIBF_UPDATE, LIBF_COUNT } LibFilter;
typedef enum { LIBS_NEWEST, LIBS_TITLE, LIBS_ARTIST, LIBS_COUNT } LibSort;

/* Événements d'un library_update, pour les bruitages et la navigation du jeu */
enum { LIB_EV_CURSOR = 1, LIB_EV_BACK = 2, LIB_EV_CHANGE = 4, LIB_EV_CONFIRM = 8,
       LIB_EV_INSTALLED = 16 };   /* une chanson vient d'être installée : le jeu relit sa liste de chansons */

/* Installation d'une chanson : téléchargement, vérification de l'empreinte, décompression. Un seul travail à la fois. */
typedef enum { LIBJ_IDLE, LIBJ_DOWNLOAD, LIBJ_VERIFY, LIBJ_INSTALL, LIBJ_DONE, LIBJ_FAILED } LibJob;

typedef struct {
    SDL_atomic_t status;    /* LibStatus : écrit en dernier par le fil, songs / song_count sont alors prêts */
    LibSong *songs;
    int      song_count;
    char     base[160];     /* serveur de téléchargement (zips, vignettes) */
    /* installation en cours : champs écrits avant le démarrage du fil, puis seulement job (atomique) et job_err (en dernier) */
    SDL_atomic_t job;       /* LibJob */
    NetProgress  prog;
    char     job_id[CHART_ID_MAX];
    char     job_url[320];
    char     job_sha[65];
    int      job_bytes;
    char     job_err[96];   /* raison de l'échec (LIBJ_FAILED) */
    /* vignettes : téléchargées une à une (la sélection d'abord, puis ses voisines) dans un dossier temporaire vidé à
       chaque ouverture, décodées par un BgCache. Le fil n'écrit que dans ces champs (jamais dans songs). */
    BgCache  thumbs;
    SDL_atomic_t thumb_busy;   /* 0 libre, 1 en cours, 2 terminé (résultat dans thumb_ok, à lire par le thread principal) */
    SDL_atomic_t thumb_ok;
    NetProgress  thumb_prog;
    int      thumb_idx;        /* indice dans songs de la vignette en cours */
    int      thumb_gen, gen;   /* gen change à chaque ouverture : un résultat d'un catalogue précédent est ignoré */
    char     thumb_url[320];
    char     thumb_file[160];
    /* vue (thread principal seulement) */
    bool     prepared;      /* états et ordre calculés pour le catalogue reçu */
    LibFilter filter;
    LibSort  sort;
    int     *order;         /* indices dans songs des chansons affichées */
    int      shown;
    int      sel;           /* rang dans order */
    int      top;           /* premier rang visible (cible) */
    float    scroll;        /* premier rang affiché (rejoint top) */
    float    hold;          /* durée d'appui haut / bas : répétition */
    float    time;
} Library;

/* Allocation unique : un fil encore en attente du réseau à la fermeture utilise la structure, qui n'est alors pas libérée */
Library *library_create(SDL_Renderer *r);
void library_destroy(Library *l);
/* (Re)lit le catalogue en arrière-plan : à l'ouverture de l'écran */
void library_open(Library *l);
LibStatus library_status(const Library *l);
/* Une installation est en cours (téléchargement, vérification ou décompression) : l'écran ne se quitte pas */
bool library_busy(const Library *l);
/* Retourne un masque de LIB_EV_* */
int  library_update(Library *l, const Input *in, float dt);
/* Dessine la liste et la fiche (le fond et l'en-tête sont ceux du jeu) */
void library_draw(const Library *l, SDL_Renderer *r, const Font *font, const Sprites *sprites);

/* Lit le catalogue JSON (testable sans réseau) : nombre de chansons lues, 0 si le JSON est inutilisable.
   Les identifiants, clés de zip et de vignette sont validés (pas de chemin hors du dossier voulu). */
int  library_parse(const char *json, LibSong *out, int max, char *base, size_t base_size);

#endif
