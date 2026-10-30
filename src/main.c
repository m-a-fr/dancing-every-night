#include "sdl.h"
#include "config.h"
#include "input.h"
#include "game.h"
#include "perf.h"
#include "mapcheck.h"
#include "files.h"
#include <stdio.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
/* Taille du tas newlib. Augmenter si le jeu charge beaucoup d'assets. */
unsigned int _newlib_heap_size_user = 128 * 1024 * 1024;
#endif

#if defined(__vita__) && defined(VITA_PAD_LINK)
/* Bloc de remplissage généré par CMake (voir CMakeLists.txt / scripts/build-vita.sh) */
extern const unsigned char vita_pad[];
#endif

#define MAX_DT (1.0f / 20.0f) /* évite l'effet "téléportation" après un lag */

#ifndef __vita__
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  define popen  _popen    /* VITAGAME_RECORD (outil de dev) */
#  define pclose _pclose
#endif
/* Outil de dev (desktop) : VITAGAME_SHOT=capture.bmp [VITAGAME_SHOT_FRAME=n] enregistre l'image n
   (60 par défaut) puis quitte. Sert à vérifier l'UI sans lancer le jeu à la main. */
static bool screenshot_due(SDL_Renderer *ren, int frame) {
    const char *path = getenv("VITAGAME_SHOT");
    if (!path) return false;
    const char *n = getenv("VITAGAME_SHOT_FRAME");
    if (frame != (n ? atoi(n) : 60)) return false;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s) {
        SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        SDL_SaveBMP(s, path);
        SDL_FreeSurface(s);
    }
    return true;
}

/* Outil de dev (desktop) : VITAGAME_KEYS="30:CROSS,60:DOWN,90:L" simule un appui sur le bouton donné à
   l'image donnée (numérotée comme VITAGAME_SHOT_FRAME). Sert à atteindre un écran ou à tester un parcours de
   menu sans manette, en combinaison avec VITAGAME_SHOT. Boutons : UP DOWN LEFT RIGHT CROSS CIRCLE SQUARE
   TRIANGLE L R START SELECT. */
static void inject_keys(Input *in, int frame) {
    static const struct { const char *name; Button b; } NAMES[] = {
        {"UP", BTN_UP}, {"DOWN", BTN_DOWN}, {"LEFT", BTN_LEFT}, {"RIGHT", BTN_RIGHT},
        {"CROSS", BTN_CROSS}, {"CIRCLE", BTN_CIRCLE}, {"SQUARE", BTN_SQUARE}, {"TRIANGLE", BTN_TRIANGLE},
        {"L", BTN_L}, {"R", BTN_R}, {"START", BTN_START}, {"SELECT", BTN_SELECT},
    };
    const char *spec = getenv("VITAGAME_KEYS");
    if (!spec) return;
    for (const char *p = spec; *p; ) {
        char *end;
        long f = strtol(p, &end, 10);
        if (end == p || *end != ':') break;
        const char *name = end + 1;
        size_t len = strcspn(name, ",");
        if ((int)f == frame)
            for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
                if (strlen(NAMES[i].name) == len && strncmp(NAMES[i].name, name, len) == 0)
                    in->pressed[NAMES[i].b] = in->held[NAMES[i].b] = true;
        p = name + len;
        if (*p == ',') p++;
    }
}

/* Outil de dev (desktop) : VITAGAME_RECORD=video.mp4 ./vitagame carte.chart joue la carte en autoplay,
   à 60 images/s fixes, et encode chaque image avec ffmpeg (sans le son : scripts/record-gameplay.sh
   ajoute la musique). La première image enregistrée est la première après le démarrage de la musique. */
#define RECORD_RESULTS_MS 7000.0f /* durée gardée de l'écran de résultats */

