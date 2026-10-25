#include "rhythm.h"
#include "shapes.h"
#include "ui.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979323846f

/* Arcs elliptiques (rx < ry) : rx contrôle à quel point l'arc "bombe" vers le centre, ry sa portée verticale
   (icônes du haut/bas à 30°/150° d'un côté, 210°/330° de l'autre — cos(30°) ≈ 0.866). Géométrie relevée sur
   le jeu de référence (vidéos de art/refs/ ramenées à 960x544, 2026-10-13) : cibles du milieu à 64 px du bord,
   cibles des coins à (108, 88) et (108, 456) en miroir, arcs presque droits. */
#define ICON_RADIUS      26.0f
#define ARC_MID_TARGET_X 64.0f   /* centre des cibles Gauche / Rond, depuis le bord de l'écran */
#define ARC_RX           88.0f
#define ARC_RY          213.0f
#define ARC_LEFT_CX     (ARC_MID_TARGET_X + ARC_RX)
#define ARC_RIGHT_CX    (SCREEN_W - ARC_LEFT_CX)
#define ARC_CY (SCREEN_H / 2.0f)

/* Notes qui approchent : elles partent du centre de l'écran et voyagent en
   ligne droite jusqu'à leur icône. Réglages ici pour ajuster la difficulté
   sans toucher au reste du code. */
/* Apparition à la manière du jeu de référence : chaque note naît sur une copie réduite des arcs, rapprochée du
   centre de l'écran (homothétie de centre = centre de l'écran), donc à un endroit propre à sa lane ;
   elle est un peu plus petite au départ et grandit jusqu'à sa taille finale en arrivant sur la cible. */
/* Point d'apparition des notes : homothétie de la cible vers le centre, mais pas la même sur les deux axes
   (distance à l'axe horizontal divisée par deux ; distance verticale au centre réduite moins fort, pour garder
   un peu l'étalement d'origine). 0 = toutes les notes partent du centre ; 1 = elles partent de leur cible. */
#define NOTE_SPAWN_SCALE_X 0.21f  /* mesuré dans le jeu de référence : les notes naissent à 21 % du trajet, sur les deux axes */
#define NOTE_SPAWN_SCALE_Y 0.21f
#define NOTE_SPAWN_SCALE_RING ((NOTE_SPAWN_SCALE_X + NOTE_SPAWN_SCALE_Y) / 2.0f) /* l'anneau est une seule ellipse partagée : sa taille de départ est la moyenne des deux */
/* Comme dans le jeu original : un trait fin mais franchement visible dès le départ de la note, pour lire
   l'ordre des notes proches comme des cercles concentriques (le plus grand arrive en premier). */
#define NOTE_RING_ALPHA_MIN 0.40f /* opacité de l'anneau à son apparition */
#define NOTE_RING_ALPHA     0.80f /* opacité maximale de l'anneau, atteinte pile à la cible */
#define NOTE_RING_WIDTH    4.0f  /* épaisseur du cœur plein, centrée sur la trajectoire des notes */
#define NOTE_RING_MIST     5.0f  /* largeur du flou (fondu vers le transparent) de part et d'autre du cœur */
#define NOTE_RING_START_P  0.05f /* progression du trajet à laquelle l'anneau apparaît (léger retard sur la note, jamais tardif) */
#define NOTE_START_SIZE  0.59f /* taille juste après l'apparition (1 = disque de 54 px) : 32 px mesurés dans le jeu de référence */
/* Hiérarchie : la prochaine note de chaque lane est pleinement visible ; les suivantes (même lane) sont
   atténuées, ainsi que leur anneau d'approche. */
#define NOTE_LATER_ALPHA      0.60f /* opacité des notes qui ne sont pas les prochaines de leur lane */
#define NOTE_RING_LATER_ALPHA 0.45f /* facteur sur l'anneau d'approche de ces notes */
#define NOTE_FINAL_SIZE  1.06f /* taille sur cible : 57 px mesurés dans le jeu de référence, un peu moins que l'anneau de la cible */
#define NOTE_SIZE_CURVE  1.0f  /* croissance : taille = départ + écart x p^courbe ; le jeu de référence est linéaire (1) */
#define NOTE_POP_P       0.07f /* apparition « pop » (jeu de référence) : la note naît plus petite et rattrape sa taille... */
#define NOTE_POP_SCALE   0.75f /* ...sur ce début de trajet, depuis cette fraction (24 px -> 32 px mesurés) */
#define NOTE_HALO_SCALE  1.30f /* halo coloré derrière la note : disque de cette taille relative... */
#define NOTE_HALO_ALPHA  70    /* ...à cette opacité (0-255) : détache la note du fond vidéo */
#define NOTE_BORDER_VIVID 1.8f /* contour : couleur de la lane écartée du gris de ce facteur (plus vive) */
/* Essai (jeu original) : la note "oscille" en taille pendant le trajet. Amplitude à 0 = désactivé.
   S'annule pile à p = 1 (facteur (1-p)) : ne change jamais la taille sur cible. */
/* Pulsation au BPM (mesurée dans le jeu de référence le 2026-10-20, image par image) : la taille ne grandit pas en continu mais par
   marches, toutes les notes ensemble à chaque temps de la musique ; entre deux temps elle reste celle du dernier temps.
   Chaque marche dépasse un peu puis se pose : une note frappée sur le temps arrive au sommet de sa pulsation. */
#define NOTE_BEAT_POP       0.08f   /* dépassement au temps (+8 %), qui retombe pendant le temps */
#define NOTE_BEAT_DECAY     2.5f    /* vitesse de retombée (exp(-DECAY x fraction du temps)) : douce après la frappe */
#define NOTE_BEAT_RISE_FRAC 0.35f   /* la note gonfle sur ce dernier tiers du temps (montée en S), sommet pile sur le
                                       temps : un saut de 50 ms (jeu de référence) se voyait trop tard, on la frappait encore « basse » */
#define NOTE_FADE_IN_P   0.12f /* fondu d'apparition sur ce début de trajet (0 = pas de fondu) */
/* Note "scratch" (d'après le jeu de référence, cf. art/refs/) : pas de cible fixe, une bande bleue épaisse qui
   grossit du centre jusqu'aux arcs, avec le motif « SCRATCH >> » écrit le long du cercle. Le motif tourne dans
   le sens à donner au stick et les chevrons pointent dans ce sens. Bleu : ni le turquoise ni le magenta des
   anneaux d'approche, et une forme (bande + texte) qu'aucune autre note n'a. */
#define SCRATCH_RING_WIDTH  24.0f   /* épaisseur de la bande à l'arrivée (réduite avec l'anneau au départ) */
#define SCRATCH_FILL        ((SDL_Color){ 34,  98, 228, 255})
#define SCRATCH_EDGE        ((SDL_Color){150, 200, 255, 255})
#define SCRATCH_GROUPS      8       /* motifs (nom ou consigne, puis chevrons) sur le tour */
#define SCRATCH_NAME        "SCRATCH"      /* le nom de la note... */
#define SCRATCH_LABEL       "R  OR  L"     /* ...et ce que le joueur doit faire (une gâchette, ou toucher un stick), en alternance */
#define SCRATCH_SPIN_SPEED  0.09f   /* rotation du motif, degrés / ms (un tour en 4 s), dans le sens du stick */
#define NOTE_BUTTON_SPRITE_SCALE 0.68f /* symbole bouton réduit dans le disque, pour ne pas toucher le contour */
#define NOTE_ARROW_SPRITE_SCALE  0.68f /* flèches au trait : même réduction que les symboles bouton */
#define MISS_NOTE_ALPHA    0.40f /* opacité d'une note ratée qui dépasse la cible (tamisée) */
#define MISS_FADE_OUT_MS 150.0f /* elle s'efface sur les dernières ms de sa course */
#define TARGET_INK_RADIUS 33.0f /* disque d'encre sous chaque cible : les arcs passent dessous */
#define CENTER_X (SCREEN_W / 2.0f)
#define CENTER_Y (SCREEN_H / 2.0f)
#define WINDOW_PERFECT_MS       50.0f
#define WINDOW_GOOD_MS         120.0f
/* Scratch : fenêtres plus larges (un coup de stick est moins précis qu'un bouton sur Vita, et n'est détecté qu'une
   fois le stick bien poussé). Toujours centrées sur l'instant où le milieu de la bande est sur les arcs. */
#define SCRATCH_WINDOW_PERFECT_MS 80.0f
#define SCRATCH_WINDOW_GOOD_MS   170.0f
#define MISS_OVERSHOOT_MS      300.0f /* durée pendant laquelle une note ratée continue sa course au lieu de s'arrêter net sur la cible */
#define FLASH_DURATION_MS      300.0f
/* Notes liées « Cascade pop » (option B du 2026-10-19, d'après les notes « U » du jeu de référence, révisée selon
   l'utilisateur) : deux têtes IDENTIQUES à celles des autres notes (halo, disque sombre, contour vers l'extérieur) mais
   au coloris mélangé du cyan (croix directionnelle) et du magenta (boutons) — comme le vert vient du jaune et du
   bleu, soit un violet-bleu vif — avec le symbole de la touche en blanc, reliées par une barre fine du même coloris,
   sans contour sombre (retiré le 2026-10-25 : les autres notes n'en ont pas), qui s'évase en rejoignant le contour de chaque tête. Essai rejeté : bouts pleins sans disque sombre
   ni contour, trop différents des autres notes. */
#define LINK_MIX ((SDL_Color){128, 78, 240, 255})   /* teinte à mi-chemin du cyan (197°) et du magenta (326°) : ~261° */
#define LINK_HEAD_R            (NOTE_DISC_RADIUS + NOTE_BORDER_WIDTH)   /* rayon d'une tête, contour compris (px à pleine taille) */
#define LINK_BAR_HALF           6.5f   /* demi-largeur de la barre */
#define LINK_FLARE              9.0f   /* longueur de l'évasement entre la barre et chaque tête */
/* Retour d'une note réussie « Cascade pop » (plan A + D du 2026-10-19, révisé selon l'utilisateur) :
   - sur la cible, le disque coloré selon le jugement d'avant (inchangé) ;
   - D : le tronçon d'arc voisin s'éclaire et l'éclat court le long de l'arc ;
   - A : des étoiles et des éclats (losanges) pseudo-aléatoires scintillent autour de la cible, jamais par-dessus
     l'icône : chacun tourne sur lui-même (pas autour de la cible), s'éloigne légèrement d'elle et s'estompe en douceur.
     Environ 30 % d'étoiles de plus que d'éclats. PERFECT : plus nombreux, citron, blanc et couleur du côté (cyan à
     gauche, rose à droite) ; GOOD : moins nombreux, plus petits, blancs. MISS : seulement le disque rose d'avant. */
#define HIT_STARS_PERFECT       4
#define HIT_STARS_GOOD          3
#define HIT_SHARDS_PERFECT      3
#define HIT_SHARDS_GOOD         2
#define HIT_FLASH_MS          220.0f   /* disque coloré de la cible (durée d'origine) */
#define HIT_STAR_MIN_R         40.0f   /* les étoiles restent en dehors du liseré de la cible (rayon 32 + halo) */
#define HIT_STAR_MAX_R         62.0f
#define HIT_STAR_SIZE_PERFECT  18.0f   /* diamètre moyen (px) */
#define HIT_STAR_SIZE_GOOD     13.0f
#define HIT_STAR_LIFE           0.55f  /* durée de vie d'une étoile ou d'un éclat, en part du temps de l'effet */
#define HIT_DRIFT_PX           14.0f   /* éloignement de la cible pendant la vie d'un élément */
#define HIT_FADE_FROM           0.5f   /* part de la vie à partir de laquelle l'élément s'estompe (jusqu'à 0 à la fin) */
#define HIT_ARC_MS            130.0f   /* l'arc voisin s'éclaire pendant ce temps */
#define HIT_ARC_REACH_DEG      16.0f   /* l'éclat s'étend jusqu'à cet angle de part et d'autre de la cible */
#define PRESS_FLASH_DURATION_MS 150.0f /* anneau d'appui, indépendant du jugement */
/* Notes tenues : capsule de la tête à la note de fin, comme les tenues « H » du jeu de référence */
#define HOLD_CAPSULE_IDLE_FILL  30      /* opacité du remplissage en approche (jeu de référence : vide ; un voile aide sur nos fonds chargés) */
#define HOLD_CAPSULE_HELD_FILL  130     /* opacité du remplissage pendant le maintien */
#define HOLD_SPARKLES           7       /* paillettes qui remontent la capsule pendant le maintien */
#define HOLD_SPARKLE_SPEED      0.0007f /* longueurs de capsule par ms */
#define POPUP_DURATION_MS       600.0f
#define POPUP_POP_MS            120.0f /* le jugement apparaît agrandi puis revient à sa taille */
#define COMBO_PULSE_MS          150.0f
/* Score à la manière du jeu de référence (mesuré sur les vidéos de art/refs : ~33 points x combo par note, sans plafond) :
   chaque note rapporte base x combo en cours, donc un combo cassé coûte cher et les dernières notes pèsent
   lourd. Le combo pris en compte est plafonné pour borner le score (HUD à 7 chiffres) et pour qu'une erreur
   dans une très longue carte reste rattrapable. Sur les cartes actuelles (80 à 930 notes), le score maximal
   va d'environ 100 000 à 7 millions. */
