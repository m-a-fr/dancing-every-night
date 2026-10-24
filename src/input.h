#ifndef INPUT_H
#define INPUT_H
#include <stdbool.h>

/* Boutons logiques, nommés comme sur la Vita. Le jeu ne lit QUE cette structure. */
typedef enum {
    BTN_CROSS, BTN_CIRCLE, BTN_SQUARE, BTN_TRIANGLE,
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_L, BTN_R, BTN_START, BTN_SELECT,
    BTN_COUNT
} Button;

typedef struct {
    bool  held[BTN_COUNT];     /* enfoncé cette frame */
    bool  pressed[BTN_COUNT];  /* vient d'être enfoncé (front montant) */
    float lx, ly;              /* stick gauche, [-1, 1], zone morte appliquée */
    float rx, ry;              /* stick droit */
    /* "Kick" (scratch, à la manière du jeu de référence) : à vrai une seule image, quand le stick gauche OU
       droit part du neutre vers une forte déflexion en peu de temps (un coup sec, pas une pression
       lente). Sens déterminé par la direction dominante du coup : bas/gauche = horaire, haut/droite =
       antihoraire. Indépendant de lx/ly (qui reçoivent aussi la croix directionnelle : un flick sur
       le stick réel, jamais un appui sur la croix). */
    bool  scratch_cw, scratch_ccw;
    bool  quit_requested;      /* fermeture fenêtre (desktop) */
} Input;

void input_init(void);
void input_poll(Input *in, float dt);
void input_shutdown(void);

#endif
