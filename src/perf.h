#ifndef PERF_H
#define PERF_H
#include <stdbool.h>
#include "sdl.h"
#include "font.h"

/* Mesure de performance (outil de dev) : images/s, temps CPU par image, nombre de dessins envoyés au GPU
   et mémoire des textures. Les appels SDL_Render* et SDL_*Texture* passent par les compteurs de perf.c
   (redirection dans sdl.h). Affichage : maintenir L + R une seconde, ou VITAGAME_PERF=1 sur desktop
   (qui imprime aussi une ligne par seconde sur la sortie standard). */

typedef struct {
    bool  visible;
    bool  print;           /* desktop : une ligne par seconde sur stdout */
    float hold_s;          /* durée d'appui simultané L + R */
    float acc_s;           /* fenêtre de moyenne en cours */
    int   frames;
    float cpu_sum_ms, cpu_max_ms, present_sum_ms;
    long  draws_sum, verts_sum;
    int   draws_max;
    /* dernière fenêtre d'une seconde */
    float fps, cpu_avg_ms, cpu_peak_ms, present_avg_ms;
    int   draws_avg, draws_peak, verts_avg;
} Perf;

void perf_init(Perf *p);
/* Début d'image : remet les compteurs de dessins à zéro */
void perf_frame_begin(void);
/* Fin d'image : cpu_ms = mise à jour + dessin, present_ms = attente de SDL_RenderPresent (image précédente) */
void perf_frame_end(Perf *p, float dt, float cpu_ms, float present_ms);
/* Bascule d'affichage : L + R tenus ensemble 1 s */
void perf_toggle_input(Perf *p, bool l_held, bool r_held, float dt);
void perf_draw(const Perf *p, SDL_Renderer *r, const Font *font);

/* Compteurs (lus par perf.c ; exposés pour les tests) */
int  perf_draw_calls(void);
long perf_texture_bytes(void);

#endif