#define SCORE_PERFECT            30
#define SCORE_GOOD               10
#define SCORE_COMBO_CAP         300
/* Points « plats » (sans le combo) : base de la précision et du rang, indépendants du modèle de score */
#define ACC_PERFECT             300
#define ACC_GOOD                100
/* Écran de résultats : fondu, décompte des points, puis révélation du rang */
#define RESULTS_FADE_MS         300.0f
#define RESULTS_COUNT_START_MS  350.0f
#define RESULTS_COUNT_MS       1200.0f
#define RESULTS_RANK_MS        1750.0f
#define RESULTS_RANK_POP_MS     280.0f
#define RESULTS_DELAY_MS       1200.0f /* pause après la dernière note avant l'écran de résultats */

/* Disposition en deux arcs (à la manière du jeu de référence) : boutons face sur un
   arc à droite (1h/3h/5h), croix directionnelle sur un arc à gauche
   (7h/9h/11h). Voir GAME_DESIGN.md. angle_deg : 0 = midi, sens horaire,
   relatif au centre de son propre arc. */
typedef enum { ARC_LEFT, ARC_RIGHT } ArcSide;

typedef struct {
    Button  button;
    ArcSide side;
    float   angle_deg;
} LaneSlot;

static const LaneSlot LANES[] = {
    { BTN_TRIANGLE, ARC_RIGHT,  30.0f }, /* 1h */
    { BTN_CIRCLE,   ARC_RIGHT,  90.0f }, /* 3h */
    { BTN_CROSS,    ARC_RIGHT, 150.0f }, /* 5h */
    { BTN_DOWN,     ARC_LEFT,  210.0f }, /* 7h */
    { BTN_LEFT,     ARC_LEFT,  270.0f }, /* 9h */
    { BTN_UP,       ARC_LEFT,  330.0f }, /* 11h */
};
#define LANE_COUNT (int)(sizeof LANES / sizeof LANES[0])

static const LaneSlot *find_lane_slot(Button lane) {
    for (int i = 0; i < LANE_COUNT; i++)
        if (LANES[i].button == lane) return &LANES[i];
    return NULL;
}

static void arc_center(ArcSide side, float *cx, float *cy) {
    *cx = (side == ARC_RIGHT) ? ARC_RIGHT_CX : ARC_LEFT_CX;
    *cy = ARC_CY;
}

static void lane_pos(ArcSide side, float angle_deg, float rx, float ry, float *x, float *y) {
    float cx, cy;
    arc_center(side, &cx, &cy);
    float a = angle_deg * PI_F / 180.0f;
    *x = cx + sinf(a) * rx;
    *y = cy - cosf(a) * ry;
}

/* Couleur de chaque lane (traînes des notes tenues), reprise des sprites de notes */
static SDL_Color lane_color(Button lane) {
    switch (lane) {
    case BTN_TRIANGLE: return (SDL_Color){8, 191, 181, 255};
    case BTN_CIRCLE:   return (SDL_Color){214, 92, 92, 255};
    case BTN_CROSS:    return (SDL_Color){130, 144, 186, 255};
    case BTN_UP:       return (SDL_Color){174, 220, 217, 255};
    case BTN_LEFT:     return (SDL_Color){225, 178, 178, 255};
    case BTN_DOWN:     return (SDL_Color){180, 187, 208, 255};
    default:           return (SDL_Color){200, 200, 210, 255};
    }
}

static SDL_Color judgment_color(Judgment j) {
    switch (j) {
    case JUDGE_PERFECT: return CH_GOLD;
    case JUDGE_GOOD:    return CH_WHITE;
    case JUDGE_MISS:    return CH_ROSE;
    default:            return (SDL_Color){0, 0, 0, 0};
    }
}

static const char *judgment_name(Judgment j) {
    switch (j) {
    case JUDGE_PERFECT: return "PERFECT";
    case JUDGE_GOOD:    return "GOOD";
    case JUDGE_MISS:    return "MISS";
    default:            return "?";
    }
}

static const char *lane_name(Button b) {
    switch (b) {
    case BTN_TRIANGLE: return "TRIANGLE";
    case BTN_CIRCLE:   return "CIRCLE";
    case BTN_CROSS:    return "CROSS";
    case BTN_UP:       return "UP";
    case BTN_LEFT:     return "LEFT";
    case BTN_DOWN:     return "DOWN";
    default:           return "?";
    }
}

/* lane = BTN_COUNT (note scratch, pas de cible physique) ou lane2 = BTN_COUNT (note simple) : pas de
   flash sur cette lane. mult multiplie les points (double note = 2). soft_miss : un MISS ne remet pas le
   combo à zéro (scratch et fin de tenue, que le jeu original ne punit pas non plus). */
static void judge_event(RhythmState *rh, Button lane, Button lane2, Judgment j, int mult, bool soft_miss) {
    switch (j) {
    case JUDGE_PERFECT:
    case JUDGE_GOOD: {
        rh->combo++;
        int factor = rh->combo < SCORE_COMBO_CAP ? rh->combo : SCORE_COMBO_CAP;
        rh->score += (j == JUDGE_PERFECT ? SCORE_PERFECT : SCORE_GOOD) * mult * factor;
        rh->flat_points += (j == JUDGE_PERFECT ? ACC_PERFECT : ACC_GOOD) * mult;
        rh->combo_pulse_ms = COMBO_PULSE_MS;
        break;
    }
    case JUDGE_MISS:
        if (!soft_miss) { rh->combo = 0; rh->combo_breaks++; }
        break;
    default: break;
    }
    if (rh->combo > rh->max_combo) rh->max_combo = rh->combo;
    rh->judgment_count[j]++;
    if (lane >= BTN_COUNT && j != JUDGE_MISS) rh->snd_scratch++;
    else rh->snd_judgment[j]++;
    rh->popup_judgment = j;
    rh->popup_text = judgment_name(j);
    rh->popup_timer = POPUP_DURATION_MS;
    if (lane < BTN_COUNT) {
        rh->lane_flash_timer[lane] = FLASH_DURATION_MS;
        rh->lane_flash_judgment[lane] = j;
        rh->lane_flash_seed[lane] = ++rh->hit_counter;
    }
    if (lane2 < BTN_COUNT) {
        rh->lane_flash_timer[lane2] = FLASH_DURATION_MS;
        rh->lane_flash_judgment[lane2] = j;
        rh->lane_flash_seed[lane2] = ++rh->hit_counter;
    }
    char names[24];
    if (lane >= BTN_COUNT) snprintf(names, sizeof names, "SCRATCH");
    else if (lane2 < BTN_COUNT) snprintf(names, sizeof names, "%s+%s", lane_name(lane), lane_name(lane2));
    else snprintf(names, sizeof names, "%s", lane_name(lane));
    fprintf(stderr, "judge: %-14s %-7s combo=%d score=%d\n", names, judgment_name(j), rh->combo, rh->score);
}

static void judge_note(RhythmState *rh, Button lane, Judgment j) {
    judge_event(rh, lane, BTN_COUNT, j, 1, false);
}

/* Une scratch manquée ne casse pas le combo */
static void judge_scratch(RhythmState *rh, Judgment j) {
    judge_event(rh, BTN_COUNT, BTN_COUNT, j, 1, true);
}

/* Fin d'une note tenue : une note à part entière (mêmes points qu'un tap, combo +1 si réussie), mais la
   rater (lâchée trop tôt) ne casse pas le combo */
static void judge_tail(RhythmState *rh, Button lane, Judgment j) {
    judge_event(rh, lane, BTN_COUNT, j, 1, true);
}

void rhythm_arc_geometry(float *left_cx, float *right_cx, float *cy, float *rx, float *ry) {
    *left_cx = ARC_LEFT_CX; *right_cx = ARC_RIGHT_CX; *cy = ARC_CY; *rx = ARC_RX; *ry = ARC_RY;
}

void rhythm_init(RhythmState *rh, const char *chart_path) {
    *rh = (RhythmState){0};
    rh->song_time_ms = -SONG_LEAD_IN_MS;
    rh->approach_ms = (float)chart_default_approach_ms("");
    rh->chart_loaded = chart_load(&rh->chart, chart_path);
    if (!rh->chart_loaded) {
        fprintf(stderr, "rhythm: impossible de charger la carte '%s'\n", chart_path);
        return;
    }
    rh->approach_ms = (float)rh->chart.approach_ms;
    fprintf(stderr, "rhythm: carte '%s' chargée (%d notes, %.2f bpm, trajet %d ms)\n",
            chart_path, rh->chart.note_count, rh->chart.bpm, rh->chart.approach_ms);
    rh->note_runtime = calloc((size_t)rh->chart.note_count, sizeof(NoteRuntime));
    for (int i = 0; i < rh->chart.note_count; i++) {
        const Note *n = &rh->chart.notes[i];
        float end = (float)(rh->chart.offset_ms + n->time_ms + n->duration_ms);
        if (end > rh->song_end_ms) rh->song_end_ms = end;
    }
    if (rh->chart.end_ms > rh->song_end_ms) rh->song_end_ms = (float)rh->chart.end_ms;   /* END : fin choisie par le mapper */
}

void rhythm_shutdown(RhythmState *rh) {
    free(rh->note_runtime);
    rh->note_runtime = NULL;
    chart_free(&rh->chart);
}

/* Relance la carte depuis le début (Select), pratique pour retester une
   séquence sans fermer/relancer le jeu. */
void rhythm_restart(RhythmState *rh) {
    rh->song_time_ms = -SONG_LEAD_IN_MS;
    rh->combo = 0;
    rh->score = 0;
    rh->max_combo = 0;
    rh->finished = false;
    rh->popup_timer = 0.0f;
    rh->score_display = 0.0f;
    rh->flat_points = 0;
    rh->combo_breaks = 0;
    rh->combo_pulse_ms = 0.0f;
    rh->results_ms = 0.0f;
    memset(rh->judgment_count, 0, sizeof rh->judgment_count);
    for (int i = 0; i < BTN_COUNT; i++) {
        rh->lane_flash_timer[i] = 0.0f;
        rh->lane_flash_seed[i] = 0;
        rh->lane_press_timer[i] = 0.0f;
        rh->lane_held[i] = false;
        rh->lane_hold[i].active = false;
    }
    if (rh->chart_loaded) {
        for (int i = 0; i < rh->chart.note_count; i++)
            rh->note_runtime[i] = (NoteRuntime){0};
    }
    fprintf(stderr, "rhythm: carte relancée\n");
}

void rhythm_autoplay(const RhythmState *rh, float dt, Input *out) {
    if (!rh->chart_loaded) return;
    /* rhythm_update avance l'horloge avant de juger : on vise la frame dont le temps suivant est le plus proche */
    const float next = rh->song_time_ms + dt * 1000.0f, half = dt * 500.0f;
    for (int i = 0; i < rh->chart.note_count; i++) {
        const Note *note = &rh->chart.notes[i];
        const NoteRuntime *nr = &rh->note_runtime[i];
        if (nr->hold_active) { out->held[note->lane] = true; continue; }
        if (nr->judged) continue;
        float hit_time = (float)(rh->chart.offset_ms + note->time_ms);
        if (next < hit_time - half || next >= hit_time + half) continue;
        if (note->type == NOTE_SCRATCH) {
            if (note->scratch_ccw) out->scratch_ccw = true; else out->scratch_cw = true;
            continue;
        }
        out->pressed[note->lane] = out->held[note->lane] = true;
        if (note->type == NOTE_DOUBLE) out->pressed[note->lane2] = out->held[note->lane2] = true;
    }
}

void rhythm_sync_time(RhythmState *rh, float target_ms) {
    float err = target_ms - rh->song_time_ms;
    if (fabsf(err) > 120.0f) rh->song_time_ms = target_ms;
    else rh->song_time_ms += err * 0.1f;
}

