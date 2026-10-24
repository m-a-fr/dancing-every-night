#ifndef AUDIO_H
#define AUDIO_H
#include <stdbool.h>
#include "sdl.h"
#include "sfx.h"

struct stb_vorbis;

/* Audio du jeu : piste OGG décodée en continu dans le callback (stb_vorbis),
   sinon métronome généré par code. Le callback compte les frames jouées depuis
   audio_start() : c'est l'horloge de référence de la musique. */
typedef struct {
    SDL_AudioDeviceID dev;      /* 0 = pas de périphérique audio (le jeu continue en silence) */
    Sfx    *sfx;                /* bruitages (sons de frappe, pause) mixés dans ce périphérique, NULL = aucun */
    bool    paused;             /* pause logicielle : le périphérique tourne (pour les bruitages), pas la musique */
    int    freq;
    struct stb_vorbis *music;   /* NULL = métronome seul */
    unsigned char     *music_data;
    /* Lus par le callback (thread audio) : modifiés sous SDL_LockAudioDevice */
    Sint64 samples_played;      /* frames stéréo ; négatif pendant le lead-in */
    int    beat_samples;        /* durée d'un temps en frames (métronome) */
    Sint64 offset_samples;      /* premier clic du métronome */
    int    chart_offset_ms;
    int    calib_ms;            /* calibration de latence : < 0 = compense une sortie audio en retard */
    Sint64 fade_start;          /* fondu de sortie de la musique : frame de départ, -1 = aucun */
    int    fade_frames;
} Audio;

/* music_path : fichier .ogg, ou NULL/"" pour le métronome seul. */
void audio_init(Audio *a, const char *music_path);
void audio_shutdown(Audio *a);
/* Rattache les bruitages à ce périphérique (jusqu'à audio_shutdown) */
void audio_attach_sfx(Audio *a, Sfx *sfx);
/* (Re)démarre à t = -lead_in_ms (silence avant t=0). Métronome : un clic par temps dès offset_ms. */
void audio_start(Audio *a, float bpm, int offset_ms, int lead_in_ms);
void audio_set_paused(Audio *a, bool paused);
/* Éteint la musique en fondu sur `ms` millisecondes (fin du morceau) ; l'horloge continue */
void audio_fade_out(Audio *a, int ms);
void audio_adjust_calibration(Audio *a, int delta_ms);
/* Fixe la calibration (réglage du joueur, appliqué à chaque chanson) */
void audio_set_calibration(Audio *a, int ms);
bool audio_has_music(const Audio *a);
/* Position de lecture en ms (négative pendant le lead-in), calibration incluse. */
float audio_song_time_ms(Audio *a);

#endif
