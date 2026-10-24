#include "input.h"
#include "sdl.h"
#include <math.h>
#include <string.h>

/*
 * Une seule API pour les deux plateformes : SDL_GameController.
 * Sur Vita, SDL expose la console comme une manette : Croix = A, Rond = B,
 * Carré = X, Triangle = Y. Sur desktop, clavier + manette éventuelle.
 */

#define DEADZONE 0.20f

/* Détection du "kick" (scratch) sur un stick : il part dès que le stick BOUGE, sur la valeur brute (sans la zone
   morte de 20 % du jeu, qui obligerait à un mouvement déjà franc), sans condition de vitesse ni de bout de course :
   un vrai coup sec est difficile à faire sur les petits sticks de la Vita.
   - SCRATCH_TRIGGER : déflexion brute qui déclenche le coup (0,12 = 12 % de la course, au-dessus de la dérive d'un
     stick au repos, de l'ordre de quelques %) ;
   - SCRATCH_REARM : le stick doit revenir en dessous (quasi neutre) pour qu'un nouveau coup puisse partir dans le
     même sens (sinon un stick maintenu dévié redéclencherait à chaque image). Un coup dans l'autre sens repart
     tout de suite, même sans repasser par le neutre (aller-retour rapide : le stick peut traverser le centre
     entre deux images). */
#define SCRATCH_TRIGGER      0.12f
#define SCRATCH_REARM        0.07f
#define SCRATCH_DIR_MARGIN   1.25f   /* une direction l'emporte si son axe dépasse l'autre de 25 % (diagonales : on ne rebascule pas) */

typedef struct { bool fired; int dir; } KickState;   /* dir du dernier coup : +1 horaire, -1 antihoraire */
static KickState kick_l, kick_r;

static SDL_GameController *pad = NULL;

static void open_first_pad(void) {
    if (pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) return;
        }
    }
}

void input_init(void) { open_first_pad(); }

void input_shutdown(void) {
    if (pad) SDL_GameControllerClose(pad);
    pad = NULL;
}

/* Valeur brute du stick, [-1, 1], sans zone morte (détection du scratch) */
static float raw_axis(SDL_GameControllerAxis a) {
    if (!pad) return 0.0f;
    float v = SDL_GameControllerGetAxis(pad, a) / 32767.0f;
    return v < -1.0f ? -1.0f : v;
}

static float axis(SDL_GameControllerAxis a) {
    if (!pad) return 0.0f;
    float v = SDL_GameControllerGetAxis(pad, a) / 32767.0f;
    if (v < -1.0f) v = -1.0f;
    if (fabsf(v) < DEADZONE) return 0.0f;
    /* Remise à l'échelle après zone morte pour garder une réponse progressive */
    return (v > 0 ? v - DEADZONE : v + DEADZONE) / (1.0f - DEADZONE);
}

static bool btn(SDL_GameControllerButton b) {
    return pad && SDL_GameControllerGetButton(pad, b);
}

static void detect_kick(KickState *ks, float x, float y, bool *cw, bool *ccw) {
    float m = sqrtf(x * x + y * y);
    if (m < SCRATCH_REARM) { ks->fired = false; ks->dir = 0; return; }
    if (m < SCRATCH_TRIGGER) return;
    /* direction dominante : bas/gauche = horaire, haut/droite = antihoraire */
    bool horiz = fabsf(x) > fabsf(y) * SCRATCH_DIR_MARGIN, vert = fabsf(y) > fabsf(x) * SCRATCH_DIR_MARGIN;
    if (!horiz && !vert) {
        if (ks->fired) return;            /* diagonale franche : on garde la direction du coup déjà parti */
        horiz = fabsf(x) > fabsf(y);
    }
    int dir = horiz ? (x > 0.0f ? -1 : 1) : (y > 0.0f ? 1 : -1);
    if (ks->fired && ks->dir == dir) return;
    ks->fired = true;
    ks->dir = dir;
    if (dir > 0) *cw = true; else *ccw = true;
}

void input_poll(Input *in, float dt) {
    bool prev[BTN_COUNT];
    memcpy(prev, in->held, sizeof prev);

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: in->quit_requested = true; break;
        case SDL_CONTROLLERDEVICEADDED: open_first_pad(); break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (pad && e.cdevice.which ==
                SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                SDL_GameControllerClose(pad);
                pad = NULL;
                open_first_pad();
            }
            break;
        /* TODO tactile : SDL_FINGERDOWN / SDL_FINGERMOTION (x, y normalisés 0..1).
           Sur Vita, touchId 0 = écran avant, 1 = pavé arrière. */
        default: break;
        }
    }

    const Uint8 *k = SDL_GetKeyboardState(NULL);

    in->held[BTN_CROSS]    = btn(SDL_CONTROLLER_BUTTON_A) || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J];
    in->held[BTN_CIRCLE]   = btn(SDL_CONTROLLER_BUTTON_B) || k[SDL_SCANCODE_K];
    in->held[BTN_SQUARE]   = btn(SDL_CONTROLLER_BUTTON_X) || k[SDL_SCANCODE_U];
    in->held[BTN_TRIANGLE] = btn(SDL_CONTROLLER_BUTTON_Y) || k[SDL_SCANCODE_I];
    in->held[BTN_UP]       = btn(SDL_CONTROLLER_BUTTON_DPAD_UP)    || k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W];
    in->held[BTN_DOWN]     = btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S];
    in->held[BTN_LEFT]     = btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A];
    in->held[BTN_RIGHT]    = btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
    in->held[BTN_L]        = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)  || k[SDL_SCANCODE_Q];
    in->held[BTN_R]        = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || k[SDL_SCANCODE_E];
    in->held[BTN_START]    = btn(SDL_CONTROLLER_BUTTON_START) || k[SDL_SCANCODE_RETURN];
    in->held[BTN_SELECT]   = btn(SDL_CONTROLLER_BUTTON_BACK)  || k[SDL_SCANCODE_BACKSPACE];

    for (int i = 0; i < BTN_COUNT; i++)
        in->pressed[i] = in->held[i] && !prev[i];

    in->lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
    in->ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    in->rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    in->ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);

    /* Détection du kick sur les axes bruts (avant le report de la croix directionnelle ci-dessous,
       sinon un appui sur Haut/Gauche/Bas déclencherait un scratch au passage). Stick gauche OU droit,
       équivalents (le joueur utilise le pouce disponible). */
    in->scratch_cw = in->scratch_ccw = false;
    (void)dt;
    detect_kick(&kick_l, raw_axis(SDL_CONTROLLER_AXIS_LEFTX), raw_axis(SDL_CONTROLLER_AXIS_LEFTY),
                &in->scratch_cw, &in->scratch_ccw);
    detect_kick(&kick_r, raw_axis(SDL_CONTROLLER_AXIS_RIGHTX), raw_axis(SDL_CONTROLLER_AXIS_RIGHTY),
                &in->scratch_cw, &in->scratch_ccw);

    /* La croix directionnelle / le clavier alimentent aussi le stick gauche */
    if (in->held[BTN_LEFT])  in->lx = -1.0f;
    if (in->held[BTN_RIGHT]) in->lx =  1.0f;
    if (in->held[BTN_UP])    in->ly = -1.0f;
    if (in->held[BTN_DOWN])  in->ly =  1.0f;
}
