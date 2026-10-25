#ifndef MENU_H
#define MENU_H
#include <stdbool.h>
#include "sdl.h"
#include "font.h"
#include "input.h"
#include "background.h"
#include "chart.h"
#include "bgcache.h"
#include "preview.h"
#include "sprites.h"

/* Une carte jouable (une difficulté d'une chanson), lue dans assets/charts/ */
typedef struct {
    char  path[CHART_PATH];
    char  folder[CHART_PATH];  /* dossier de la map (avec le '/' final) : tout ce qui appartient à la map */
    char  score_key[96];   /* clé du record : « <ID>:<DIFFICULTÉ> » (survit aux renommages et aux réimports) */
    char  id[CHART_ID_MAX];    /* ID de la map ; les cartes d'un même ID forment un morceau */
    bool  imported;        /* map importée par le joueur (supprimable), sinon officielle (dans le jeu) */
    long long file_size, file_mtime;   /* du .chart : une ligne de l'index en cache n'est valable que s'ils sont inchangés */
    char  title[64];
    char  artist[64];
    char  pack[64];        /* pack du morceau (entête PACK), vide = sans pack */
    char  difficulty[24];
    int   order;           /* ORDER de la carte (1 = première difficulté du morceau), 0 = ordre par défaut */
    float bpm;
    int   offset_ms;       /* position du temps 0 dans la piste (calage des animations au tempo) */
    int   notes;
    int   best_score;      /* 0 = jamais joué */
    char  background[CHART_PATH]; /* image de fond de la chanson (chemin complet), vide = aucune */
    int   background_dim;
    char  audio[CHART_PATH];      /* piste (chemin complet), vide = aucune : pas d'aperçu musical */
    int   preview_ms;      /* début de l'aperçu musical dans la piste (refrain) */
} MenuEntry;

/* Ligne du fichier des records */
typedef struct { char key[96]; int score; } MenuScore;

/* Une chanson = plage d'entrées consécutives partageant le même titre */
typedef struct { int first; int count; } MenuSong;

/* Un pack = morceaux partageant le même nom de pack. name vide = SINGLES (morceaux sans pack). */
typedef struct {
    char name[64];
    int  song_count;
    int  first_entry;      /* première carte du premier morceau : fond d'écran et aperçu du pack */
} MenuPack;

/* Deux onglets, affichés seulement s'il existe au moins un pack nommé :
   ALL SONGS = tous les morceaux ; PACKS = niveau 1 la liste des packs, niveau 2 les morceaux du pack ouvert.
   Une map importée se supprime avec Triangle depuis n'importe quelle liste de morceaux. */
typedef enum { MENU_TAB_ALL, MENU_TAB_PACKS, MENU_TAB_COUNT } MenuTab;

typedef struct {
    MenuEntry *entries;    /* toutes les cartes, triées par titre puis difficulté (tableau dynamique) */
    int       entry_count;
    MenuSong  *songs;      /* morceaux listés : tous (hors d'un pack) ou ceux du pack ouvert */
    int       song_count;
    MenuPack  *packs;      /* packs nommés triés par nom, puis SINGLES s'il y a des morceaux sans pack */
    MenuEntry *index;          /* index en cache, le temps de menu_init (voir menu.c) */
    int        index_count;
    bool       index_dirty;
    MenuScore *other_scores;   /* records du fichier sans carte installée (map supprimée...) : réécrits tels quels */
    int        other_score_count;
    int       pack_count;
    bool      has_packs;   /* au moins un pack nommé : sinon pas d'onglet PACKS */
    bool      confirm_delete; /* fenêtre « supprimer cette map ? » ouverte sur la chanson sélectionnée */
    bool      just_deleted;   /* une map vient d'être supprimée (bruitage, remis à false par le jeu) */
    MenuTab   tab;
    bool      in_pack;     /* onglet PACKS : false = liste des packs (niveau 1), true = morceaux d'un pack (niveau 2) */
    int       pack;        /* pack sélectionné / ouvert */
    int       song;        /* chanson sélectionnée */
    int       diff;        /* difficulté sélectionnée dans la chanson */
    bool      start_requested;
    bool      back_requested;   /* Rond hors d'un pack : retour au menu principal (lu par le jeu) */
    /* Image de fond de la sélection : décodée en arrière-plan (cache + anticipation des voisines), affichée dès
       qu'elle est prête ; tant qu'elle ne l'est pas (défilement très rapide), l'image précédente reste. */
    SDL_Renderer *renderer;
    BgCache    bgc;
    BgCache    thumbs;      /* pochettes de la fiche d'un pack : vignettes 72x41 décodées en arrière-plan */
    char       bg_shown[BGCACHE_PATH];   /* image affichée ("" = aucune) */
    const Background *bg;                /* dans bgc, NULL = aucune ; mis à jour par menu_update */
    Preview    preview;     /* extrait musical de la sélection, joué en boucle */
    /* Animations (état d'affichage, avancé par menu_update) */
    float      time;        /* secondes depuis l'ouverture : pulsation de la sélection */
    float      cursor;      /* position affichée de la cascade, en rangs (rejoint la sélection ; peut sortir de
                               0..count-1 pour boucler par le chemin le plus court) */
    float      info_t;      /* apparition de la fiche : 1 = affichée (changement instantané, sans fondu) */
    int        info_song;   /* élément (chanson ou pack) affiché dans la fiche, -1 = à renouveler */
    float      change_t;    /* secondes depuis le dernier changement de sélection (éclaboussure, gerbe, décor) */
    int        deco_prev;   /* élément précédent : le décor glisse de ses positions vers les nouvelles (-1 = aucun) */
    float      beat_ms;     /* horloge du tempo, en ms dans la piste ; recalée sur l'extrait musical quand il joue */
} Menu;

/* Scanne assets/charts/ et charge les meilleurs scores */
void menu_init(Menu *m, SDL_Renderer *r);
/* Libère les tableaux du menu et son image de fond */
void menu_shutdown(Menu *m);
/* Relit les chansons installées (après une installation depuis la bibliothèque) : la sélection revient au début */
void menu_reload(Menu *m);
void menu_update(Menu *m, const Input *in, float dt);
/* Avant de lancer une chanson : coupe l'aperçu musical (libère le périphérique audio) et les images de fond
   (VRAM). Tout revient au prochain menu_update. */
void menu_suspend(Menu *m);
/* Retourne la carte choisie (Croix) une seule fois, sinon NULL */
const MenuEntry *menu_take_selection(Menu *m);
/* Index de l'entrée dont le chemin est `path`, -1 sinon */
int  menu_find_entry(const Menu *m, const char *path);
/* Enregistre le score s'il bat le meilleur (sauvegarde sur disque). Retourne true si nouveau record. */
bool menu_record_score(Menu *m, int entry, int score);
void menu_draw(const Menu *m, SDL_Renderer *r, const Font *font, const Sprites *sprites);

#endif