void rhythm_update(RhythmState *rh, const Input *in, float dt) {
    if (in->pressed[BTN_SELECT]) {
        rhythm_restart(rh);
        return;
    }

    rh->song_time_ms += dt * 1000.0f;

    for (int i = 0; i < BTN_COUNT; i++) {
        if (rh->lane_flash_timer[i] > 0.0f) {
            rh->lane_flash_timer[i] -= dt * 1000.0f;
            if (rh->lane_flash_timer[i] < 0.0f) rh->lane_flash_timer[i] = 0.0f;
        }
        if (rh->lane_press_timer[i] > 0.0f) {
            rh->lane_press_timer[i] -= dt * 1000.0f;
            if (rh->lane_press_timer[i] < 0.0f) rh->lane_press_timer[i] = 0.0f;
        }
    }

    /* Accusé de réception visuel sur l'icône dès qu'on appuie, qu'il y ait
       ou non une note à juger à ce moment — pour confirmer que l'appui est
       bien reçu. */
    for (int i = 0; i < LANE_COUNT; i++) {
        Button b = LANES[i].button;
        if (in->pressed[b]) rh->lane_press_timer[b] = PRESS_FLASH_DURATION_MS;
        rh->lane_held[b] = in->held[b]; /* effet d'empreinte tant que le bouton est physiquement enfoncé */
    }

    if (rh->popup_timer > 0.0f) rh->popup_timer -= dt * 1000.0f;
    if (rh->combo_pulse_ms > 0.0f) rh->combo_pulse_ms -= dt * 1000.0f;
    if (rh->finished) rh->results_ms += dt * 1000.0f;
    ui_approach(&rh->score_display, (float)rh->score, 14.0f, dt);

    if (!rh->chart_loaded) return;

    bool all_done = true;
    float last_end = 0.0f;
    for (int i = 0; i < rh->chart.note_count; i++) {
        const Note *note = &rh->chart.notes[i];
        NoteRuntime *nr = &rh->note_runtime[i];
        float hit_time = (float)(rh->chart.offset_ms + note->time_ms);

        if (!nr->judged || nr->hold_active) all_done = false;
        if (hit_time + (float)note->duration_ms > last_end) last_end = hit_time + (float)note->duration_ms;

        /* Double note : les deux lanes doivent être frappées dans la fenêtre ;
           jugement unique = le plus mauvais des deux, points doublés. */
        if (note->type == NOTE_DOUBLE) {
            if (nr->judged) continue;
            float delta = rh->song_time_ms - hit_time;
            if (fabsf(delta) <= WINDOW_GOOD_MS) {
                Judgment jd = (fabsf(delta) <= WINDOW_PERFECT_MS) ? JUDGE_PERFECT : JUDGE_GOOD;
                if (!nr->a_hit && in->pressed[note->lane])  { nr->a_hit = true; nr->ja = jd; }
                if (!nr->b_hit && in->pressed[note->lane2]) { nr->b_hit = true; nr->jb = jd; }
            }
            if (nr->a_hit && nr->b_hit) {
                Judgment j = nr->ja > nr->jb ? nr->ja : nr->jb;
                nr->judged = true;
                nr->judgment = j;
                judge_event(rh, note->lane, note->lane2, j, 2, false);
            } else if (delta > WINDOW_GOOD_MS) {
                nr->judged = true;
                nr->judgment = JUDGE_MISS;
                judge_event(rh, note->lane, note->lane2, JUDGE_MISS, 1, false);
            }
            continue;
        }

        if (note->type == NOTE_SCRATCH) {
            if (nr->judged) continue;
            float delta = rh->song_time_ms - hit_time;
            /* Un léger toucher de l'un des deux sticks, dans n'importe quel sens, ou L / R :
               le sens de la note ne se vérifie plus (il reste visible sur l'anneau) */
            bool kicked = in->scratch_cw || in->scratch_ccw || in->pressed[BTN_L] || in->pressed[BTN_R];

            if (kicked && fabsf(delta) <= SCRATCH_WINDOW_GOOD_MS) {
                Judgment j = (fabsf(delta) <= SCRATCH_WINDOW_PERFECT_MS) ? JUDGE_PERFECT : JUDGE_GOOD;
                nr->judged = true;
                nr->judgment = j;
                judge_scratch(rh, j);
            } else if (delta > SCRATCH_WINDOW_GOOD_MS) {
                nr->judged = true;
                nr->judgment = JUDGE_MISS;
                judge_scratch(rh, JUDGE_MISS);
            }
            continue;
        }

        if (note->type == NOTE_TAP) {
            if (nr->judged) continue;
            float delta = rh->song_time_ms - hit_time; /* > 0 = en retard sur le timing */

            if (in->pressed[note->lane] && fabsf(delta) <= WINDOW_GOOD_MS) {
                Judgment j = (fabsf(delta) <= WINDOW_PERFECT_MS) ? JUDGE_PERFECT : JUDGE_GOOD;
                nr->judged = true;
                nr->judgment = j;
                judge_note(rh, note->lane, j);
            } else if (delta > WINDOW_GOOD_MS) {
                nr->judged = true;
                nr->judgment = JUDGE_MISS;
                judge_note(rh, note->lane, JUDGE_MISS);
            }
            continue;
        }

        /* NOTE_HOLD : même fenêtre de jugement au démarrage qu'un tap, puis
           il faut garder le bouton enfoncé jusqu'à hit_time + duration_ms. */
        float end_time = hit_time + (float)note->duration_ms;

        if (!nr->judged) {
            float delta = rh->song_time_ms - hit_time;
            if (in->pressed[note->lane] && fabsf(delta) <= WINDOW_GOOD_MS) {
                Judgment j = (fabsf(delta) <= WINDOW_PERFECT_MS) ? JUDGE_PERFECT : JUDGE_GOOD;
                nr->judged = true;
                nr->judgment = j;
                nr->hold_active = true;
                rh->lane_hold[note->lane] = (LaneHold){
                    .active = true, .hit_time_ms = hit_time, .duration_ms = (float)note->duration_ms
                };
                judge_note(rh, note->lane, j);
            } else if (delta > WINDOW_GOOD_MS) {
                nr->judged = true;
                nr->judgment = JUDGE_MISS;
                judge_note(rh, note->lane, JUDGE_MISS); /* jamais commencée */
            }
        } else if (nr->hold_active) {
            /* La fin de la tenue est une note à part entière, jugée comme un tap (300 / 100 points, combo +1) :
               tenue jusqu'au bout = PERFECT ; lâchée un peu avant = PERFECT (<= WINDOW_PERFECT_MS avant la fin)
               ou GOOD (<= WINDOW_GOOD_MS avant) ; lâchée plus tôt, la fin est ratée (0 point) mais le combo est
               conservé. On teste d'abord "temps écoulé" (valable qu'on tienne encore ou non) pour ne jamais
               rater le cas où le relâchement et la fin tombent pile sur la même frame. */
            float remaining = end_time - rh->song_time_ms;
            bool released = !in->held[note->lane];
            if (remaining <= 0.0f || (released && remaining <= WINDOW_GOOD_MS)) {
                nr->hold_active = false;
                nr->hold_done = true;
                rh->lane_hold[note->lane].active = false;
                judge_tail(rh, note->lane, remaining <= WINDOW_PERFECT_MS ? JUDGE_PERFECT : JUDGE_GOOD);
            } else if (released) {
                nr->hold_active = false;
                nr->hold_broken = true;
                rh->lane_hold[note->lane].active = false;
                judge_tail(rh, note->lane, JUDGE_MISS);   /* relâchée trop tôt : fin perdue, combo conservé */
            }
        }
    }

    /* Fin : END si le mapper l'a posée après la dernière note, sinon un court délai après celle-ci */
    float finish_at = rh->chart.end_ms >= 0 && (float)rh->chart.end_ms >= last_end ? (float)rh->chart.end_ms
                                                                                   : last_end + RESULTS_DELAY_MS;
    if (!rh->finished && rh->chart.note_count > 0 && all_done && rh->song_time_ms > finish_at)
        rh->finished = true;
}

/* Barre d'encre inclinée (skewX -20°), remplissage rose, repère citron ; liseré blanc translucide */
static void draw_progress_bar(const RhythmState *rh, SDL_Renderer *r) {
    const float w = 500.0f, h = 12.0f, x = (SCREEN_W - w) / 2.0f, y = SCREEN_H - 16.0f - h, tan20 = 0.364f;
    float t = rh->song_end_ms > 0.0f ? rh->song_time_ms / rh->song_end_ms : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    ui_skew_box(r, (SDL_FRect){x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f}, (h + 4.0f) * 0.5f * tan20, ui_alpha(CH_WHITE, 89));
    ui_skew_box(r, (SDL_FRect){x, y, w, h}, h * 0.5f * tan20, CH_INK);
    if (t > 0.0f) ui_skew_box(r, (SDL_FRect){x, y, w * t, h}, h * 0.5f * tan20, CH_PINK);
    ui_skew_box(r, (SDL_FRect){x + w * t - 2.0f, y - 3.0f, 4.0f, h + 6.0f}, (h + 6.0f) * 0.5f * tan20, CH_GOLD);
}

/* Taille de fond (NOTE_START_SIZE .. NOTE_FINAL_SIZE) à la progression p du trajet (0 = départ, 1 = cible) */
static float note_trend_size(float p) {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return NOTE_START_SIZE + (NOTE_FINAL_SIZE - NOTE_START_SIZE) * powf(p, NOTE_SIZE_CURVE);
}

/* Taille relative d'une note à la progression p (0 = départ, 1 = cible) à l'instant courant : la taille de fond
   qu'elle avait au dernier temps de la musique (marches, voir NOTE_BEAT_*), avec le dépassement de ce temps, puis
   l'apparition « pop » du début du trajet. Ne dépend que de p et de l'horloge : têtes, notes de fin des tenues et
   notes tenues sur la cible pulsent ensemble. */
static float note_size(const RhythmState *rh, float p) {
    const float beat = 60000.0f / rh->chart.bpm, rise = beat * NOTE_BEAT_RISE_FRAC;
    float since = rh->song_time_ms - (float)rh->chart.offset_ms;    /* ms depuis le temps 0 de la carte */
    float k = floorf((since + rise) / beat);                        /* temps en cours (ou dont le saut a commencé) */
    float d = since - k * beat;                                     /* ms depuis ce temps ; < 0 pendant le saut */
    float p_beat = p - d / rh->approach_ms;                         /* progression de la note à ce temps */
    float size;
    if (d >= 0.0f) {
        size = note_trend_size(p_beat) * (1.0f + NOTE_BEAT_POP * expf(-NOTE_BEAT_DECAY * d / beat));
    } else {   /* montée : de la marche précédente (posée) au sommet de celle-ci */
        float from = note_trend_size(p_beat - beat / rh->approach_ms) * (1.0f + NOTE_BEAT_POP * expf(-NOTE_BEAT_DECAY));
        float to = note_trend_size(p_beat) * (1.0f + NOTE_BEAT_POP);
        float u = 1.0f + d / rise;
        size = from + (to - from) * u * u * (3.0f - 2.0f * u);
    }
    if (p < NOTE_POP_P) size *= NOTE_POP_SCALE + (1.0f - NOTE_POP_SCALE) * ((p < 0.0f ? 0.0f : p) / NOTE_POP_P);
    return size;
}

/* Capsule d'une tenue : relie la note de fin (x1, y1, taille s1) à la tête (x2, y2, taille s2). Ses côtés sont le
   prolongement exact de la bordure des deux notes (même rayon, même épaisseur, qui suit la taille de chaque note) :
   les notes, dessinées par-dessus, ferment la capsule sans anneau en plus. Remplie de `fill` ; pendant le maintien
   (sparkle_alpha > 0), des paillettes la remontent vers la tête. */