static FILE *record_open(void) {
    const char *path = getenv("VITAGAME_RECORD");
    if (!path) return NULL;
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s %dx%d -r 60 -i - "
             "-c:v libx264 -preset medium -crf 16 -pix_fmt yuv420p '%s'", SCREEN_W, SCREEN_H, path);
    FILE *f = popen(cmd, "w");
    if (!f) fprintf(stderr, "record: impossible de lancer ffmpeg\n");
    return f;
}

static void record_frame(FILE *f, SDL_Renderer *ren, Uint32 *pixels) {
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, SCREEN_W * 4);
    fwrite(pixels, 4, (size_t)SCREEN_W * SCREEN_H, f);
}
#endif

/* Outil de dev (desktop) : VITAGAME_FONTTEST=1 affiche une planche des faces « Cascade pop » au lieu du jeu
   (avec VITAGAME_SHOT pour la capturer). */
#ifndef __vita__
#include "ui.h"
static bool font_test(SDL_Renderer *ren, const Font *f) {
    if (!getenv("VITAGAME_FONTTEST")) return false;
    SDL_SetRenderDrawColor(ren, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, 255);
    SDL_RenderClear(ren);
    SDL_Color none = {0, 0, 0, 0}, ink = CH_INK;
    font_draw_ink(f, ren, FONT_POP_XL, 40, 40, ALIGN_LEFT, 1.0f, CH_GOLD, ink, ink, 5, 5, "BACKLASH Kumo wo Nuke");
    font_draw_ink(f, ren, FONT_POP_XL, 40, 130, ALIGN_LEFT, 0.6f, CH_GOLD, ink, CH_PINK, 4, 4, "ANATA NO HANA NO IRO 66 -> 40");
    font_draw_ink(f, ren, FONT_POP, 40, 200, ALIGN_LEFT, 1.0f, CH_WHITE, ink, CH_PINK, 4, 4, "PAUSE  RESUME  0 184 730");
    font_draw_ink(f, ren, FONT_POP, 40, 260, ALIGN_LEFT, 1.0f, CH_NIGHT, CH_WHITE, none, 0, 0, "IGNITE  FX SENSHI (CONTOUR)");
    SDL_SetRenderDrawColor(ren, ink.r, ink.g, ink.b, 255);
    SDL_Rect card = {40, 320, 520, 150};
    SDL_RenderFillRect(ren, &card);
    font_draw(f, ren, FONT_CARD_NAME, 60, 338, ALIGN_LEFT, CH_WHITE, "HIROMITSU KITAYAMA Éàç");
    font_draw(f, ren, FONT_CARD, 60, 380, ALIGN_LEFT, CH_MUTED, "185 BPM · 105 NOTES · BEST — —");
    font_draw(f, ren, FONT_CARD_BOLD, 60, 410, ALIGN_LEFT, CH_CYAN, "EASY  NORMAL  HARD  EXPERT");
    font_draw(f, ren, FONT_LABEL, 60, 440, ALIGN_LEFT, CH_GOLD, "NIGHT 02 / 10   SELECT PACK   ✕ PLAY");
    return true;
}
#endif

static int fatal(const char *what) {
    fprintf(stderr, "%s: %s\n", what, SDL_GetError());
#ifdef __vita__
    sceKernelExitProcess(1);
#endif
    return 1;
}

int main(int argc, char *argv[]) {

#ifdef __vita__
    /* Fréquences max : confortable pour prototyper, à réduire plus tard pour l'autonomie */
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
#endif

#ifndef __vita__
    /* Validateur de map, sans fenêtre : ./vitagame --check ma_map.zip (ou un dossier de map) */
    if (argc >= 3 && strcmp(argv[1], "--check") == 0) return mapcheck_run(argv[2], false);
    if (argc >= 3 && strcmp(argv[1], "--check-json") == 0) return mapcheck_run(argv[2], true);   /* sortie JSON : bibliothèque */
    files_enter_game_dir();   /* après --check : son chemin est relatif au dossier courant */
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
        return fatal("SDL_Init");

    SDL_Window *win = SDL_CreateWindow(GAME_NAME,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) return fatal("SDL_CreateWindow");

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) return fatal("SDL_CreateRenderer");

    /* Sur desktop, la fenêtre peut être redimensionnée : on garde 960x544 logique */
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);

