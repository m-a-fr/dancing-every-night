#ifndef RHYTHM_H
#define RHYTHM_H
#include <stdbool.h>
#include "chart.h"
#include "input.h"
#include "sdl.h"
#include "font.h"
#include "sprites.h"
#include "hostess.h"
#include "background.h"

/* Silence avant t=0 pour laisser les premières notes arriver depuis le centre */
#define SONG_LEAD_IN_MS 2500

typedef enum { JUDGE_NONE, JUDGE_PERFECT, JUDGE_GOOD, JUDGE_MISS } Judgment;

typedef struct {
    bool     judged;      /* jugement de démarrage (tap, ou début de note tenue) attribué */
    Judgment judgment;
    bool     hold_active; /* note tenue : maintien en cours */
    bool     hold_broken; /* relâchée trop tôt */
    bool     hold_done;   /* maintenue jusqu'au bout */
    bool     a_hit, b_hit; /* double note : chaque lane a été frappée dans la fenêtre */
    Judgment ja, jb;      /* jugement de chaque frappe d'une double note */
} NoteRuntime;

/* État d'affichage de la note tenue en cours sur une lane (pour l'anneau de
   compte à rebours), tenu à part pour ne pas rechercher dans le chart au
   dessin de chaque frame. */
typedef struct {
    bool  active;
    float hit_time_ms;
    float duration_ms;
} LaneHold;

typedef struct {
    Chart        chart;
    bool         chart_loaded;
    NoteRuntime *note_runtime; /* un par note du chart, alloué à l'init */
    float        approach_ms;     /* temps de trajet des notes (de la carte, ou défaut de sa difficulté) */
    float        song_time_ms;
    int          combo;
    int          score;
    int          flat_points;         /* points « plats » (300 / 100 par jugement, sans le combo) : précision et rang */
    int          combo_breaks;        /* nombre de fois où le combo a été cassé (0 = Full Combo) */
    float        lane_flash_timer[BTN_COUNT];    /* > 0 = flash de jugement en cours sur cette lane */
    Judgment     lane_flash_judgment[BTN_COUNT];
    unsigned     lane_flash_seed[BTN_COUNT];     /* graine de l'effet de réussite (éclats, toujours les mêmes pour un même appui) */
    unsigned     hit_counter;                    /* nombre d'effets lancés : fournit les graines */
    float        lane_press_timer[BTN_COUNT];    /* > 0 = anneau d'appui en cours, indépendant du jugement */
    bool         lane_held[BTN_COUNT];           /* bouton physiquement enfoncé cette frame (effet d'empreinte) */
    LaneHold     lane_hold[BTN_COUNT];
    /* Statistiques et affichage */
    int          judgment_count[4];   /* indexé par Judgment (PERFECT/GOOD/MISS) */
    int          max_combo;
    bool         finished;            /* toutes les notes résolues : écran de résultats */
    Judgment     popup_judgment;      /* dernier jugement affiché au centre */
    const char  *popup_text;
    float        popup_timer;         /* ms restantes */
    /* Animations du HUD et des résultats (avancées par rhythm_update, lues par rhythm_draw) */
    float        score_display;       /* score affiché : rejoint score en défilant */
    float        combo_pulse_ms;      /* > 0 : le nombre du combo grossit (note réussie) */
    float        results_ms;          /* temps écoulé depuis l'arrivée sur l'écran de résultats */
    float        song_end_ms;         /* fin de la dernière note : 100 % de la barre de progression */
    /* Évènements sonores de l'image, remplis par rhythm_update et vidés par le jeu (qui joue les bruitages) */
    int          snd_judgment[4];     /* jugements de tap / tenue, indexé par Judgment */
    int          snd_scratch;         /* scratches réussies */
} RhythmState;

void rhythm_init(RhythmState *rh, const char *chart_path);
/* Géométrie des arcs de l'écran de jeu (pour préparer leurs images : sprites_build_arcs) */
void rhythm_arc_geometry(float *left_cx, float *right_cx, float *cy, float *rx, float *ry);
void rhythm_shutdown(RhythmState *rh);
/* Remet la carte à zéro (temps, score, notes) */
void rhythm_restart(RhythmState *rh);
void rhythm_update(RhythmState *rh, const Input *in, float dt);
/* Outil de dev (enregistrement de vidéos) : remplit `out` avec les appuis parfaits pour la prochaine
   rhythm_update(rh, out, dt). N'écrit que les lanes ; le reste de `out` est laissé tel quel. */
void rhythm_autoplay(const RhythmState *rh, float dt, Input *out);
/* Recale doucement l'horloge de jeu sur l'horloge audio (évite la dérive sur les longues pistes). */
void rhythm_sync_time(RhythmState *rh, float target_ms);
/* best_score : record de la carte (après enregistrement de cette partie), -1 = inconnu (carte hors menu) ;
   new_best : cette partie vient de l'établir. Affichés sur l'écran de résultats. */
/* night : numéro du morceau dans la liste du menu (bandeau « NIGHT nn »), 0 = inconnu */
void rhythm_draw(const RhythmState *rh, SDL_Renderer *r, const Font *font, const Sprites *sprites, const Background *bg,
                 const Hostess *host, int best_score, bool new_best, int night);
/* Rang de la partie finie : 0 = S, 1 = A, 2 = B, 3 = C, 4 = D (précision des jugements, comme l'écran de résultats) */
int rhythm_rank(const RhythmState *rh);
/* Instant (ms depuis la fin de la partie) où le tampon du rang tombe sur l'écran de résultats */
#define RESULTS_RANK_MS 1750.0f

#endif