static void draw_hold_capsule(SDL_Renderer *r, float x1, float y1, float s1, float x2, float y2, float s2,
                              SDL_Color fill, SDL_Color line, float sparkle_alpha, float time_ms) {
    const float mid_r = NOTE_DISC_RADIUS + NOTE_BORDER_WIDTH * 0.5f;   /* milieu de la bande de bordure */
    float r1 = mid_r * s1, r2 = mid_r * s2, w1 = NOTE_BORDER_WIDTH * s1 * 0.5f, w2 = NOTE_BORDER_WIDTH * s2 * 0.5f;
    float dx = x2 - x1, dy = y2 - y1, d = sqrtf(dx * dx + dy * dy);
    if (d <= fabsf(r1 - r2) + 0.5f) return;   /* une note dans l'autre : rien qui dépasse */
    float ux = dx / d, uy = dy / d, nx = -uy, ny = ux;
    float s = (r1 - r2) / d, c = sqrtf(1.0f - s * s);
    /* normales des deux tangentes extérieures (m·u = (r1 - r2) / d) */
    const float mx[2] = { nx * c + ux * s, -nx * c + ux * s }, my[2] = { ny * c + uy * s, -ny * c + uy * s };

    if (fill.a) {   /* entre les deux tangentes (les bouts sont sous les notes) */
        SDL_FPoint q[4] = { { x1 + mx[0] * r1, y1 + my[0] * r1 }, { x2 + mx[0] * r2, y2 + my[0] * r2 },
                            { x2 + mx[1] * r2, y2 + my[1] * r2 }, { x1 + mx[1] * r1, y1 + my[1] * r1 } };
        shapes_filled_polygon(r, q, 4, fill);
    }

    /* paillettes : étoiles à quatre branches qui glissent de la fin vers la tête en scintillant */
    if (sparkle_alpha > 0.0f) {
        for (int i = 0; i < HOLD_SPARKLES; i++) {
            float seed = (float)i * 0.618034f;
            float t = seed + time_ms * HOLD_SPARKLE_SPEED;
            t -= floorf(t);
            float side = (seed * 7.3f - floorf(seed * 7.3f)) * 2.0f - 1.0f;   /* -1..1 à travers la capsule */
            float rad = r1 + (r2 - r1) * t;
            float px = x1 + dx * t + nx * side * rad * 0.6f, py = y1 + dy * t + ny * side * rad * 0.6f;
            float tw = 0.5f + 0.5f * sinf(time_ms * 0.012f + (float)i * 2.1f);
            float sz = (2.5f + 3.5f * tw) * rad / 30.0f;
            float edge = t < 0.15f ? t / 0.15f : t > 0.85f ? (1.0f - t) / 0.15f : 1.0f;   /* naît et meurt en fondu */
            SDL_Color sc = { 255, 255, 255, (Uint8)(235.0f * sparkle_alpha * edge * (0.4f + 0.6f * tw)) };
            const SDL_FPoint v[4] = { {px, py - sz}, {px + sz * 0.28f, py}, {px, py + sz}, {px - sz * 0.28f, py} };
            const SDL_FPoint h[4] = { {px - sz, py}, {px, py - sz * 0.28f}, {px + sz, py}, {px, py + sz * 0.28f} };
            shapes_filled_polygon(r, v, 4, sc);
            shapes_filled_polygon(r, h, 4, sc);
        }
    }

    /* côtés : bande de la bordure, de son bord intérieur à son bord extérieur, plus large côté tête */
    for (int k = 0; k < 2; k++) {
        SDL_FPoint q[4] = { { x1 + mx[k] * (r1 - w1), y1 + my[k] * (r1 - w1) }, { x2 + mx[k] * (r2 - w2), y2 + my[k] * (r2 - w2) },
                            { x2 + mx[k] * (r2 + w2), y2 + my[k] * (r2 + w2) }, { x1 + mx[k] * (r1 + w1), y1 + my[k] * (r1 + w1) } };
        shapes_filled_polygon(r, q, 4, line);
    }
}

/* Tête de note (boutons comme flèches) : symbole posé sur un disque quasi opaque avec contour de la couleur
   du symbole. La traîne des tenues et le trait des doubles, dessinés avant, passent derrière. */
/* Prochaine note de chaque lane : la première, dans l'ordre de la carte, pas encore jugée (ou tenue en
   cours). Une double compte pour ses deux lanes. Calculé au dessin, sans modifier l'état. */
static void find_next_notes(const RhythmState *rh, int next[BTN_COUNT]) {
    for (int b = 0; b < BTN_COUNT; b++) next[b] = -1;
    for (int i = 0; i < rh->chart.note_count; i++) {
        const Note *note = &rh->chart.notes[i];
        const NoteRuntime *nr = &rh->note_runtime[i];
        if (note->type == NOTE_SCRATCH || (nr->judged && !nr->hold_active)) continue;
        if (note->lane < BTN_COUNT && next[note->lane] < 0) next[note->lane] = i;
        if (note->type == NOTE_DOUBLE && note->lane2 < BTN_COUNT && next[note->lane2] < 0) next[note->lane2] = i;
    }
}

static bool is_next_note(const RhythmState *rh, const int next[BTN_COUNT], int i) {
    const Note *note = &rh->chart.notes[i];
    if (note->type == NOTE_SCRATCH) return true;
    if (note->lane < BTN_COUNT && next[note->lane] == i) return true;
    return note->type == NOTE_DOUBLE && note->lane2 < BTN_COUNT && next[note->lane2] == i;
}

/* Contour des notes : couleur de la lane rendue plus vive (écartée du gris), pour qu'elles ressortent du fond */
static SDL_Color note_border_color(Button lane) {
    SDL_Color c = lane_color(lane);
    float gray = (c.r + c.g + c.b) / 3.0f, v[3] = { c.r, c.g, c.b };
    for (int k = 0; k < 3; k++) {
        v[k] = gray + (v[k] - gray) * NOTE_BORDER_VIVID;
        v[k] = v[k] < 0.0f ? 0.0f : v[k] > 255.0f ? 255.0f : v[k];
    }
    return (SDL_Color){ (Uint8)v[0], (Uint8)v[1], (Uint8)v[2], 255 };
}

static void draw_note_head_ex(SDL_Renderer *r, const Sprites *sprites, Button lane, float x, float y, SDL_Color ring,
                              float alpha, float size, bool white_symbol) {
    ring.a = (Uint8)((float)ring.a * alpha);
    /* halo de la couleur du contour, puis disque sombre (contraste du symbole) et contour */
    const float halo = NOTE_HALO_SCALE * (NOTE_DISC_RADIUS + NOTE_BORDER_WIDTH) / NOTE_DISC_RADIUS;   /* note entière */
    sprite_draw_note_disc(sprites, r, x, y, size * halo, ui_alpha(ring, (Uint8)(NOTE_HALO_ALPHA * alpha)), UI_NONE);
    sprite_draw_note_disc(sprites, r, x, y, size, (SDL_Color){12, 12, 20, (Uint8)(225.0f * alpha)}, ring);
    bool is_button = (lane == BTN_TRIANGLE || lane == BTN_CIRCLE || lane == BTN_CROSS);
    float scale = is_button ? NOTE_BUTTON_SPRITE_SCALE : NOTE_ARROW_SPRITE_SCALE;
    if (white_symbol) sprite_draw_note_white(sprites, r, lane, x, y, scale * size, (Uint8)(255.0f * alpha));
    else              sprite_draw_note(sprites, r, lane, x, y, scale * size, (Uint8)(255.0f * alpha));
}

static void draw_note_head(SDL_Renderer *r, const Sprites *sprites, Button lane, float x, float y, SDL_Color ring,
                           float alpha, float size) {
    draw_note_head_ex(r, sprites, lane, x, y, ring, alpha, size, false);
}

/* HUD « Cascade pop » (maquette validée le 2026-10-14) : score citron cerclé d'encre en haut, zéros de tête en
   contour ; jugement incliné en relief au-dessus du centre (PERFECT à double ombre et étincelles) ; combo blanc à
   ombre rose, légèrement tourné, sous le centre. */
#define HUD_SCORE_CY   55.0f
#define HUD_JUDGE_CY  236.0f
#define HUD_COMBO_CY  333.0f
#define HUD_COMBO_PIVOT_Y 348.0f

static void draw_score(const RhythmState *rh, SDL_Renderer *r, const Font *font) {
    ui_pop_text(r, font, FONT_LABEL, SCREEN_W / 2.0f, 19.0f, 11.0f / 14.0f, 3.0f, 0.0f, 0.0f,
                ui_alpha(CH_WHITE, 217), UI_NONE, UI_NONE, 0, 0, "SCORE");
    /* 7 chiffres au moins (plus au-delà de 9 999 999), groupés par trois (« 0 184 730 ») ; les zéros de tête sont en
       contour pâle */
    char d[16], grouped[24];
    int v = (int)(rh->score_display + 0.5f);
    snprintf(d, sizeof d, "%07d", v < 0 ? 0 : v);
    const int n = (int)strlen(d);
    int lead = 0;
    while (lead < n - 1 && d[lead] == '0') lead++;
    int k = 0, split = 0;                            /* split : index du premier chiffre significatif dans grouped */
    for (int i = 0; i < n; i++) {
        if (i > 0 && (n - i) % 3 == 0) grouped[k++] = ' ';
        if (i == lead) split = k;
        grouped[k++] = d[i];
    }
    grouped[k] = '\0';
    const float sc = 34.0f / 38.0f, trk = 2.0f / sc;
    const FontRot flat = { 0.0f, 0.0f, 0.0f, trk, 0.0f };
    float total = (float)font_width_tracked(font, FONT_POP, grouped, trk) * sc;
    float x = SCREEN_W / 2.0f - total * 0.5f, top = HUD_SCORE_CY - font_cap_height(font, FONT_POP) * sc * 0.5f;
    char head[16];
    snprintf(head, sizeof head, "%.*s", split, grouped);
    if (head[0]) {
        font_draw_hollow_rot(font, r, FONT_POP, x, top, sc, flat, ui_alpha(CH_WHITE, 102), head);
        x += ((float)font_width_tracked(font, FONT_POP, head, trk) + trk) * sc;
    }
    font_draw_ink_rot(font, r, FONT_POP, x, top, sc, flat, CH_GOLD, CH_INK, CH_INK, 3.0f, 3.0f, grouped + split);
}

static void draw_judgment(const RhythmState *rh, SDL_Renderer *r, const Font *font) {
    if (rh->popup_timer <= 0.0f || !rh->popup_text) return;
    float fade = rh->popup_timer / POPUP_DURATION_MS;
    float since = POPUP_DURATION_MS - rh->popup_timer;
    float pop = since < POPUP_POP_MS ? 1.0f - since / POPUP_POP_MS : 0.0f;
    Uint8 a = (Uint8)(255.0f * (fade < 0.4f ? fade / 0.4f : 1.0f));
    const float cx = SCREEN_W / 2.0f, cy = HUD_JUDGE_CY, grow = 1.0f + 0.45f * pop * pop;
    const SDL_Color ink = ui_alpha(CH_INK, a), none = UI_NONE;
    switch (rh->popup_judgment) {
    case JUDGE_PERFECT: {
        float sc = 50.0f / 66.0f * grow;
        /* relief : ombres empilées encre, cyan, rose, puis la lettre citron cernée d'encre */
        ui_pop_text(r, font, FONT_POP_XL, cx, cy, sc, 2.0f, 0.0f, -12.0f, none, none, ink, 11, 11, rh->popup_text);
        ui_pop_text(r, font, FONT_POP_XL, cx, cy, sc, 2.0f, 0.0f, -12.0f, none, none, ui_alpha(CH_CYAN, a), 8, 8, rh->popup_text);
        ui_pop_text(r, font, FONT_POP_XL, cx, cy, sc, 2.0f, 0.0f, -12.0f, ui_alpha(CH_GOLD, a), ink, ui_alpha(CH_PINK, a), 4, 4,
                    rh->popup_text);
        /* étincelles autour du mot */
        static const float SPARK[3][3] = { {-139.0f, -35.0f, 1.0f}, {149.0f, 11.0f, 0.7f}, {131.0f, -41.0f, 0.45f} };
        float k = 1.0f - pop;
        for (int i = 0; i < 3; i++)
            ui_star(r, cx + SPARK[i][0], cy + SPARK[i][1], 22.0f * SPARK[i][2] * (0.4f + 0.6f * k), since * 0.15f,
                    ui_alpha(CH_GOLD, a));
        break;
    }
    case JUDGE_GOOD:
        ui_pop_text(r, font, FONT_POP_XL, cx, cy, 44.0f / 66.0f * grow, 2.0f, 0.0f, -12.0f, ui_alpha(CH_WHITE, a), ink, ink,
                    4, 4, rh->popup_text);
        break;
    default:   /* MISS : lavande, penché, un peu effacé */
        ui_pop_text(r, font, FONT_POP_XL, cx, cy, 44.0f / 66.0f * grow, 2.0f, 4.0f, -12.0f,
                    ui_alpha(CH_ROSE, (Uint8)(a * 0.9f)), ink, ink, 3, 3, rh->popup_text);
        break;
    }
}

static void draw_combo(const RhythmState *rh, SDL_Renderer *r, const Font *font) {
    if (rh->combo < 2) return;
    char buf[16];
    snprintf(buf, sizeof buf, "%d", rh->combo);
    float pulse = rh->combo_pulse_ms > 0.0f ? rh->combo_pulse_ms / COMBO_PULSE_MS : 0.0f;
    const float cx = SCREEN_W / 2.0f, angle = -4.0f;
    /* le nombre et son étiquette tournent ensemble autour du même point */
    float sc = 74.0f / 66.0f * (1.0f + 0.12f * pulse);
    float w = (float)font_width(font, FONT_POP_XL, buf) * sc;
    const FontRot rot = { angle, cx, HUD_COMBO_PIVOT_Y, 0.0f, 0.0f };
    font_draw_ink_rot(font, r, FONT_POP_XL, cx - w * 0.5f, HUD_COMBO_CY - font_cap_height(font, FONT_POP_XL) * sc * 0.5f, sc,
                      rot, CH_WHITE, CH_INK, CH_PINK, 6.0f, 6.0f, buf);
    const float lsc = 13.0f / 14.0f, trk = 4.0f / lsc;
    const FontRot lr = { angle, cx, HUD_COMBO_PIVOT_Y, trk, 0.0f };
    float lw = (float)font_width_tracked(font, FONT_LABEL, "COMBO", trk) * lsc;
    const float th = 22.0f, ty = HUD_COMBO_CY + 41.0f;
    ui_fill_rect_rot(r, (SDL_FRect){cx - lw * 0.5f - 10.0f, ty, lw + 20.0f + 4.0f * lsc, th}, lr, CH_INK);
    font_draw_ink_rot(font, r, FONT_LABEL, cx - lw * 0.5f, ty + (th - font_cap_height(font, FONT_LABEL) * lsc) * 0.5f, lsc,
                      lr, CH_CYAN, UI_NONE, UI_NONE, 0, 0, "COMBO");
}