#if defined(__vita__) && defined(VITA_PAD_LINK)
    if (*(volatile const unsigned char *)vita_pad == 0) return 1; /* jamais vrai : sert à garder le bloc */
#endif
    input_init();
    Input in = {0};
    Game game;
    /* Desktop : ./vitagame [chemin/vers/carte.chart] */
    game_init(&game, ren, argc > 1 ? argv[1] : NULL);

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();
    Perf perf;
    perf_init(&perf);
    float present_ms = 0.0f;
#ifndef __vita__
    int frame = 0;
    FILE *record = record_open();
    Uint32 *record_pixels = record ? malloc((size_t)SCREEN_W * SCREEN_H * 4) : NULL;
    const bool autoplay = getenv("VITAGAME_AUTOPLAY") != NULL;   /* jeu automatique sans enregistrer */
    game.fixed_clock = record != NULL || autoplay;
    /* VITAGAME_RECORD_MENU=n : avec VITAGAME_RECORD, enregistre les n premières images (menus, touches de
       VITAGAME_KEYS) au lieu d'une carte en autoplay */
    const char *menu_env = getenv("VITAGAME_RECORD_MENU");
    const int menu_frames = record && menu_env ? atoi(menu_env) : 0;
#endif

    while (!in.quit_requested && !game.wants_quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)freq;
        prev = now;
        if (dt > MAX_DT) dt = MAX_DT;

        perf_frame_begin();
        input_poll(&in, dt);
        perf_toggle_input(&perf, in.held[BTN_L], in.held[BTN_R], dt);
#ifndef __vita__
        inject_keys(&in, frame);
        if (record || autoplay) {   /* VITAGAME_AUTOPLAY=1 : jeu parfait automatique, pour les captures */
            dt = 1.0f / 60.0f;
            /* sans enregistrement, les touches simulées (VITAGAME_KEYS) gardent la main hors du jeu et en pause,
               et Start ouvre la pause */
            bool keep = autoplay && !record && (game.paused || game.screen != SCREEN_PLAY);
            bool start = in.pressed[BTN_START];
            if (!keep && !menu_frames) {
                memset(in.held, 0, sizeof in.held);
                memset(in.pressed, 0, sizeof in.pressed);
                in.scratch_cw = in.scratch_ccw = false;
                if (autoplay && !record) in.pressed[BTN_START] = start;
                if (game.screen == SCREEN_PLAY) rhythm_autoplay(&game.rhythm, dt, &in);
            }
        }
#endif
        game_update(&game, &in, dt);
#ifndef __vita__
        if (!font_test(ren, &game.font))
#endif
        game_draw(&game, ren);
        float cpu_ms = (float)(SDL_GetPerformanceCounter() - now) * 1000.0f / (float)freq;
        perf_frame_end(&perf, dt, cpu_ms, present_ms);
        perf_draw(&perf, ren, &game.font);
#ifndef __vita__
        if (screenshot_due(ren, ++frame)) break;
        if (record && menu_frames) {
            record_frame(record, ren, record_pixels);
            if (frame >= menu_frames) break;
        } else if (record && game.audio_started) {
            record_frame(record, ren, record_pixels);
            if (game.rhythm.finished && game.rhythm.results_ms > RECORD_RESULTS_MS) break;
        }
#endif
        Uint64 before_present = SDL_GetPerformanceCounter();
        SDL_RenderPresent(ren);
        present_ms = (float)(SDL_GetPerformanceCounter() - before_present) * 1000.0f / (float)freq;
    }

#ifndef __vita__
    if (record) { pclose(record); free(record_pixels); }
#endif
    game_shutdown(&game);
    input_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

#ifdef __vita__
    sceKernelExitProcess(0);
#endif
    return 0;
}
