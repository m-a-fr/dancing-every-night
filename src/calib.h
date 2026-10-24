#ifndef CALIB_H
#define CALIB_H
#include <stdbool.h>
#include "sdl.h"
#include "audio.h"
#include "font.h"
#include "input.h"
#include "sprites.h"

/* Calibration du décalage audio, à l'oreille : un clic régulier (métronome de audio.c, calé à l'échantillon près),
   le joueur tape ✕ sur chaque clic. Les CALIB_COUNT_IN premiers temps sont un décompte non mesuré, puis
   CALIB_TAPS temps mesurés. Écart de chaque frappe au temps le plus proche, sur l'horloge audio ; la médiane
   (insensible à une frappe ratée) est le retard ressenti (sortie audio + manette + réflexe), et le décalage
   proposé est son opposé (Audio.calib_ms < 0 compense un son entendu en retard). Pendant la mesure, rien ne bat
   à l'écran : un repère visuel fausserait la mesure à l'oreille. */

#define CALIB_BPM       100.0f
#define CALIB_COUNT_IN  4
#define CALIB_TAPS      16
#define CALIB_MIN_TAPS  8       /* en dessous : mesure refusée (« recommencer ») */
#define CALIB_LEAD_IN_MS 1200

typedef enum { CALIB_RUNNING, CALIB_RESULT, CALIB_FAILED } CalibPhase;

typedef struct {
    Audio      audio;             /* périphérique propre (le menu ferme le sien) : métronome + bruitages */
    CalibPhase phase;
    float      beat_ms;
    float      time_ms;           /* horloge audio (sans calibration), négative pendant l'attente du premier clic */
    float      err[CALIB_TAPS];   /* écart de chaque frappe mesurée au temps le plus proche (ms, > 0 = en retard) */
    int        n;
    int        last_beat;         /* dernier temps qui a reçu une frappe (une seule frappe comptée par temps) */
    float      median_ms;
    int        result_ms;         /* décalage proposé (= -médiane, borné) */
    float      tap_flash;         /* secondes depuis la dernière frappe acceptée (animation) */
} Calib;

/* Ouvre le périphérique audio (sfx peut être NULL) et lance la mesure */
void calib_start(Calib *c, Sfx *sfx);
/* Relance une mesure (même périphérique) */
void calib_restart(Calib *c);
void calib_stop(Calib *c);
/* Frappes (✕) pendant la mesure ; passe en CALIB_RESULT / CALIB_FAILED à la fin */
void calib_update(Calib *c, const Input *in, float dt);
/* current_ms : décalage actuellement réglé (affiché pour comparaison) */
void calib_draw(const Calib *c, SDL_Renderer *r, const Font *font, const Sprites *sprites, int current_ms);

#endif