static void draw_hud(const RhythmState *rh, SDL_Renderer *r, const Font *font) {
    draw_score(rh, r, font);
    draw_judgment(rh, r, font);
    draw_combo(rh, r, font);
}

/* 0 -> 1 avec ralentissement final, borné */
static float ease_out(float t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

/* Dépassement puis retour (« ressort ») : 0 -> 1 en passant un peu au-delà */
static float ease_back(float t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float u = t - 1.0f;
    return 1.0f + 2.70158f * u * u * u + 1.70158f * u * u;
}

/* Écran de résultats « Cascade pop » (maquette validée le 2026-10-14) : fiche du morceau en haut à gauche, score
   qui défile, carte d'encre des compteurs, rang en tampon incliné qui tombe après le décompte, médailles en
   pastilles. La moitié droite reste libre (réservée à l'hôtesse). */
static void draw_results(const RhythmState *rh, SDL_Renderer *r, const Font *font, const Sprites *sprites,
                         const Background *bg, int best_score, bool new_best, int night) {
    char buf[96], num[24];

    /* Précision et rang : points plats (sans le combo), comme dans le jeu de référence où le rang suit la qualité des
       jugements et non la valeur du score */
    float max_flat = 0.0f;
    for (int i = 0; i < rh->chart.note_count; i++) {
        NoteType nt = rh->chart.notes[i].type;
        max_flat += (nt == NOTE_DOUBLE ? 2.0f : 1.0f) * ACC_PERFECT;
        if (nt == NOTE_HOLD) max_flat += ACC_PERFECT;   /* la fin de la tenue vaut une note */
    }
    float acc = max_flat > 0.0f ? (float)rh->flat_points / max_flat : 0.0f;
    /* Médailles : ALL PERFECT = aucun GOOD ni MISS (scratch et fins de tenue comprises) ; FULL COMBO = combo jamais cassé */
    bool all_perfect = rh->judgment_count[JUDGE_GOOD] == 0 && rh->judgment_count[JUDGE_MISS] == 0;
    bool full_combo = rh->combo_breaks == 0;
    const char *rank = acc >= 0.95f ? "S" : acc >= 0.85f ? "A" : acc >= 0.70f ? "B" : acc >= 0.50f ? "C" : "D";
    SDL_Color rank_color = acc >= 0.95f ? CH_GOLD : acc >= 0.85f ? CH_CYAN : acc >= 0.70f ? CH_PINK
                         : acc >= 0.50f ? CH_ROSE : CH_WHITE;

    const float t = rh->results_ms;
    float fade = ease_out(t / RESULTS_FADE_MS);
    Uint8 a = (Uint8)(255.0f * fade);
    float count = ease_out((t - RESULTS_COUNT_START_MS) / RESULTS_COUNT_MS);   /* décompte des valeurs */

    /* fond : l'image ou la vidéo du morceau revient par-dessus le jeu, puis un voile d'encre à gauche */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, a);
    SDL_RenderFillRect(r, NULL);
    background_draw_faded(bg, r, fade);
    static const float VX[4] = { 0.0f, 0.42f, 0.62f, 1.0f };
    const float va[4] = { 0.94f * fade, 0.94f * fade, 0.6f * fade, 0.15f * fade };
    ui_veil_gradient(r, VX, va, 4);

    /* bandeau, titre, artiste et difficulté */
    char night_txt[16];
    snprintf(night_txt, sizeof night_txt, "NIGHT %02d", night);
    const char *tags[2] = { night_txt, "RESULTS" };
    const SDL_Color tag_bg[2] = { CH_PINK, CH_INK };
    if (night > 0) ui_tags(r, font, 30.0f, 20.0f, tags, tag_bg, 2, a);
    else           ui_tags(r, font, 30.0f, 20.0f, tags + 1, tag_bg + 1, 1, a);

    char up[96];
    ui_upper(rh->chart.title[0] ? rh->chart.title : "", up, sizeof up);
    float tsc = 52.0f / 66.0f, tw = (float)font_width(font, FONT_POP_XL, up) * tsc;
    if (tw > 440.0f) { tsc *= 440.0f / tw; tw = 440.0f; }
    const FontRot trot = { -3.0f, 30.0f, 78.0f, 0.0f, 0.0f };
    font_draw_ink_rot(font, r, FONT_POP_XL, 30.0f, 78.0f - font_cap_height(font, FONT_POP_XL) * tsc * 0.5f, tsc, trot,
                      ui_alpha(CH_GOLD, a), ui_alpha(CH_INK, a), ui_alpha(CH_PINK, a), 5.0f, 5.0f, up);

    float mx = 34.0f;
    const float my = 136.0f;
    if (rh->chart.artist[0]) {
        const float asc = 15.0f / 16.0f;
        ui_fit_text(font, FONT_CARD, rh->chart.artist, 300, buf, sizeof buf);
        font_draw_ex(font, r, FONT_CARD, mx, my - font_cap_height(font, FONT_CARD) * 0.5f, ALIGN_LEFT, asc,
                     ui_alpha(CH_WHITE, (Uint8)(217 * fade)), UI_NONE, buf);
        mx += (float)font_width(font, FONT_CARD, buf) * asc + 10.0f;
    }
    if (rh->chart.difficulty[0]) {
        ui_upper(rh->chart.difficulty, up, sizeof up);
        float pw = (float)font_width(font, FONT_CARD_BOLD, up) + 22.0f;
        ui_panel(r, (SDL_FRect){mx, my - 11.0f, pw, 22.0f}, 11.0f, ui_alpha(CH_CYAN, a), UI_NONE, 0.0f);
        font_draw(font, r, FONT_CARD_BOLD, mx + pw * 0.5f, my - font_cap_height(font, FONT_CARD_BOLD) * 0.5f, ALIGN_CENTER,
                  ui_alpha(CH_INK, a), up);
    }

    /* score qui défile, record précédent dessous */
    const FontRot lab = { 0.0f, 0.0f, 0.0f, 4.0f / (12.0f / 14.0f), 0.0f };
    font_draw_ink_rot(font, r, FONT_LABEL, 30.0f, 170.0f - font_cap_height(font, FONT_LABEL) * (12.0f / 14.0f) * 0.5f,
                      12.0f / 14.0f, lab, ui_alpha(CH_WHITE, (Uint8)(217 * fade)), UI_NONE, UI_NONE, 0, 0, "SCORE");
    ui_format_score((int)(rh->score * count + 0.5f), num, sizeof num);
    const float ssc = 70.0f / 66.0f;
    const FontRot srot = { 0.0f, 0.0f, 0.0f, 1.0f / ssc, 0.0f };
    font_draw_ink_rot(font, r, FONT_POP_XL, 30.0f, 214.0f - font_cap_height(font, FONT_POP_XL) * ssc * 0.5f, ssc, srot,
                      ui_alpha(CH_WHITE, a), ui_alpha(CH_INK, a), ui_alpha(CH_PINK, a), 6.0f, 6.0f, num);
    if (best_score > 0 && !new_best) {
        const float bsc = 14.0f / 16.0f;
        font_draw_ex(font, r, FONT_CARD, 30.0f, 262.0f - font_cap_height(font, FONT_CARD) * 0.5f, ALIGN_LEFT, bsc,
                     ui_alpha(CH_WHITE, (Uint8)(191 * fade)), UI_NONE, "BEST");
        ui_format_score(best_score, num, sizeof num);
        font_draw_ex(font, r, FONT_CARD_BOLD, 30.0f + (float)font_width(font, FONT_CARD, "BEST ") * bsc + 4.0f,
                     262.0f - font_cap_height(font, FONT_CARD_BOLD) * 0.5f, ALIGN_LEFT, 1.0f, ui_alpha(CH_WHITE, a),
                     UI_NONE, num);
    }

    /* carte d'encre des compteurs : entre par la gauche */
    {
        float ct = ease_out((t - 100.0f) / 450.0f), off = (1.0f - ct) * -80.0f;
        Uint8 ca = (Uint8)(255.0f * ct);
        const float cw = 360.0f, ch = 142.0f, cx = 24.0f + off, cy = SCREEN_H - 54.0f - ch, lean = ch * 0.5f * 0.1405f;
        ui_skew_box(r, (SDL_FRect){cx + 8.0f, cy + 8.0f, cw, ch}, lean, ui_alpha(CH_PINK, ca));
        ui_skew_box(r, (SDL_FRect){cx, cy, cw, ch}, lean, ui_alpha(CH_INK, ca));
        const struct { const char *label; SDL_Color color; int value; } rows[] = {
            { "PERFECT",   CH_GOLD,  rh->judgment_count[JUDGE_PERFECT] },
            { "GOOD",      CH_WHITE, rh->judgment_count[JUDGE_GOOD] },
            { "MISS",      CH_ROSE,  rh->judgment_count[JUDGE_MISS] },
            { "MAX COMBO", CH_CYAN,  rh->max_combo },
        };
        const float rsc = 17.0f / 28.0f, mid = cy + ch * 0.5f;
        for (int i = 0; i < 4; i++) {
            float y = cy + 25.0f + i * 27.0f + (i == 3 ? 10.0f : 0.0f);
            float lx = cx + 20.0f - 0.1405f * (y - mid), rx = cx + cw - 20.0f - 0.1405f * (y - mid);
            if (i == 3) {
                SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8)(46 * ct));
                SDL_RenderDrawLine(r, (int)(lx + 1.0f), (int)(y - 17.0f), (int)(rx + 1.0f), (int)(y - 17.0f));
            }
            float top = y - font_cap_height(font, FONT_CARD_NAME) * rsc * 0.5f;
            const FontRot rr = { 0.0f, 0.0f, 0.0f, 1.0f / rsc, 0.0f };
            font_draw_ink_rot(font, r, FONT_CARD_NAME, lx, top, rsc, rr, ui_alpha(rows[i].color, ca), UI_NONE, UI_NONE, 0, 0,
                              rows[i].label);
            snprintf(num, sizeof num, "%d", (int)(rows[i].value * count + 0.5f));
            font_draw_ex(font, r, FONT_CARD_NAME, rx, top, ALIGN_RIGHT, rsc, ui_alpha(CH_WHITE, ca), UI_NONE, num);
        }
    }

    /* rang : tampon incliné qui tombe (grand et tourné -> en place, avec un rebond) */
    if (t >= RESULTS_RANK_MS) {
        float p = (t - RESULTS_RANK_MS) / 420.0f, e = ease_back(p);
        float s = 2.4f + (1.0f - 2.4f) * e, ang = -30.0f + 20.0f * e;
        Uint8 ra = (Uint8)(255.0f * (p < 0.6f ? p / 0.6f : 1.0f));
        const float cx = 585.0f, cy = 315.0f, half = 115.0f * s;
        const FontRot rot = { ang, cx, cy, 0.0f, 0.0f };
        ui_round_rect_rot(r, (SDL_FRect){cx - half + 4.0f * s, cy - half + 4.0f * s, 2.0f * half + 12.0f * s, 2.0f * half + 12.0f * s},
                          34.0f * s, rot, ui_alpha(CH_INK, ra));
        ui_round_rect_rot(r, (SDL_FRect){cx - half - 6.0f * s, cy - half - 6.0f * s, 2.0f * half + 12.0f * s, 2.0f * half + 12.0f * s},
                          34.0f * s, rot, ui_alpha(rank_color, ra));
        ui_round_rect_rot(r, (SDL_FRect){cx - half, cy - half, 2.0f * half, 2.0f * half}, 28.0f * s, rot, ui_alpha(CH_INK, ra));
        const float lsc = 13.0f / 14.0f * s, ltrk = 5.0f / (13.0f / 14.0f);
        const FontRot lr = { ang, cx, cy, ltrk, 0.0f };
        float lw = (float)font_width_tracked(font, FONT_LABEL, "RANK", ltrk) * lsc;
        font_draw_ink_rot(font, r, FONT_LABEL, cx - lw * 0.5f, cy - 91.0f * s - font_cap_height(font, FONT_LABEL) * lsc * 0.5f,
                          lsc, lr, ui_alpha(CH_WHITE, (Uint8)(204 * ra / 255)), UI_NONE, UI_NONE, 0, 0, "RANK");
        float hsc = s, hw = (float)font_width(font, FONT_POP_HUGE, rank) * hsc;
        float htop = cy - 3.0f * s - font_cap_height(font, FONT_POP_HUGE) * hsc * 0.5f;
        font_draw_ink_rot(font, r, FONT_POP_HUGE, cx - hw * 0.5f, htop, hsc, rot, UI_NONE, UI_NONE, ui_alpha(CH_CYAN, ra),
                          11.0f * s, 11.0f * s, rank);
        font_draw_ink_rot(font, r, FONT_POP_HUGE, cx - hw * 0.5f, htop, hsc, rot, ui_alpha(rank_color, ra), ui_alpha(CH_INK, ra),
                          ui_alpha(CH_PINK, ra), 6.0f * s, 6.0f * s, rank);
        snprintf(buf, sizeof buf, "%.1f%%", acc * 100.0f);
        const float asc = 15.0f / 28.0f * s;
        float aw = (float)font_width(font, FONT_CARD_NAME, buf) * asc;
        font_draw_ink_rot(font, r, FONT_CARD_NAME, cx - aw * 0.5f, cy + 91.0f * s - font_cap_height(font, FONT_CARD_NAME) * asc * 0.5f,
                          asc, rot, ui_alpha(CH_WHITE, ra), UI_NONE, UI_NONE, 0, 0, buf);

        /* étincelles autour du tampon */
        static const float SP[3][3] = { {720.0f, 205.0f, 24.0f}, {452.0f, 418.0f, 16.0f}, {706.0f, 436.0f, 12.0f} };
        float sp = ease_back((t - RESULTS_RANK_MS - 150.0f) / 500.0f);
        if (t > RESULTS_RANK_MS + 150.0f)
            for (int i = 0; i < 3; i++)
                ui_star(r, SP[i][0], SP[i][1], SP[i][2] * sp, -90.0f * (1.0f - sp), CH_GOLD);
    }

    /* médailles en pastilles, puis l'autocollant NEW RECORD! */
    const struct { bool on; const char *label; SDL_Color bg; float angle; float delay; } medals[2] = {
        { full_combo, "FULL COMBO", CH_CYAN, -6.0f, 400.0f },
        { all_perfect, "ALL PERFECT", CH_GOLD, 4.0f, 550.0f },
    };
    float medal_x = 488.0f;
    for (int i = 0; i < 2; i++) {
        if (!medals[i].on) continue;
        float pp = ease_back((t - RESULTS_RANK_MS - medals[i].delay) / 350.0f);
        const float msc = 14.0f / 28.0f, mtrk = 1.0f / msc;
        float w = (float)font_width_tracked(font, FONT_CARD_NAME, medals[i].label, mtrk) * msc + 26.0f, h = 30.0f;
        if (pp > 0.0f) {
            float cx = medal_x + w * 0.5f, cy = 452.0f + h * 0.5f;
            const FontRot mr = { medals[i].angle, cx, cy, mtrk, 0.0f };
            ui_round_rect_rot(r, (SDL_FRect){cx - w * 0.5f * pp + 3.0f, cy - h * 0.5f * pp + 3.0f, w * pp, h * pp}, 15.0f * pp, mr, CH_INK);
            ui_round_rect_rot(r, (SDL_FRect){cx - w * 0.5f * pp, cy - h * 0.5f * pp, w * pp, h * pp}, 15.0f * pp, mr, medals[i].bg);
            float tw2 = (float)font_width_tracked(font, FONT_CARD_NAME, medals[i].label, mtrk) * msc * pp;
            font_draw_ink_rot(font, r, FONT_CARD_NAME, cx - tw2 * 0.5f, cy - font_cap_height(font, FONT_CARD_NAME) * msc * pp * 0.5f,
                              msc * pp, mr, CH_INK, UI_NONE, UI_NONE, 0, 0, medals[i].label);
        }
        medal_x += w + 10.0f;
    }
    if (new_best) {
        float pp = ease_back((t - RESULTS_RANK_MS - 700.0f) / 350.0f);
        if (pp > 0.0f) {
            const float nsc = 22.0f / 38.0f * pp, ntrk = 1.0f / (22.0f / 38.0f);
            float w = ((float)font_width_tracked(font, FONT_POP, "NEW RECORD!", ntrk) * nsc) + 24.0f * pp, h = 36.0f * pp;
            const float cx = 330.0f + 76.0f, cy = 160.0f + 18.0f;
            const FontRot nr = { 8.0f, cx, cy, ntrk, 0.0f };
            ui_fill_rect_rot(r, (SDL_FRect){cx - w * 0.5f + 4.0f, cy - h * 0.5f + 4.0f, w, h}, nr, CH_INK);
            ui_fill_rect_rot(r, (SDL_FRect){cx - w * 0.5f, cy - h * 0.5f, w, h}, nr, CH_PINK);
            font_draw_ink_rot(font, r, FONT_POP, cx - w * 0.5f + 12.0f * pp, cy - font_cap_height(font, FONT_POP) * nsc * 0.5f, nsc,
                              nr, CH_WHITE, UI_NONE, UI_NONE, 0, 0, "NEW RECORD!");
        }
    }

    const float hy = SCREEN_H - 26.0f;
    float x = ui_button_hint_pop(r, font, sprites, 30.0f, hy, (const Button[]){BTN_SELECT}, 1, "RETRY") + 18.0f;
    ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CIRCLE}, 1, "MENU");
}

