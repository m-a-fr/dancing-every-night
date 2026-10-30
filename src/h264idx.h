#ifndef H264IDX_H
#define H264IDX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Index d'un flux H.264 brut (Annex B, extension .h264) : où commence chaque image, lesquelles sont
   des images clés, et ce que dit l'en-tête (taille, cadence). Code portable (testable sur PC) ; le
   décodeur matériel de la Vita qui s'en sert est dans video_h264_vita.c.
   Le flux doit être produit par tools/media.py : une tranche par image, sans B-frames, en-têtes SPS/PPS répétés
   devant chaque image clé. Les images sont repérées par leur tranche de macrobloc 0 (pas besoin de délimiteurs). */
typedef struct {
    uint32_t off, size;     /* position et taille de l'image dans le fichier */
    bool     key;           /* image clé (IDR) : on peut y sauter */
} H264Au;

typedef struct {
    H264Au *au;
    int     count;
    int     width, height;  /* taille affichée, après recadrage */
    int     num_ref_frames;
    int     profile;        /* profile_idc : 66 = Baseline (le seul que lit le décodeur logiciel des PC) */
    double  fps;            /* 0 si le flux n'en dit rien */
} H264Index;

bool h264_index_build(const uint8_t *data, size_t size, H264Index *out);
void h264_index_free(H264Index *ix);
/* Dernière image clé <= frame (0 si frame est avant la première) */
int  h264_index_key_at(const H264Index *ix, int frame);

#endif
