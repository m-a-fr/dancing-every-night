#ifndef GAME_H
#define GAME_H
#include <stdbool.h>
#include "input.h"
#include "sdl.h"
#include "rhythm.h"
#include "audio.h"
#include "font.h"
#include "sprites.h"
#include "background.h"
#include "menu.h"
#include "title.h"
#include "sfx.h"
#include "settings.h"
#include "calib.h"

/* Titre -> menu principal (BEATMAPS / OPTIONS / QUIT) -> sélection de chanson (SCREEN_MENU) -> jeu ;
   OPTIONS -> calibration */
typedef enum { SCREEN_TITLE, SCREEN_MAIN, SCREEN_MENU, SCREEN_OPTIONS, SCREEN_CALIB, SCREEN_PLAY } Screen;

/* Tout l'état du jeu vit ici. Pas de variables globales dans la logique de jeu. */
typedef struct {
    SDL_Renderer *renderer;       /* appartient à main.c ; sert à charger les textures d'une chanson */
    Screen      screen;
    Menu        menu;
    Title       title;            /* écran d'accueil */
    Sfx         sfx;              /* bruitages et musique des menus (périphérique fermé pendant une chanson) */
    Font        font;
    Sprites     sprites;
    /* Chanson en cours (valide si song_loaded) */
    RhythmState rhythm;
    Audio       audio;
    Background  background;
    bool        song_loaded;
    int         loading;          /* > 0 : chargement en cours (nombre d'images restant à afficher avant la fin) */
    bool        audio_started;
    int         entry;            /* index dans menu.entries, -1 si carte hors menu */
    bool        result_recorded;  /* score de fin déjà enregistré */
    bool        new_best;
    bool        paused;
    int         pause_sel;        /* option du menu pause : PAUSE_RESUME / RESTART / MENU */
    float       countdown_ms;     /* > 0 : compte à rebours avant reprise (jeu figé) */
    int         countdown_n;      /* chiffre affiché du compte à rebours (bruitage à chaque changement) */
    bool        wants_quit;
    float       time;             /* secondes depuis le lancement : animations d'UI (clignotements, pulsations) */
    bool        fixed_clock;      /* outil d'enregistrement : l'horloge de jeu ne suit pas l'audio (images rendues hors temps réel) */
    float       pause_cursor;     /* position affichée de la sélection du menu pause (suit pause_sel) */
    Settings    settings;         /* réglages du joueur (décalage audio), sauvegardés */
    int         main_sel;         /* menu principal : MAIN_BEATMAPS / OPTIONS / QUIT */
    float       main_cursor;
    int         opt_sel;          /* options : OPT_OFFSET / OPT_CALIBRATE */
    float       opt_cursor;
    Calib       calib;            /* écran de calibration (périphérique audio ouvert seulement sur cet écran) */
} Game;

/* chart_path : NULL = ouvre le menu ; sinon lance directement cette carte (desktop : ./vitagame carte.chart) */
void game_init(Game *g, SDL_Renderer *r, const char *chart_path);
void game_shutdown(Game *g);
void game_update(Game *g, const Input *in, float dt);
void game_draw(const Game *g, SDL_Renderer *r);

#endif