/* Bande scratch centrée sur l'ellipse (rx, ry) : bande bleue + liserés clairs + motif « SCRATCH >> R OR L >> » blanc qui
   tourne dans le sens à donner au stick. s = taille relative de l'anneau (épaisseur et texte suivent). */
static void draw_scratch_ring(SDL_Renderer *r, const Font *font, float rx, float ry, float s, float time_ms,
                              bool ccw, Uint8 alpha) {
    const float dir = ccw ? -1.0f : 1.0f;    /* angles comptés dans le sens horaire */
    float t = SCRATCH_RING_WIDTH * s;
    if (t < 5.0f) t = 5.0f;
    SDL_Color fill = ui_alpha(SCRATCH_FILL, (Uint8)(alpha * 230 / 255)), edge = ui_alpha(SCRATCH_EDGE, alpha);
    SDL_Color white = {255, 255, 255, alpha};
    shapes_arc_outline(r, CENTER_X, CENTER_Y, rx + t / 2, ry + t / 2, t, 0.0f, 360.0f, fill);
    shapes_arc_outline(r, CENTER_X, CENTER_Y, rx + t / 2, ry + t / 2, 1.5f, 0.0f, 360.0f, edge);
    shapes_arc_outline(r, CENTER_X, CENTER_Y, rx - t / 2 + 1.5f, ry - t / 2 + 1.5f, 1.5f, 0.0f, 360.0f, edge);

    /* motif : SCRATCH_GROUPS fois « SCRATCH » ou « R OR L » (en alternance) puis deux chevrons, décalés dans le temps (rotation) */
    const float group = 360.0f / SCRATCH_GROUPS, spin = time_ms * SCRATCH_SPIN_SPEED * dir;
    const FontFace face = FONT_BODY_BOLD;
    const float text_scale = t * 0.6f / font_cap_height(font, face);   /* capitales = 60 % de la bande */
    const float h = t * 0.62f;                                          /* hauteur d'un chevron */
    const float deg = 180.0f / PI_F;
    bool show_text = s > 0.45f;                                 /* trop petit au départ : chevrons seuls */
    const float name_deg = show_text ? font_angle_on_ellipse(font, face, rx, ry, text_scale, SCRATCH_NAME) : 0.0f;
    const float label_deg = show_text ? font_angle_on_ellipse(font, face, rx, ry, text_scale, SCRATCH_LABEL) : 0.0f;
    const float chev_step = h * 0.75f / ((rx + ry) * 0.5f) * deg;       /* écart entre les deux chevrons « >> » */
    for (int g = 0; g < SCRATCH_GROUPS; g++) {
        float a0 = g * group + spin;
        const bool name = g % 2 == 0;   /* « SCRATCH » puis « R OR L », un groupe sur deux */
        const float text_deg = name ? name_deg : label_deg;
        if (show_text)
            font_draw_on_ellipse(font, r, face, CENTER_X, CENTER_Y, rx, ry, a0, text_scale, white, name ? SCRATCH_NAME : SCRATCH_LABEL);
        float mid = a0 + text_deg + (group - text_deg) * 0.5f;   /* « >> » centrés dans la place restante */
        for (int c = 0; c < 2; c++) {
            float a = (mid + (c - 0.5f) * chev_step) / deg;
            float sn = sinf(a), cs = cosf(a);
            float px = CENTER_X + sn * rx, py = CENTER_Y - cs * ry;
            float tx = cs * dir, ty = sn * dir;                  /* tangente dans le sens de rotation */
            float nx = -sn, ny = cs;                             /* normale vers l'intérieur */
            float tipx = px + tx * h * 0.35f, tipy = py + ty * h * 0.35f;
            float bx = px - tx * h * 0.35f, by = py - ty * h * 0.35f;
            float lw = t * 0.13f < 1.5f ? 1.5f : t * 0.13f;
            shapes_thick_line(r, bx + nx * h * 0.5f, by + ny * h * 0.5f, tipx, tipy, lw, white);
            shapes_thick_line(r, bx - nx * h * 0.5f, by - ny * h * 0.5f, tipx, tipy, lw, white);
        }
    }
}

/* Hachage d'une graine -> [0, 1[ : les éclats d'un appui sont toujours les mêmes */
static float hit_rand(unsigned seed, int k) {
    unsigned h = seed * 2654435761u + (unsigned)k * 40503u + 0x9E3779B9u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
}

/* Étoile à quatre branches ou losange, ajoutés au lot de géométrie (bords lissés) */
static void hit_star(SDL_Vertex *v, int *nv, int vmax, int *idx, int *ni, int imax, float cx, float cy, float size,
                     float deg, SDL_Color c) {
    static const float P[8][2] = { {.5f, 0}, {.6f, .4f}, {1, .5f}, {.6f, .6f}, {.5f, 1}, {.4f, .6f}, {0, .5f}, {.4f, .4f} };
    if (size <= 0.5f || c.a == 0) return;
    const float rad = deg * (PI_F / 180.0f), cs = cosf(rad), sn = sinf(rad);
    SDL_FPoint p[8];
    for (int i = 0; i < 8; i++) {
        float x = (P[i][0] - 0.5f) * size, y = (P[i][1] - 0.5f) * size;
        p[i] = (SDL_FPoint){ cx + x * cs - y * sn, cy + x * sn + y * cs };
    }
    ui_poly_append(v, nv, vmax, idx, ni, imax, p, 8, c);
}

static void hit_diamond(SDL_Vertex *v, int *nv, int vmax, int *idx, int *ni, int imax, float cx, float cy, float side,
                        float deg, SDL_Color c) {
    if (side <= 0.5f || c.a == 0) return;
    const float rad = (deg + 45.0f) * (PI_F / 180.0f), cs = cosf(rad), sn = sinf(rad), h = side * 0.5f;
    const float q[4][2] = { {-h, -h}, {h, -h}, {h, h}, {-h, h} };
    SDL_FPoint p[4];
    for (int i = 0; i < 4; i++) p[i] = (SDL_FPoint){ cx + q[i][0] * cs - q[i][1] * sn, cy + q[i][0] * sn + q[i][1] * cs };
    ui_poly_append(v, nv, vmax, idx, ni, imax, p, 4, c);
}

