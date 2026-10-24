#ifndef CHART_H
#define CHART_H
#include <stdbool.h>
#include "input.h"

typedef enum { NOTE_TAP, NOTE_HOLD, NOTE_DOUBLE, NOTE_SCRATCH } NoteType;

typedef struct {
    int      time_ms;
    Button   lane;         /* NOTE_SCRATCH : inutilisé */
    Button   lane2;        /* NOTE_DOUBLE : deuxième lane à frapper en même temps */
    NoteType type;
    int      duration_ms;  /* > 0 seulement pour NOTE_HOLD */
    bool     scratch_ccw;  /* NOTE_SCRATCH seulement : sens antihoraire (sinon horaire) */
} Note;

/* Taille des chemins complets de médias (ux0:data/<jeu>/songs/<ID>/<fichier> tient largement) */
#define CHART_PATH   192
#define CHART_ID_MAX 48

typedef struct {
    /* Format de map (FORMAT 1) : une map = un dossier songs/<ID>/ avec ses .chart et ses médias. Les lignes
       AUDIO, BACKGROUND et VIDEO y nomment un simple fichier du dossier. Sans ligne FORMAT (format 0, cartes
       de dev), elles sont relatives à assets/. */
    int   format;
    char  id[CHART_ID_MAX];  /* identifiant stable de la map (ID, sinon nom du dossier), vide en format 0 */
    int   version;           /* VERSION de la map, 1 par défaut */
    char  mapper[64];        /* MAPPER : auteur de la carte */
    char  source[64];        /* SOURCE : œuvre d'origine (album, anime...) */
    char  license[64];       /* LICENSE : droits déclarés sur la musique et les médias */
    int   bad_lines;         /* lignes ignorées au chargement (voir stderr) */
    float bpm;
    int   offset_ms;   /* position du temps 0 dans la piste audio, en ms */
    char  title[64];      /* métadonnées d'affichage (menu) */
    char  artist[64];
    char  pack[64];       /* pack auquel appartient le morceau, vide = aucun (vue "par pack" du menu) */
    char  difficulty[24];
    int   order;          /* ORDER : rang de la difficulté dans le morceau (1 = première), 0 = ordre par défaut */
    char  background_path[CHART_PATH]; /* image de fond (chemin complet, voir chart_load), vide = aucune */
    char  video_path[CHART_PATH];      /* clip .mpg MPEG-1 (chemin complet), vide = aucun */
    int   video_offset_ms;      /* position de la piste audio à laquelle la vidéo commence (> 0 = vidéo en retard) */
    int   approach_ms;          /* temps de trajet d'une note, de son apparition à sa cible (ms) ; défaut selon la difficulté */
    int   background_dim;       /* assombrissement du fond, 0..100 */
    char  audio_path[CHART_PATH]; /* piste (chemin complet), vide = métronome seul */
    int   preview_ms;      /* début de l'aperçu musical du menu, en ms dans la piste audio (le refrain, voir chart.c) */
    int   end_ms;          /* END : fin du morceau dans la piste audio (résultats), -1 = automatique (après la dernière note) */
    Note *notes;      /* triées par time_ms croissant (responsabilité de l'auteur de la carte) */
    int   note_count;
} Chart;

/* Charge une carte depuis un fichier texte (.chart), voir GAME_DESIGN.md pour
   le format. Alloue chart->notes (à libérer avec chart_free). Retourne false
   si le fichier est illisible ; les lignes mal formées sont ignorées avec un
   message sur stderr. */
bool chart_load(Chart *chart, const char *path);
/* Temps de trajet par défaut d'une difficulté (EASY, NORMAL, HARD, EXPERT ; autres noms : celui de NORMAL) */
int chart_default_approach_ms(const char *difficulty);
void chart_free(Chart *chart);

#endif
