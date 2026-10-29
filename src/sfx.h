#ifndef SFX_H
#define SFX_H
#include <stdbool.h>
#include "sdl.h"

struct stb_vorbis;

/* Bruitages d'interface, sons de frappe et musique de fond (BGM) des menus.
   Les WAV (16 bits mono 44100 Hz, tools/gen_sfx.py) sont chargés une fois dans sfx_init ; ceux du joueur dans
   custom/audio/sfx/ (CUSTOM_DIR) passent avant ceux du jeu. Les voix de l'hôtesse (voice_*) sont des bruitages comme les autres. */

#define SFX_LIST(X) \
    X(CURSOR, "cursor") X(CONFIRM, "confirm") X(CANCEL, "cancel") X(TAB, "tab") X(DIFFICULTY, "difficulty") \
    X(START, "start") X(PAUSE_OPEN, "pause_open") X(PAUSE_CLOSE, "pause_close") X(TICK, "tick") X(TICK_GO, "tick_go") \
    X(SCORE_TICK, "score_tick") X(RANK_S, "rank_s") X(RANK_A, "rank_a") X(RANK_B, "rank_b") X(RANK_C, "rank_c") \
    X(RANK_D, "rank_d") X(NEW_RECORD, "new_record") X(FULL_COMBO, "full_combo") X(ALL_PERFECT, "all_perfect") \
    X(BOOT, "boot") X(QUIT, "quit") X(PERFECT, "perfect") X(GOOD, "good") X(MISS, "miss") X(SCRATCH, "scratch") \
    X(VOICE_TITLE, "voice_title") X(VOICE_RANK_S, "voice_rank_s") X(VOICE_RANK_A, "voice_rank_a") \
    X(VOICE_RANK_B, "voice_rank_b") X(VOICE_RANK_CD, "voice_rank_cd")

typedef enum {
#define X(id, name) SFX_##id,
    SFX_LIST(X)
#undef X
    SFX_COUNT
} SfxId;

#define SFX_VOICES 8

typedef struct {
    int id;      /* -1 = voix libre */
    int pos;     /* frames déjà jouées */
} SfxVoice;

typedef struct {
    SDL_AudioDeviceID dev;       /* périphérique hôte rattaché (verrou des données partagées), 0 = aucun */
    int       freq;
    Sint16   *pcm[SFX_COUNT];    /* mono, NULL = fichier absent */
    int       len[SFX_COUNT];
    float     sfx_vol;           /* 0..1 */
    float     bgm_vol;           /* 0..1 */
    /* Lus par le callback (thread audio) : modifiés sous SDL_LockAudioDevice */
    SfxVoice  voices[SFX_VOICES];
    struct stb_vorbis *bgm;      /* NULL = pas de musique */
    unsigned char     *bgm_data;
    char       bgm_path[192];
    float      bgm_gain;         /* gain courant (fondu), rejoint bgm_target */
    float      bgm_target;       /* 0 = coupé / baissé, 1 = plein */
} Sfx;

/* Charge les WAV de assets/audio/sfx/ (ceux qui manquent restent muets). N'ouvre pas le périphérique. */
void sfx_init(Sfx *s);
void sfx_shutdown(Sfx *s);
/* Le mixeur n'ouvre aucun périphérique : sur Vita, SDL n'en accepte qu'un à la fois. Le périphérique ouvert (aperçu
   du menu, ou audio de jeu) s'y rattache avec sfx_attach(dev) et appelle sfx_mix dans son callback, après avoir
   écrit sa propre musique ; il se détache avant de se fermer. Sans périphérique rattaché, sfx_play ne fait rien. */
void sfx_attach(Sfx *s, SDL_AudioDeviceID dev);
void sfx_detach(Sfx *s);
/* Ajoute musique de fond et bruitages à `frames` frames stéréo 44100 Hz de `out` (dans le callback du périphérique) */
void sfx_mix(Sfx *s, Sint16 *out, int frames);
/* Joue un bruitage (sans effet si le périphérique est fermé) */
void sfx_play(Sfx *s, SfxId id);
/* Musique de fond en boucle (fichier OGG 44100 Hz, NULL ou "" = aucune). Même chemin = rien à faire. */
void sfx_bgm_load(Sfx *s, const char *path);
/* Volume cible de la musique (0..1) : le gain y glisse en ~0,5 s (baisse pendant l'aperçu d'une chanson) */
void sfx_bgm_target(Sfx *s, float target);

#endif