/* Petites étoiles scintillantes autour des cibles réussies : un seul lot de géométrie (au plus une réussite par lane) */
static void draw_hit_effects(const RhythmState *rh, SDL_Renderer *r) {
    SDL_Vertex v[800];
    int idx[3200], nv = 0, ni = 0;
    const int vmax = (int)(sizeof v / sizeof v[0]), imax = (int)(sizeof idx / sizeof idx[0]);
    for (int i = 0; i < LANE_COUNT; i++) {
        const Button b = LANES[i].button;
        if (rh->lane_flash_timer[b] <= 0.0f) continue;
        const Judgment j = rh->lane_flash_judgment[b];
        if (j == JUDGE_MISS) continue;
        if (nv > 380 || ni > 1600) {   /* une réussite complète tient dans ~380 sommets : on vide le lot avant la suivante */
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(r, NULL, v, nv, idx, ni);
            nv = ni = 0;
        }
        const bool perfect = j == JUDGE_PERFECT;
        const float t = 1.0f - rh->lane_flash_timer[b] / FLASH_DURATION_MS;   /* 0 -> 1 */
        const unsigned seed = rh->lane_flash_seed[b];
        float x, y;
        lane_pos(LANES[i].side, LANES[i].angle_deg, ARC_RX, ARC_RY, &x, &y);
        const SDL_Color side = LANES[i].side == ARC_RIGHT ? CH_PINK : CH_CYAN;
        const int stars = perfect ? HIT_STARS_PERFECT : HIT_STARS_GOOD, shards = perfect ? HIT_SHARDS_PERFECT : HIT_SHARDS_GOOD;
        const int count = stars + shards;
        const float size0 = perfect ? HIT_STAR_SIZE_PERFECT : HIT_STAR_SIZE_GOOD;
        for (int k = 0; k < count; k++) {
            const bool is_star = k < stars;
            /* autour de la cible, mais sans régularité : chaque élément tire son angle, son rayon, son départ, sa taille et
               son sens de rotation indépendamment des autres (tout vient de la graine de l'appui) ; avec peu
               d'éléments, le hasard fait des grappes et des vides, jamais une couronne */
            const float ang0 = hit_rand(seed, k * 5) * 6.2831853f;
            const float start = (1.0f - HIT_STAR_LIFE) * hit_rand(seed, k * 5 + 1);
            const float u = (t - start) / HIT_STAR_LIFE;
            if (u <= 0.0f || u >= 1.0f) continue;
            /* scintille : grandit vite jusqu'à 1,25 puis redescend à 0 ; reste sur son rayon en s'en éloignant un peu de
               la cible (ease-out) et tourne sur elle-même, dans un sens tiré au hasard */
            const float tw = u < 0.35f ? u / 0.35f : 1.0f - (u - 0.35f) / 0.65f;
            const float dir = hit_rand(seed, k * 5 + 4) < 0.5f ? -1.0f : 1.0f;
            const float ang = ang0;
            const float rad = HIT_STAR_MIN_R + (HIT_STAR_MAX_R - HIT_STAR_MIN_R) * hit_rand(seed, k * 5 + 2) +
                              HIT_DRIFT_PX * (1.0f - (1.0f - u) * (1.0f - u));
            const float lead = k == 0 ? 1.3f : 0.85f;   /* une étoile plus grosse que les autres : un point d'appui visuel */
            const float sz = size0 * (is_star ? 1.0f : 0.7f) * lead * (0.7f + 0.6f * hit_rand(seed, k * 5 + 3)) * 1.25f * tw;
            SDL_Color c;
            switch (k % 3) {
            case 0:  c = perfect ? CH_GOLD : CH_WHITE; break;
            case 1:  c = side; break;
            default: c = CH_WHITE; break;
            }
            const float fade = u < HIT_FADE_FROM ? 1.0f : 1.0f - (u - HIT_FADE_FROM) / (1.0f - HIT_FADE_FROM);
            c.a = (Uint8)(255.0f * fade);
            const float sx = x + cosf(ang) * rad, sy = y + sinf(ang) * rad, spin = dir * 200.0f * u;
            if (is_star) {
                hit_star(v, &nv, vmax, idx, &ni, imax, sx, sy, sz * 1.3f, spin, ui_alpha(CH_INK, (Uint8)(c.a * 0.8f)));
                hit_star(v, &nv, vmax, idx, &ni, imax, sx, sy, sz, spin, c);
            } else {
                hit_diamond(v, &nv, vmax, idx, &ni, imax, sx, sy, sz * 1.3f, spin, ui_alpha(CH_INK, (Uint8)(c.a * 0.8f)));
                hit_diamond(v, &nv, vmax, idx, &ni, imax, sx, sy, sz, spin, c);
            }
        }
    }
    if (ni > 0) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(r, NULL, v, nv, idx, ni);
    }
}

/* --- notes liées : l'os --- */

/* Ajoute un polygone de la couleur c (frange lissée comprise) au lot de géométrie */
static void link_piece(SDL_Vertex *v, int *nv, int vmax, int *idx, int *ni, int imax, const SDL_FPoint *pts, int n,
                       SDL_Color c) {
    ui_poly_append(v, nv, vmax, idx, ni, imax, pts, n, c);
}

/* Silhouette de la barre entre deux têtes (centres p1, p2, rayon r des têtes, demi-barre w) : un rectangle et quatre
   évasements courbes qui rejoignent le contour de chaque tête (les têtes, dessinées après, les recouvrent).
   `grow` épaissit tout de la même épaisseur (passe du contour d'encre). */
static void link_bar(SDL_Vertex *v, int *nv, int vmax, int *idx, int *ni, int imax, float x1, float y1, float x2, float y2,
                     float r, float w, float grow, float f, SDL_Color c) {
    const float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    if (len < 2.0f * r) return;
    const float ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
    const float R = r + grow, W = w + grow, X[2] = { x1, x2 }, Y[2] = { y1, y2 };
    const SDL_FPoint quad[4] = { {x1 + nx * W, y1 + ny * W}, {x2 + nx * W, y2 + ny * W}, {x2 - nx * W, y2 - ny * W}, {x1 - nx * W, y1 - ny * W} };
    link_piece(v, nv, vmax, idx, ni, imax, quad, 4, c);
    if (W >= R) return;
    /* évasements : A sur le bord de la barre, B sur le contour de la tête, Bézier entre les deux (contrôle = jonction) */
    for (int e = 0; e < 2; e++) {
        const float dir = e == 0 ? 1.0f : -1.0f;               /* sens de l'axe en s'éloignant de la tête */
        const float ax = dir * ux, ay = dir * uy, along = sqrtf(R * R - W * W);
        for (int side = -1; side <= 1; side += 2) {
            const float sx = side * nx, sy = side * ny;
            const SDL_FPoint C = { X[e] + ax * along + sx * W, Y[e] + ay * along + sy * W };   /* jonction */
            const SDL_FPoint A = { C.x + ax * f, C.y + ay * f };
            const float phi = asinf(W / R) + f / R;           /* angle de B depuis l'axe, du côté `side` */
            const SDL_FPoint B = { X[e] + (ax * cosf(phi) + sx * sinf(phi)) * R, Y[e] + (ay * cosf(phi) + sy * sinf(phi)) * R };
            SDL_FPoint wing[7];
            wing[0] = (SDL_FPoint){ C.x - ax * 3.0f - sx * 1.5f, C.y - ay * 3.0f - sy * 1.5f };   /* point intérieur */
            for (int i = 0; i <= 4; i++) {
                float t = (float)i / 4.0f, m = 1.0f - t;
                wing[1 + i] = (SDL_FPoint){ m * m * A.x + 2.0f * m * t * C.x + t * t * B.x, m * m * A.y + 2.0f * m * t * C.y + t * t * B.y };
            }
            wing[6] = (SDL_FPoint){ B.x - (B.x - X[e]) * 0.08f, B.y - (B.y - Y[e]) * 0.08f };
            link_piece(v, nv, vmax, idx, ni, imax, wing, 7, c);
        }
    }
}

/* Une note liée : la barre (un aplat) en un seul lot de géométrie, puis les deux têtes, identiques
   à celles des autres notes (halo, disque sombre, contour vers l'extérieur) au coloris mélangé, symbole en blanc.
   show[k] = false : cette tête a déjà été frappée ; s'il en reste une, elle reste seule, sans barre, en attendant l'autre. */
static void draw_link(SDL_Renderer *r, const Sprites *sprites, const Button lanes[2], const float hx[2], const float hy[2],
                      const bool show[2], float size, float alpha) {
    if (!show[0] && !show[1]) return;
    if (show[0] && show[1]) {
        SDL_Vertex v[400];
        int idx[1600], nv = 0, ni = 0;
        const int vmax = (int)(sizeof v / sizeof v[0]), imax = (int)(sizeof idx / sizeof idx[0]);
        const Uint8 a = (Uint8)(255.0f * alpha);
        const float R = LINK_HEAD_R * size, w = LINK_BAR_HALF * size, f = LINK_FLARE * size;
        link_bar(v, &nv, vmax, idx, &ni, imax, hx[0], hy[0], hx[1], hy[1], R, w, 0.0f, f, ui_alpha(LINK_MIX, a));
        if (ni > 0) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(r, NULL, v, nv, idx, ni);
        }
    }
    for (int k = 0; k < 2; k++)
        if (show[k]) draw_note_head_ex(r, sprites, lanes[k], hx[k], hy[k], LINK_MIX, alpha, size, true);
}

void rhythm_draw(const RhythmState *rh, SDL_Renderer *r, const Font *font, const Sprites *sprites, const Background *bg,
                 int best_score, bool new_best, int night) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 12, 12, 20, 255);
    SDL_RenderClear(r);
    if (rh->finished && rh->results_ms >= RESULTS_FADE_MS) {   /* résultats posés : le jeu n'est plus visible dessous */
        draw_results(rh, r, font, sprites, bg, best_score, new_best, night);
        return;
    }
    background_draw_ex(bg, r, false);   /* couleurs d'origine, sans le voile noir de la carte */
    ui_veil_radial(r);

    /* Anneau d'approche (dessiné avant les arcs et les cibles : il passe derrière) : chaque note en vol est un point d'un cercle centré sur l'écran, visible en faible
       opacité. Les positions des notes étant des homothéties de celles des cibles, cet anneau est une
       ellipse passant par les cibles, réduite du même facteur ; il grossit avec la note et se confond avec les arcs à l'arrivée. */
    /* Ellipse centrée sur l'écran passant par les 6 cibles (Rond/Gauche aux extrémités, Triangle/Croix/Haut/Bas
       à 30° et 150°) : quasi un cercle (~390 x 381), elle suit presque exactement les arcs entre les cibles. */
    const float ring_rx = ARC_RIGHT_CX + ARC_RX - CENTER_X;
    const float ring_dx = ARC_RIGHT_CX + sinf(30.0f * PI_F / 180.0f) * ARC_RX - CENTER_X;
    const float ring_dy = cosf(30.0f * PI_F / 180.0f) * ARC_RY;
    const float ring_ry = ring_dy / sqrtf(1.0f - (ring_dx * ring_dx) / (ring_rx * ring_rx));
    int next_note[BTN_COUNT];
    if (rh->chart_loaded) find_next_notes(rh, next_note);
    if (rh->chart_loaded) {
        /* de la plus tardive à la plus proche : l'anneau de la prochaine note passe par-dessus */
        for (int i = rh->chart.note_count - 1; i >= 0; i--) {
            const Note *note = &rh->chart.notes[i];
            if (note->type == NOTE_SCRATCH) continue; /* anneau dédié, pas le simple écho d'approche */
            if (rh->note_runtime[i].judged) continue;
            float spawn_time = (float)(rh->chart.offset_ms + note->time_ms) - rh->approach_ms;
            float p = (rh->song_time_ms - spawn_time) / rh->approach_ms;
            if (p < NOTE_RING_START_P || p > 1.0f) continue; /* l'anneau apparaît après la note, pas dès son départ */
            float s = NOTE_SPAWN_SCALE_RING + (1.0f - NOTE_SPAWN_SCALE_RING) * p;
            /* déjà lisible à l'apparition (NOTE_RING_ALPHA_MIN), puis de plus en plus marqué jusqu'à la cible
               (NOTE_RING_ALPHA) : fondu en S (smoothstep) sur tout le trajet. */
            float ring_t = (p - NOTE_RING_START_P) / (1.0f - NOTE_RING_START_P);
            float ring_glow = ring_t * ring_t * (3.0f - 2.0f * ring_t);
            float ring_alpha = NOTE_RING_ALPHA_MIN + (NOTE_RING_ALPHA - NOTE_RING_ALPHA_MIN) * ring_glow;
            if (!is_next_note(rh, next_note, i)) ring_alpha *= NOTE_RING_LATER_ALPHA;
            /* couleur du côté de la note : magenta (boutons), turquoise (croix directionnelle) ; une double
               (toujours un bouton + une direction) : moitié droite magenta, moitié gauche turquoise */
            Uint8 ring_a = (Uint8)(255.0f * ring_alpha);
            SDL_Color right = neon_glow_color(neon_for_lane(note->lane)), left = right;
            if (note->type == NOTE_DOUBLE) {
                right = neon_glow_color(NEON_MAGENTA);
                left = neon_glow_color(NEON_TEAL);
            }
            right.a = left.a = ring_a;
            /* Trait doux (cœur + fondu, 4 rangées de sommets) plutôt qu'un profil néon (12 rangées) : cet
               anneau se dessine pour chaque note en vol, chaque image, et le profil néon coûtait trop cher au
               GPU de la Vita (gel de l'écran après ~20 s, cf. GAME_DESIGN.md). Deux moitiés pour la couleur. */
            shapes_soft_arc(r, CENTER_X, CENTER_Y, ring_rx * s, ring_ry * s, NOTE_RING_WIDTH, NOTE_RING_MIST, 0.0f, 180.0f, right);
            shapes_soft_arc(r, CENTER_X, CENTER_Y, ring_rx * s, ring_ry * s, NOTE_RING_WIDTH, NOTE_RING_MIST, 180.0f, 360.0f, left);
        }
    }

    /* Notes "scratch" : bande dédiée (pas le simple écho ci-dessus) qui grossit du centre jusqu'aux arcs —
       la cible ET la note à la fois, comme dans le jeu original (voir draw_scratch_ring). */
    if (rh->chart_loaded) {
        for (int i = rh->chart.note_count - 1; i >= 0; i--) {
            const Note *note = &rh->chart.notes[i];
            if (note->type != NOTE_SCRATCH) continue;
            if (rh->note_runtime[i].judged) continue;
            float hit_time = (float)(rh->chart.offset_ms + note->time_ms);
            float spawn_time = hit_time - rh->approach_ms;
            float p = (rh->song_time_ms - spawn_time) / rh->approach_ms;
            float late = rh->song_time_ms - hit_time;   /* > 0 : après l'instant parfait, encore validable */
            if (p < 0.0f || late > SCRATCH_WINDOW_GOOD_MS) continue;
            float fade = (NOTE_FADE_IN_P > 0.0f && p < NOTE_FADE_IN_P) ? p / NOTE_FADE_IN_P : 1.0f;
            if (late > 0.0f) fade = 1.0f - late / SCRATCH_WINDOW_GOOD_MS;   /* dépasse les arcs en s'estompant */
            float s = NOTE_SPAWN_SCALE_RING + (1.0f - NOTE_SPAWN_SCALE_RING) * p;
            draw_scratch_ring(r, font, ring_rx * s, ring_ry * s, s, rh->song_time_ms, note->scratch_ccw,
                              (Uint8)(255.0f * fade));
        }
    }

    /* Arcs en aplat (cyan à gauche, rose à droite) cerclés d'encre : images préparées au démarrage
       (sprites_build_arcs), qui passent sous les cibles ; chaque cible pose un disque d'encre par-dessus */
    sprite_draw_arcs(sprites, r);

    /* Réaction de l'arc (D) : le tronçon voisin de la cible réussie s'éclaire et l'éclat s'étend le long de l'arc */
    for (int i = 0; i < LANE_COUNT; i++) {
        const Button b = LANES[i].button;
        if (rh->lane_flash_timer[b] <= 0.0f || rh->lane_flash_judgment[b] == JUDGE_MISS) continue;
        const float el = FLASH_DURATION_MS - rh->lane_flash_timer[b];
        if (el >= HIT_ARC_MS) continue;
        const float t = el / HIT_ARC_MS, grow = 1.0f - (1.0f - t) * (1.0f - t);
        const float reach = 5.0f + (HIT_ARC_REACH_DEG - 5.0f) * grow;
        const bool right = LANES[i].side == ARC_RIGHT;
        const float lo = right ? 18.0f : 198.0f, hi = right ? 162.0f : 342.0f;   /* étendue des arcs dessinés */
        float a0 = LANES[i].angle_deg - reach, a1 = LANES[i].angle_deg + reach;
        a0 = a0 < lo ? lo : a0;
        a1 = a1 > hi ? hi : a1;
        float cx, cy;
        arc_center(LANES[i].side, &cx, &cy);
        SDL_Color c = right ? (SDL_Color){255, 190, 225, 255} : (SDL_Color){215, 245, 255, 255};   /* teinte de côté éclaircie */
        if (rh->lane_flash_judgment[b] == JUDGE_PERFECT) c = font_lighten(c, 0.35f);
        c.a = (Uint8)(255.0f * (1.0f - t) * (1.0f - t * 0.4f));
        shapes_arc_outline(r, cx, cy, ARC_RX, ARC_RY, 11.0f, a0, a1, c);
    }

    /* Cibles des lanes, avec flash de jugement derrière */
    for (int i = 0; i < LANE_COUNT; i++) {
        float x, y;
        lane_pos(LANES[i].side, LANES[i].angle_deg, ARC_RX, ARC_RY, &x, &y);
        Button b = LANES[i].button;
        sprite_draw_note_disc(sprites, r, x, y, TARGET_INK_RADIUS / NOTE_DISC_RADIUS, CH_INK, UI_NONE);

        /* Liseré néon derrière la cible ; il s'illumine brièvement à chaque appui sur la lane */
        sprite_draw_target_ring(sprites, r, neon_for_lane(b), x, y,
            rh->lane_press_timer[b] > 0.0f ? rh->lane_press_timer[b] / PRESS_FLASH_DURATION_MS : 0.0f);

        /* Effet d'empreinte : cible estompée tant que le bouton est
           physiquement enfoncé, indépendamment de tout jugement. */
        sprite_draw_target(sprites, r, b, x, y, rh->lane_held[b] ? 130 : 255);

        /* flash de jugement SUR la cible (effet d'origine, MISS compris) : le disque de la couleur du jugement couvre
           l'icône et le liseré, grossit et s'efface ; avant, il était dessiné dessous et presque entièrement caché */
        if (rh->lane_flash_timer[b] > 0.0f) {
            const float el = FLASH_DURATION_MS - rh->lane_flash_timer[b];
            if (el < HIT_FLASH_MS) {
                const float t = 1.0f - el / HIT_FLASH_MS;   /* 1 -> 0 */
                SDL_Color flash = judgment_color(rh->lane_flash_judgment[b]);
                flash.a = (Uint8)(190.0f * t);
                shapes_filled_circle(r, x, y, ICON_RADIUS * (1.0f + 0.5f * (1.0f - t)) + 4.0f, flash);
            }
        }

        /* Anneau d'appui : réagit à tout appui sur cette lane, indépendamment
           du jugement (même sans note à portée), pour confirmer l'input. */
        if (rh->lane_press_timer[b] > 0.0f) {
            float t = rh->lane_press_timer[b] / PRESS_FLASH_DURATION_MS; /* 1 -> 0 */
            float ring_radius = ICON_RADIUS * (1.15f + 0.5f * (1.0f - t));
            SDL_Color ring = {255, 255, 255, (Uint8)(200.0f * t)};
            shapes_circle_outline(r, x, y, ring_radius, 3.0f, ring);
        }

    }

    /* Notes (approche, tenues, doubles), dessinées par-dessus les cibles. Ordre inverse de la carte : la
       note à frapper en premier est dessinée en dernier, donc jamais cachée par celles qui la suivent. */
    if (rh->chart_loaded) {
        for (int i = rh->chart.note_count - 1; i >= 0; i--) {
            const Note *note = &rh->chart.notes[i];
            if (note->type == NOTE_SCRATCH) continue; /* dessinée séparément (anneau dédié) */
            const NoteRuntime *nr = &rh->note_runtime[i];
            float hit_time = (float)(rh->chart.offset_ms + note->time_ms);
            float spawn_time = hit_time - rh->approach_ms;
            float end_time = hit_time + (float)note->duration_ms;

            /* Une note tenue reste visible (traîne qui se raccourcit) tant
               qu'elle est activement maintenue. Une note ratée (jamais
               commencée à temps) continue sa course au lieu de s'arrêter
               net sur la cible, pendant une courte fenêtre de dépassement. */
            bool start_missed = nr->judged && nr->judgment == JUDGE_MISS;
            bool visible = (note->type == NOTE_HOLD)
                ? (!nr->judged || nr->hold_active || start_missed)
                : (!nr->judged || start_missed);
            if (!visible) continue;

            float upper_bound;
            if (note->type == NOTE_HOLD && nr->hold_active) upper_bound = end_time;
            else if (start_missed) upper_bound = hit_time + WINDOW_GOOD_MS + MISS_OVERSHOOT_MS;
            else upper_bound = hit_time + WINDOW_GOOD_MS;
            if (rh->song_time_ms < spawn_time || rh->song_time_ms > upper_bound) continue;

            /* Une double note a deux têtes (une par lane), reliées par un trait ;
               une tête déjà frappée disparaît en attendant l'autre. */
            /* Note ratée : elle continue sa course au-delà de la cible, tamisée, puis s'efface */
            float alpha = 1.0f;
            if (start_missed) {
                float left = upper_bound - rh->song_time_ms;
                alpha = MISS_NOTE_ALPHA * (left < MISS_FADE_OUT_MS ? (left < 0.0f ? 0.0f : left / MISS_FADE_OUT_MS) : 1.0f);
            }
            const int lane_n = (note->type == NOTE_DOUBLE) ? 2 : 1;
            const Button lanes[2] = { note->lane, note->lane2 };
            float hx[2] = {0}, hy[2] = {0};
            bool show[2] = { true, true };

            float p = (rh->song_time_ms - spawn_time) / rh->approach_ms;
            if (p < 0.0f) p = 0.0f;
            /* Tant qu'une note tenue est activement maintenue, sa tête reste
               ancrée sur l'icône ; sinon (approche, ou ratée) elle continue
               sa course sans s'arrêter au niveau de la cible. */
            if (note->type == NOTE_HOLD && nr->hold_active) p = 1.0f;

            /* taille (grandit jusqu'à la cible) et fondu d'apparition, selon la progression */
            float pc = p > 1.0f ? 1.0f : p;
            float size = note_size(rh, pc);
            float fade = (NOTE_FADE_IN_P > 0.0f && pc < NOTE_FADE_IN_P) ? pc / NOTE_FADE_IN_P : 1.0f;
            alpha *= fade;
            /* notes qui ne sont pas les prochaines de leur lane : atténuées (tête, traîne, lien des doubles) */
            float later = is_next_note(rh, next_note, i) ? 1.0f : NOTE_LATER_ALPHA;
            alpha *= later;

            for (int k = 0; k < lane_n; k++) {
                const LaneSlot *slot = find_lane_slot(lanes[k]);
                if (!slot) { show[k] = false; continue; }

                float icon_x, icon_y;
                lane_pos(slot->side, slot->angle_deg, ARC_RX, ARC_RY, &icon_x, &icon_y);
                /* point d'apparition propre à la lane, puis trajet rectiligne jusqu'à la cible */
                float spawn_x = CENTER_X + (icon_x - CENTER_X) * NOTE_SPAWN_SCALE_X;
                float spawn_y = CENTER_Y + (icon_y - CENTER_Y) * NOTE_SPAWN_SCALE_Y;
                hx[k] = spawn_x + (icon_x - spawn_x) * p;
                hy[k] = spawn_y + (icon_y - spawn_y) * p;

                if (note->type == NOTE_DOUBLE && !start_missed && (k == 0 ? nr->a_hit : nr->b_hit))
                    show[k] = false;

                if (note->type == NOTE_HOLD && !start_missed) { /* départ raté : plus de traîne */
                    /* Traîne derrière la note : sa longueur (en temps de trajet)
                       représente la durée à tenir. Pendant l'approche elle suit la
                       tête ; pendant le maintien elle se raccourcit jusqu'à
                       rejoindre l'icône exactement à la fin de la tenue. */
                    float tail_p;
                    if (nr->hold_active) {
                        float remaining_frac = (end_time - rh->song_time_ms) / (float)note->duration_ms;
                        if (remaining_frac < 0.0f) remaining_frac = 0.0f;
                        if (remaining_frac > 1.0f) remaining_frac = 1.0f;
                        tail_p = 1.0f - remaining_frac * ((float)note->duration_ms / rh->approach_ms);
                    } else {
                        tail_p = p - (float)note->duration_ms / rh->approach_ms;
                    }
                    if (tail_p < 0.0f) tail_p = 0.0f;
                    float tx = spawn_x + (icon_x - spawn_x) * tail_p;
                    float ty = spawn_y + (icon_y - spawn_y) * tail_p;
                    /* capsule de la note de fin à la tête, chaque bout à la taille de sa note (perspective) */
                    float tail_size = note_size(rh, tail_p);
                    SDL_Color col = note_border_color(lanes[k]);
                    float a = fade * later;
                    Uint8 fill_a = (Uint8)((nr->hold_active ? HOLD_CAPSULE_HELD_FILL : HOLD_CAPSULE_IDLE_FILL) * a);
                    draw_hold_capsule(r, tx, ty, tail_size, hx[k], hy[k], size, ui_alpha(col, fill_a),
                                      ui_alpha(col, (Uint8)(255.0f * a)), nr->hold_active ? a : 0.0f, rh->song_time_ms);
                    /* note de fin : la même que la tête (symbole de la lane), dessinée sous elle */
                    draw_note_head(r, sprites, lanes[k], tx, ty, col, alpha, tail_size);
                }
            }

            if (note->type == NOTE_DOUBLE) {   /* l'os : barre et deux bouts d'un seul dessin (la barre disparaît si une tête est frappée) */
                draw_link(r, sprites, lanes, hx, hy, show, size, alpha);
                continue;
            }

            for (int k = 0; k < lane_n; k++) {
                if (!show[k]) continue;
                draw_note_head(r, sprites, lanes[k], hx[k], hy[k], note_border_color(lanes[k]), alpha, size);
            }
        }
    }

    if (!rh->finished) draw_hit_effects(rh, r);   /* par-dessus les notes et leurs têtes */

    if (rh->finished) {
        draw_results(rh, r, font, sprites, bg, best_score, new_best, night);
    } else {
        draw_progress_bar(rh, r);
        draw_hud(rh, r, font);
        /* bandeau du morceau en haut à gauche : « NIGHT nn » rose, titre d'encre */
        char night_txt[16], title[96], fit[96];
        snprintf(night_txt, sizeof night_txt, "NIGHT %02d", night);
        ui_upper(rh->chart.title, title, sizeof title);
        ui_fit_text(font, FONT_POP_S, title, 300, fit, sizeof fit);
        const char *tags[2] = { night_txt, fit };
        const SDL_Color tag_bg[2] = { CH_PINK, CH_INK };
        if (night > 0) ui_tags(r, font, 22.0f, 18.0f, tags, tag_bg, fit[0] ? 2 : 1, 255);
        else if (fit[0]) ui_tags(r, font, 22.0f, 18.0f, tags + 1, tag_bg + 1, 1, 255);
    }
}
