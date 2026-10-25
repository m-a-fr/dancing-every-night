#include "game.h"
#include "config.h"
#include "ui.h"
#include "shapes.h"
#include "splash.h"
#include "mapimport.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MUSIC_FADE_MS 1500   /* fondu de la musique à la fin du morceau */

#define COUNTDOWN_MS 3000.0f

enum { PAUSE_RESUME, PAUSE_RESTART, PAUSE_MENU, PAUSE_OPTIONS };
static const char *PAUSE_LABELS[PAUSE_OPTIONS] = { "RESUME", "RESTART", "SONG SELECT" };
enum { MAIN_BEATMAPS, MAIN_LIBRARY, MAIN_OPTIONS, MAIN_QUIT, MAIN_COUNT };
static const char *MAIN_LABELS[MAIN_COUNT] = { "SONGS", "LIBRARY", "OPTIONS", "QUIT" };
enum { OPT_OFFSET, OPT_CALIBRATE, OPT_UPDATES, OPT_COUNT };
static const char *OPT_LABELS[OPT_COUNT] = { "AUDIO OFFSET", "CALIBRATE", "UPDATE CHECK" };


static void stop_song(Game *g) {
    if (!g->song_loaded) return;
    if (g->audio.dev || g->audio.music) audio_shutdown(&g->audio);
    background_free(&g->background);
    rhythm_shutdown(&g->rhythm);
    g->song_loaded = false;
}

/* Retour au menu : le périphérique des bruitages et de la musique de fond se rouvre */
static void go_menu(Game *g) {
    g->screen = SCREEN_MENU;
    preview_open(&g->menu.preview);   /* porte aussi les bruitages et la musique de fond du menu */
}

/* Chargement par étapes : la fiche de chargement s'affiche dès l'appui, puis chaque image fait une étape bloquante —
   1. carte et image de fond, 2. piste audio, 3. index de la vidéo (si la chanson en a une). La barre montre les étapes
   réellement terminées ; l'étape en cours (celle qui bloque l'affichage) est nommée sous la barre. La vidéo prend ensuite
   le relais de l'image en fondu, dès que sa première image est décodée. */
static void start_song(Game *g, const char *path, int entry) {
    stop_song(g);
    snprintf(g->load_path, sizeof g->load_path, "%s", path);
    g->entry = entry;
    g->load_done = 0;
    g->load_total = 2;
    g->loading = 1;
    g->audio_started = false;
    g->result_recorded = false;
    g->new_best = false;
    g->paused = false;
    g->countdown_ms = 0.0f;
    g->screen = SCREEN_PLAY;
    sfx_bgm_target(&g->sfx, 0.0f);   /* pas de musique de fond en partie */
}

static void load_step(Game *g) {
    const Uint32 t0 = SDL_GetTicks();
    if (g->load_done == 0) {
        rhythm_init(&g->rhythm, g->load_path);
        background_load(&g->background, g->renderer, g->rhythm.chart.background_path, g->rhythm.chart.background_dim);
        g->background.image_fade = 1.0f; /* visible tout de suite pendant le chargement (pas de fondu depuis le noir) */
        g->song_loaded = true;
        menu_suspend(&g->menu);          /* ferme le périphérique du menu ; l'audio de jeu prend le relais */
        if (g->rhythm.chart.video_path[0]) g->load_total = 3;
    } else if (g->load_done == 1) {
        audio_init(&g->audio, g->rhythm.chart.audio_path);
        audio_attach_sfx(&g->audio, &g->sfx);   /* sons de frappe et de pause dans le périphérique de jeu */
        audio_set_calibration(&g->audio, g->settings.audio_offset_ms);
    } else {
        background_load_video(&g->background, g->renderer, g->rhythm.chart.video_path, g->rhythm.chart.video_offset_ms);
        background_seek(&g->background, 0.0f); /* première image dès le départ */
    }
    fprintf(stderr, "loading: étape %d/%d en %u ms\n", g->load_done + 1, g->load_total, (unsigned)(SDL_GetTicks() - t0));
    if (++g->load_done >= g->load_total) g->loading = 0;
}

void game_init(Game *g, SDL_Renderer *r, const char *chart_path) {
    *g = (Game){0};
    g->renderer = r;
    settings_migrate_legacy();   /* avant tout chargement : les scores (menu_init) en profitent aussi */
    settings_load(&g->settings);
    /* Écran de démarrage tout de suite, puis la barre avance entre les chargements (parts mesurées sur desktop) */
    Splash splash;
    splash_init(&splash, r);
    splash_show(&splash, r, 0.0f);
    font_init(&g->font, r);
    splash_show(&splash, r, 0.20f);
    sprites_init(&g->sprites, r);
    {
        float lcx, rcx, cy, rx, ry;
        rhythm_arc_geometry(&lcx, &rcx, &cy, &rx, &ry);
        sprites_build_arcs(&g->sprites, r, lcx, rcx, cy, rx, ry);
    }
    splash_show(&splash, r, 0.32f);
    /* Maps déposées dans import/ : décompressées avant de lister les chansons (voir mapimport.h) */
    MapImport imp;
    int pending = mapimport_scan(&imp);
    for (int i = 0; i < pending; i++) {
        char label[48];
        snprintf(label, sizeof label, "IMPORTING SONGS %d / %d", i + 1, pending);
        splash_show_label(&splash, r, 0.32f + 0.14f * (float)i / (float)pending, &g->font, label);
        mapimport_run(&imp, i);
    }
    splash_show(&splash, r, 0.46f);
    menu_init(&g->menu, r);
    splash_show(&splash, r, 0.56f);
    title_init(&g->title, r);
    splash_show(&splash, r, 0.75f);
    sfx_init(&g->sfx);
    g->entry = -1;
    g->menu.preview.sfx = &g->sfx;
    if (chart_path) {
        start_song(g, chart_path, menu_find_entry(&g->menu, chart_path));
    } else {
        g->screen = SCREEN_TITLE;
        sfx_bgm_load(&g->sfx, ASSET_PATH("audio/bgm/menu.ogg"));
        preview_open(&g->menu.preview);
        sfx_play(&g->sfx, SFX_BOOT);
    }
    splash_show(&splash, r, 1.0f);
    splash_shutdown(&splash);
    if (!chart_path && g->settings.update_check) { g->update = update_create(); update_start(g->update, GAME_VERSION); }
}

void game_shutdown(Game *g) {
    stop_song(g);
    if (g->screen == SCREEN_CALIB) calib_stop(&g->calib);
    library_destroy(g->library);
    menu_shutdown(&g->menu);   /* ferme le périphérique qui mixe les bruitages : ils peuvent ensuite être libérés */
    sfx_shutdown(&g->sfx);
    title_shutdown(&g->title);
    sprites_shutdown(&g->sprites);
    font_shutdown(&g->font);
}

static void restart_song(Game *g) {
    rhythm_restart(&g->rhythm);
    g->audio_started = false; /* la musique redémarre à la fin du compte à rebours */
    g->result_recorded = false;
    g->new_best = false;
}

static void update_pause_menu(Game *g, const Input *in, float dt) {
    if (in->pressed[BTN_DOWN]) g->pause_sel = (g->pause_sel + 1) % PAUSE_OPTIONS;
    if (in->pressed[BTN_UP])   g->pause_sel = (g->pause_sel + PAUSE_OPTIONS - 1) % PAUSE_OPTIONS;

    ui_approach(&g->pause_cursor, (float)g->pause_sel, 18.0f, dt);

    /* Décalage audio (L = -10 ms, R = +10 ms) : en pause, car en jeu L et R valident les scratches ; sauvegardé */
    if (in->pressed[BTN_L] || in->pressed[BTN_R]) {
        g->settings.audio_offset_ms = settings_clamp_offset(g->settings.audio_offset_ms + (in->pressed[BTN_R] ? 10 : -10));
        audio_set_calibration(&g->audio, g->settings.audio_offset_ms);
        settings_save(&g->settings);
    }

    int choice = -1;
    if (in->pressed[BTN_START]) choice = PAUSE_RESUME;
    if (in->pressed[BTN_CROSS]) choice = g->pause_sel;

    if (choice == PAUSE_RESUME || choice == PAUSE_RESTART) {
        if (choice == PAUSE_RESTART) restart_song(g);
        g->paused = false;
        g->countdown_ms = COUNTDOWN_MS;
        g->countdown_n = 0;
        sfx_play(&g->sfx, SFX_PAUSE_CLOSE);
    } else if (choice == PAUSE_MENU) {
        stop_song(g);
        go_menu(g);
    }
}

/* Un bruitage par type d'évènement de l'image (pas d'empilement si plusieurs notes tombent ensemble) */
static void play_hit_sounds(Game *g) {
    RhythmState *rh = &g->rhythm;
    if (rh->snd_judgment[JUDGE_PERFECT]) sfx_play(&g->sfx, SFX_PERFECT);
    else if (rh->snd_judgment[JUDGE_GOOD]) sfx_play(&g->sfx, SFX_GOOD);
    if (rh->snd_scratch) sfx_play(&g->sfx, SFX_SCRATCH);
    if (rh->snd_judgment[JUDGE_MISS]) sfx_play(&g->sfx, SFX_MISS);
    memset(rh->snd_judgment, 0, sizeof rh->snd_judgment);
    rh->snd_scratch = 0;
}

static void update_play(Game *g, const Input *in, float dt) {
    if (g->loading > 0) {
        background_update(&g->background, dt);
        load_step(g);   /* la fiche de cette étape a été affichée à l'image précédente */
        return;
    }

    bool finished = g->rhythm.finished;
    background_update(&g->background, dt); /* fondus + téléversement de l'image vidéo, même en pause */

    /* Retour au menu depuis l'écran de résultats (Rond est une lane en jeu) */
    if (finished && in->pressed[BTN_CIRCLE]) {
        stop_song(g);
        go_menu(g);
        return;
    }

    if (g->paused) { update_pause_menu(g, in, dt); return; }

    /* Start : pause (ou annule le compte à rebours de reprise et repasse en pause) */
    if (in->pressed[BTN_START] && !finished) {
        g->paused = true;
        g->pause_sel = PAUSE_RESUME;
        g->pause_cursor = PAUSE_RESUME;
        g->countdown_ms = 0.0f;
        audio_set_paused(&g->audio, true);
        sfx_play(&g->sfx, SFX_PAUSE_OPEN);
        return;
    }

    if (g->countdown_ms > 0.0f) {
        g->countdown_ms -= dt * 1000.0f;
        int n = (int)((g->countdown_ms + 999.0f) / 1000.0f);
        if (g->countdown_ms <= 0.0f) sfx_play(&g->sfx, SFX_TICK_GO);
        else if (n != g->countdown_n) sfx_play(&g->sfx, SFX_TICK);
        g->countdown_n = n;
        if (g->countdown_ms <= 0.0f) {
            g->countdown_ms = 0.0f;
            if (g->audio_started) audio_set_paused(&g->audio, false);
        }
        return;
    }

    /* La musique (ou le métronome) démarre avec la carte, et se relance avec elle (Select) */
    if (!g->audio_started || in->pressed[BTN_SELECT]) {
        audio_start(&g->audio, g->rhythm.chart.bpm, g->rhythm.chart.offset_ms, SONG_LEAD_IN_MS);
        g->audio_started = true;
        g->result_recorded = false;
        g->new_best = false;
    }

    rhythm_update(&g->rhythm, in, dt);
    play_hit_sounds(g);
    if (audio_has_music(&g->audio) && !g->fixed_clock) rhythm_sync_time(&g->rhythm, audio_song_time_ms(&g->audio));
    background_seek(&g->background, g->rhythm.song_time_ms);

    if (g->rhythm.finished && !g->result_recorded) {
        g->result_recorded = true;
        audio_fade_out(&g->audio, MUSIC_FADE_MS);   /* fin du morceau : la musique s'éteint sous les résultats */
        g->new_best = menu_record_score(&g->menu, g->entry, g->rhythm.score);
    }
}

/* Bruitages du menu, déduits du changement de sélection (le menu ne connaît pas le son) et musique de fond
   baissée tant qu'un extrait de chanson est voulu */
typedef struct { MenuTab tab; bool in_pack; int pack, song, diff; bool confirm_delete; } MenuSel;
static void menu_sfx(Game *g, const MenuSel *b) {
    Menu *m = &g->menu;
    if (m->just_deleted)                 { sfx_play(&g->sfx, SFX_QUIT); m->just_deleted = false; }
    else if (m->confirm_delete != b->confirm_delete) sfx_play(&g->sfx, m->confirm_delete ? SFX_CONFIRM : SFX_CANCEL);
    else if (m->tab != b->tab)           sfx_play(&g->sfx, SFX_TAB);
    else if (m->in_pack != b->in_pack)   sfx_play(&g->sfx, m->in_pack ? SFX_CONFIRM : SFX_CANCEL);
    else if (m->pack != b->pack || m->song != b->song) sfx_play(&g->sfx, SFX_CURSOR);
    else if (m->diff != b->diff)         sfx_play(&g->sfx, SFX_DIFFICULTY);
    sfx_bgm_target(&g->sfx, m->preview.want_path[0] ? 0.0f : 1.0f);
}

/* Menu principal et options : scène du titre qui défile, musique de fond, aucun extrait de chanson */
static void menu_ambient(Game *g, float dt) {
    title_update(&g->title, dt);
    preview_want(&g->menu.preview, "", 0);
    preview_update(&g->menu.preview, dt);
    sfx_bgm_target(&g->sfx, 1.0f);
}

/* Haut / bas dans une liste en cascade (boucle), avec le bruitage du curseur */
static void list_nav(Game *g, const Input *in, int *sel, float *cursor, int n, float dt) {
    int before = *sel;
    if (in->pressed[BTN_DOWN]) *sel = (*sel + 1) % n;
    if (in->pressed[BTN_UP])   *sel = (*sel + n - 1) % n;
    if (*sel != before) sfx_play(&g->sfx, SFX_CURSOR);
    ui_approach(cursor, (float)*sel, 18.0f, dt);
}

static void update_main(Game *g, const Input *in, float dt) {
    menu_ambient(g, dt);
    list_nav(g, in, &g->main_sel, &g->main_cursor, MAIN_COUNT, dt);
    if (in->pressed[BTN_CIRCLE]) { g->screen = SCREEN_TITLE; sfx_play(&g->sfx, SFX_CANCEL); return; }
    if (!in->pressed[BTN_CROSS]) return;
    if (g->main_sel == MAIN_BEATMAPS) {
        g->screen = SCREEN_MENU;
        sfx_play(&g->sfx, SFX_CONFIRM);
    } else if (g->main_sel == MAIN_LIBRARY) {
        if (!g->library) g->library = library_create(g->renderer);
        library_open(g->library);
        g->screen = SCREEN_LIBRARY;
        sfx_play(&g->sfx, SFX_CONFIRM);
    } else if (g->main_sel == MAIN_OPTIONS) {
        g->screen = SCREEN_OPTIONS;
        g->opt_sel = 0;
        g->opt_cursor = 0.0f;
        sfx_play(&g->sfx, SFX_CONFIRM);
    } else {
        sfx_play(&g->sfx, SFX_QUIT);
        g->wants_quit = true;
    }
}

static void update_library(Game *g, const Input *in, float dt) {
    menu_ambient(g, dt);
    int ev = library_update(g->library, in, dt);
    if (ev & LIB_EV_BACK) { g->screen = SCREEN_MAIN; sfx_play(&g->sfx, SFX_CANCEL); }
    else if (ev & LIB_EV_INSTALLED) { menu_reload(&g->menu); sfx_play(&g->sfx, SFX_START); }
    else if (ev & LIB_EV_CONFIRM) sfx_play(&g->sfx, SFX_CONFIRM);
    else if (ev & LIB_EV_CHANGE) sfx_play(&g->sfx, SFX_DIFFICULTY);
    else if (ev & LIB_EV_CURSOR) sfx_play(&g->sfx, SFX_CURSOR);
}

static void update_options(Game *g, const Input *in, float dt) {
    menu_ambient(g, dt);
    list_nav(g, in, &g->opt_sel, &g->opt_cursor, OPT_COUNT, dt);
    if (g->opt_sel == OPT_OFFSET) {   /* ← → : 1 ms, L / R : 10 ms */
        int d = (in->pressed[BTN_RIGHT] ? 1 : 0) - (in->pressed[BTN_LEFT] ? 1 : 0) + (in->pressed[BTN_R] ? 10 : 0)
              - (in->pressed[BTN_L] ? 10 : 0);
        if (d) {
            g->settings.audio_offset_ms = settings_clamp_offset(g->settings.audio_offset_ms + d);
            settings_save(&g->settings);
            sfx_play(&g->sfx, SFX_DIFFICULTY);
        }
    }
    if (g->opt_sel == OPT_UPDATES && (in->pressed[BTN_CROSS] || in->pressed[BTN_LEFT] || in->pressed[BTN_RIGHT])) {
        g->settings.update_check = !g->settings.update_check;
        settings_save(&g->settings);
        if (g->settings.update_check && !g->update) { g->update = update_create(); update_start(g->update, GAME_VERSION); }
        sfx_play(&g->sfx, SFX_DIFFICULTY);
    }
    if (in->pressed[BTN_CIRCLE]) { g->screen = SCREEN_MAIN; sfx_play(&g->sfx, SFX_CANCEL); return; }
    if (in->pressed[BTN_CROSS] && g->opt_sel == OPT_CALIBRATE) {
        menu_suspend(&g->menu);    /* ferme le périphérique du menu : la calibration ouvre le sien (un seul sur Vita) */
        g->sfx.bgm_target = g->sfx.bgm_gain = 0.0f;   /* pas de musique de fond sous le métronome */
        calib_start(&g->calib, &g->sfx);
        g->screen = SCREEN_CALIB;
        sfx_play(&g->sfx, SFX_CONFIRM);
    }
}

static void leave_calib(Game *g) {
    calib_stop(&g->calib);
    preview_open(&g->menu.preview);   /* rouvre le périphérique du menu (bruitages, musique de fond) */
    g->screen = SCREEN_OPTIONS;
}

static void update_calib(Game *g, const Input *in, float dt) {
    calib_update(&g->calib, in, dt);
    if (in->pressed[BTN_CIRCLE]) { leave_calib(g); sfx_play(&g->sfx, SFX_CANCEL); return; }
    if (g->calib.phase == CALIB_RUNNING) return;
    if (in->pressed[BTN_TRIANGLE]) {
        calib_restart(&g->calib);
        sfx_play(&g->sfx, SFX_CONFIRM);
    } else if (in->pressed[BTN_CROSS] && g->calib.phase == CALIB_RESULT) {
        g->settings.audio_offset_ms = g->calib.result_ms;
        settings_save(&g->settings);
        leave_calib(g);
        sfx_play(&g->sfx, SFX_CONFIRM);
    }
}

void game_update(Game *g, const Input *in, float dt) {
    if (in->held[BTN_SELECT] && in->pressed[BTN_START]) { g->wants_quit = true; return; }
    g->time += dt;

    if (g->screen == SCREEN_TITLE) {
        title_update(&g->title, dt);
        if (in->pressed[BTN_CROSS] || in->pressed[BTN_START]) {
            g->screen = SCREEN_MAIN;
            g->main_sel = MAIN_BEATMAPS;
            g->main_cursor = 0.0f;
            sfx_play(&g->sfx, SFX_CONFIRM);
        }
        return;
    }
    if (g->screen == SCREEN_MAIN)    { update_main(g, in, dt); return; }
    if (g->screen == SCREEN_LIBRARY) { update_library(g, in, dt); return; }
    if (g->screen == SCREEN_OPTIONS) { update_options(g, in, dt); return; }
    if (g->screen == SCREEN_CALIB)   { update_calib(g, in, dt); return; }

    if (g->screen == SCREEN_MENU) {
        const MenuSel before = { g->menu.tab, g->menu.in_pack, g->menu.pack, g->menu.song, g->menu.diff,
                                 g->menu.confirm_delete };
        menu_update(&g->menu, in, dt);
        menu_sfx(g, &before);
        if (g->menu.back_requested) {
            g->menu.back_requested = false;
            g->screen = SCREEN_MAIN;
            sfx_play(&g->sfx, SFX_CANCEL);
            return;
        }
        const MenuEntry *e = menu_take_selection(&g->menu);
        if (e) {
            sfx_play(&g->sfx, SFX_START);
            start_song(g, e->path, (int)(e - g->menu.entries));
        }
        return;
    }
    update_play(g, in, dt);
}


/* Numéro du morceau joué dans la liste du menu (bandeaux « NIGHT nn »), 0 = carte lancée hors menu */
static int game_night(const Game *g) {
    return g->entry >= 0 && g->menu.song_count > 0 ? g->menu.song + 1 : 0;
}

/* Éclaboussures des écrans de transition (clip-path des maquettes, en fractions de la boîte) */
static const float LOAD_SPLASH[19][2] = {
    {0, .34f}, {.04f, .10f}, {.16f, .16f}, {.26f, 0}, {.40f, .12f}, {.54f, .02f}, {.68f, .14f}, {.82f, .03f}, {.95f, .18f},
    {1, .40f}, {.97f, .64f}, {1, .86f}, {.86f, 1}, {.70f, .88f}, {.55f, 1}, {.40f, .90f}, {.24f, 1}, {.10f, .88f}, {.02f, .70f},
};
static const float PAUSE_SPLASH[15][2] = {
    {0, .30f}, {.06f, .06f}, {.22f, .14f}, {.40f, 0}, {.58f, .12f}, {.76f, .02f}, {.94f, .16f}, {1, .44f}, {.96f, .72f},
    {1, .92f}, {.80f, 1}, {.60f, .90f}, {.40f, 1}, {.20f, .90f}, {.03f, .98f},
};

/* Texte espacé et tourné à partir de (x, cy = milieu des majuscules) */
static void draw_tracked(const Game *g, SDL_Renderer *r, FontFace face, float x, float cy, float scale, float tracking_px,
                         FontRot rot, SDL_Color c, const char *text) {
    rot.tracking = tracking_px / scale;
    font_draw_ink_rot(&g->font, r, face, x, cy - font_cap_height(&g->font, face) * scale * 0.5f, scale, rot, c, UI_NONE,
                      UI_NONE, 0, 0, text);
}

/* Chargement (écran fixe : le chargement bloque l'affichage) : bandeau NIGHT, titre sur son éclaboussure, artiste et
   difficulté, barre indicative. Image du morceau en couleurs d'origine sous un voile d'encre à gauche. */
static void draw_loading(const Game *g, SDL_Renderer *r) {
    SDL_SetRenderDrawColor(r, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, 255);
    SDL_RenderClear(r);
    background_draw_ex(&g->background, r, false);
    static const float VX[4] = { 0.0f, 0.40f, 0.62f, 1.0f }, VA[4] = { 0.92f, 0.92f, 0.5f, 0.1f };
    ui_veil_gradient(r, VX, VA, 4);

    /* avant la première étape la carte n'est pas lue : titre, artiste et difficulté viennent de la liste du menu */
    const MenuEntry *me = g->entry >= 0 && g->entry < g->menu.entry_count ? &g->menu.entries[g->entry] : NULL;
    const Chart *ch = &g->rhythm.chart;
    const char *song_title = g->song_loaded ? ch->title : me ? me->title : "";
    const char *song_artist = g->song_loaded ? ch->artist : me ? me->artist : "";
    const char *song_diff = g->song_loaded ? ch->difficulty : me ? me->difficulty : "";
    char up[96], num[16], total[16];
    int night = game_night(g);
    if (night > 0) {   /* « NIGHT 02 / 67 », penché */
        snprintf(num, sizeof num, "%02d", night);
        snprintf(total, sizeof total, " / %d", g->menu.song_count);
        const char *parts[3] = { "NIGHT ", num, total };
        const SDL_Color cols[3] = { CH_GOLD, CH_WHITE, CH_GOLD };
        const float sc = 1.0f, trk = 4.0f, h = 30.0f;
        float w = 24.0f;
        for (int k = 0; k < 3; k++) w += (float)font_width_tracked(&g->font, FONT_LABEL, parts[k], trk) + trk;
        const FontRot rot = { -6.0f, 50.0f + w * 0.5f, 92.0f + h * 0.5f, 0.0f, 0.0f };
        ui_fill_rect_rot(r, (SDL_FRect){50.0f, 92.0f, w, h}, rot, CH_INK);
        float x = 62.0f;
        for (int k = 0; k < 3; k++) {
            draw_tracked(g, r, FONT_LABEL, x, 92.0f + h * 0.5f, sc, trk, rot, cols[k], parts[k]);
            x += (float)font_width_tracked(&g->font, FONT_LABEL, parts[k], trk) + trk;
        }
    }

    /* titre citron cerclé d'encre sur une éclaboussure rose, penché de 6° */
    ui_upper(song_title, up, sizeof up);
    float tsc = 76.0f / 66.0f, tw = (float)font_width(&g->font, FONT_POP_XL, up) * tsc;
    if (tw > 560.0f) { tsc *= 560.0f / tw; tw = 560.0f; }
    const float tcy = 206.0f;
    const FontRot trot = { -6.0f, 40.0f, tcy, 0.0f, 0.0f };
    ui_splash_rect(r, (SDL_FRect){40.0f - 28.0f, tcy - 38.0f - 16.0f, tw + 68.0f, 76.0f + 36.0f}, trot, LOAD_SPLASH, 19, CH_PINK);
    font_draw_ink_rot(&g->font, r, FONT_POP_XL, 40.0f, tcy - font_cap_height(&g->font, FONT_POP_XL) * tsc * 0.5f, tsc, trot,
                      CH_GOLD, CH_INK, CH_INK, 6.0f, 6.0f, up);

    /* artiste et pastille de difficulté */
    const float my = 319.0f;
    const FontRot mrot = { -6.0f, 50.0f, my, 0.0f, 0.0f };
    float mx = 50.0f;
    if (song_artist[0]) {
        char fit[96];
        ui_upper(song_artist, up, sizeof up);
        ui_fit_text(&g->font, FONT_CARD_NAME, up, 420, fit, sizeof fit);
        const float asc = 26.0f / 28.0f;
        draw_tracked(g, r, FONT_CARD_NAME, mx, my, asc, 0.0f, mrot, CH_WHITE, fit);
        mx += (float)font_width(&g->font, FONT_CARD_NAME, fit) * asc + 12.0f;
    }
    if (song_diff[0]) {
        ui_upper(song_diff, up, sizeof up);
        float pw = (float)font_width(&g->font, FONT_CARD_BOLD, up) + 26.0f;
        ui_round_rect_rot(r, (SDL_FRect){mx, my - 13.0f, pw, 26.0f}, 13.0f, mrot, CH_CYAN);
        draw_tracked(g, r, FONT_CARD_BOLD, mx + 13.0f, my, 1.0f, 0.0f, mrot, CH_INK, up);
    }

    /* barre : les étapes réellement terminées ; sous la barre, celle qui est en cours (elle bloque l'affichage) */
    const float bx = 44.0f, bw = 380.0f, bh = 14.0f, by = SCREEN_H - 58.0f - bh, tan20 = 0.364f;
    const float p = (float)g->load_done / (float)g->load_total;
    ui_skew_box(r, (SDL_FRect){bx - 2.0f, by - 2.0f, bw + 4.0f, bh + 4.0f}, (bh + 4.0f) * 0.5f * tan20, ui_alpha(CH_WHITE, 89));
    ui_skew_box(r, (SDL_FRect){bx, by, bw, bh}, bh * 0.5f * tan20, CH_INK);
    if (p > 0.0f) ui_skew_box(r, (SDL_FRect){bx, by, bw * p, bh}, bh * 0.5f * tan20, CH_PINK);
    const FontRot flat = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    draw_tracked(g, r, FONT_POP, bx, by - 22.0f, 18.0f / 38.0f, 4.0f, flat, CH_WHITE, "LOADING");
    const char *step = g->load_done == 0 ? "READING THE SONG" : g->load_done == 1 ? "LOADING THE MUSIC" : "LOADING THE VIDEO";
    draw_tracked(g, r, FONT_LABEL, bx, by + bh + 18.0f, 1.0f, 2.0f, flat, ui_alpha(CH_WHITE, 200), step);
}

/* Choix en cascade (pause, menu principal, options) : le courant glisse vers la droite, grossit et passe en citron
   sur son éclaboussure rose, les autres en contour. cursor suit la sélection en glissant (fractionnaire). */
static void draw_cascade(const Game *g, SDL_Renderer *r, const char *const *labels, int n, float cursor, float y0, float step) {
    for (int i = 0; i < n; i++) {
        float k = 1.0f - fabsf((float)i - cursor);
        k = k < 0.0f ? 0.0f : k;
        const float cy = y0 + i * step, s = 1.0f + 0.12f * k, x = 60.0f + 22.0f * k;
        const float base = 40.0f / 66.0f, trk = 1.0f;
        float w = (float)font_width_tracked(&g->font, FONT_POP_XL, labels[i], trk / base) * base;
        const FontRot rot = { 0.0f, x, cy, trk / (base * s), 0.0f };
        if (k > 0.01f) {
            ui_splash_rect(r, (SDL_FRect){x - 26.0f * s, cy - 32.0f * s - 6.0f * s, (w + 56.0f) * s, (64.0f + 14.0f) * s}, rot,
                           PAUSE_SPLASH, 15, ui_alpha(CH_PINK, (Uint8)(255.0f * k)));
            font_draw_ink_rot(&g->font, r, FONT_POP_XL, x, cy - font_cap_height(&g->font, FONT_POP_XL) * base * s * 0.5f, base * s,
                              rot, ui_alpha(CH_GOLD, (Uint8)(255.0f * k)), ui_alpha(CH_INK, (Uint8)(255.0f * k)),
                              ui_alpha(CH_INK, (Uint8)(255.0f * k)), 5.0f, 5.0f, labels[i]);
        }
        if (k < 0.99f) {
            const float hs = 40.0f / 38.0f * s;
            FontRot hr = rot;
            hr.tracking = trk / hs;
            font_draw_hollow_rot(&g->font, r, FONT_POP, x, cy - font_cap_height(&g->font, FONT_POP) * hs * 0.5f, hs, hr,
                                 ui_alpha(CH_WHITE, (Uint8)(255.0f * (1.0f - k))), labels[i]);
        }
    }
}

/* Pause, par-dessus le jeu figé et assombri : « PAUSE » citron, choix en cascade (le choix courant en citron sur
   une éclaboussure rose, les autres en contour), calibration audio (L / R, ±10 ms), aides */
static void draw_pause(const Game *g, SDL_Renderer *r) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, 199);
    SDL_RenderFillRect(r, NULL);

    const float hsc = 64.0f / 66.0f, hw = (float)font_width(&g->font, FONT_POP_XL, "PAUSE") * hsc;
    ui_pop_text(r, &g->font, FONT_POP_XL, 40.0f + hw * 0.5f, 81.0f, hsc, 0.0f, -4.0f, 0.0f, CH_GOLD, CH_INK, CH_PINK, 5.0f, 5.0f,
                "PAUSE");
    const Chart *c = &g->rhythm.chart;
    char line[128], fit[128];
    if (c->difficulty[0]) snprintf(line, sizeof line, "%s  \xC2\xB7  %s", c->title, c->difficulty);
    else                  snprintf(line, sizeof line, "%s", c->title);
    ui_fit_text(&g->font, FONT_CARD, line, 520, fit, sizeof fit);
    const FontRot srot = { -4.0f, 46.0f, 139.0f, 0.0f, 0.0f };
    draw_tracked(g, r, FONT_CARD, 46.0f, 139.0f, 1.0f, 0.0f, srot, ui_alpha(CH_WHITE, 204), fit);

    draw_cascade(g, r, PAUSE_LABELS, PAUSE_OPTIONS, g->pause_cursor, 202.0f, 78.0f);

    /* calibration audio : [L −10 MS] [+20 MS] [+10 MS R] dans une pilule d'encre cerclée de cyan */
    const FontRot flat = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    draw_tracked(g, r, FONT_LABEL, 60.0f, 414.0f, 12.0f / 14.0f, 4.0f, flat, ui_alpha(CH_WHITE, 178), "AUDIO OFFSET");
    const float ksc = 11.0f / 14.0f, ry = 429.0f, rh = 40.0f, rcy = ry + rh * 0.5f;
    float kw = (float)font_width_tracked(&g->font, FONT_LABEL, "-10 MS", 1.0f / ksc) * ksc;
    const float key_w = 6.0f + 24.0f + 6.0f + kw + 6.0f, val_w = 104.0f, total_w = 6.0f + key_w * 2.0f + val_w + 6.0f;
    ui_round_rect_rot(r, (SDL_FRect){58.0f, ry - 2.0f, total_w + 4.0f, rh + 4.0f}, 22.0f, flat, ui_alpha(CH_CYAN, 115));
    ui_round_rect_rot(r, (SDL_FRect){60.0f, ry, total_w, rh}, 20.0f, flat, CH_INK);
    float x = 66.0f;
    /* touche L */
    ui_round_rect_rot(r, (SDL_FRect){x + 6.0f, rcy - 12.0f, 24.0f, 24.0f}, 6.0f, flat, CH_WHITE);
    font_draw(&g->font, r, FONT_LABEL, x + 18.0f, rcy - font_cap_height(&g->font, FONT_LABEL) * 0.5f, ALIGN_CENTER, CH_INK, "L");
    draw_tracked(g, r, FONT_LABEL, x + 36.0f, rcy, ksc, 1.0f, flat, ui_alpha(CH_WHITE, 153), "-10 MS");
    x += key_w;
    /* valeur, entre deux filets */
    SDL_SetRenderDrawColor(r, 255, 255, 255, 38);
    SDL_RenderDrawLine(r, (int)x, (int)(ry + 8.0f), (int)x, (int)(ry + rh - 8.0f));
    SDL_RenderDrawLine(r, (int)(x + val_w), (int)(ry + 8.0f), (int)(x + val_w), (int)(ry + rh - 8.0f));
    char calib[24];
    snprintf(calib, sizeof calib, "%+d MS", g->audio.calib_ms);
    ui_pop_text(r, &g->font, FONT_POP, x + val_w * 0.5f, rcy, 26.0f / 38.0f, 1.0f, 0.0f, 0.0f, CH_CYAN, UI_NONE, UI_NONE, 0, 0,
                calib);
    x += val_w;
    /* touche R */
    draw_tracked(g, r, FONT_LABEL, x + 6.0f, rcy, ksc, 1.0f, flat, ui_alpha(CH_WHITE, 153), "+10 MS");
    ui_round_rect_rot(r, (SDL_FRect){x + 12.0f + kw, rcy - 12.0f, 24.0f, 24.0f}, 6.0f, flat, CH_WHITE);
    font_draw(&g->font, r, FONT_LABEL, x + 24.0f + kw, rcy - font_cap_height(&g->font, FONT_LABEL) * 0.5f, ALIGN_CENTER, CH_INK, "R");

    const float hy = SCREEN_H - 26.0f;
    float hx = ui_button_hint_pop(r, &g->font, &g->sprites, 34.0f, hy, (const Button[]){BTN_UP, BTN_DOWN}, 2, "CHOOSE") + 18.0f;
    hx = ui_button_hint_pop(r, &g->font, &g->sprites, hx, hy, (const Button[]){BTN_CROSS}, 1, "OK") + 18.0f;
    ui_button_hint_pop(r, &g->font, &g->sprites, hx, hy, (const Button[]){BTN_START}, 1, "RESUME");
    const Button quit[] = { BTN_SELECT, BTN_START };
    float qw = ui_button_hint_pop_width(&g->font, quit, 2, "QUIT GAME");
    ui_button_hint_pop(r, &g->font, &g->sprites, SCREEN_W - 34.0f - qw, hy, quit, 2, "QUIT GAME");
}

/* Fond du menu principal et des options : la scène du titre (sans l'invitation) sous un voile de nuit, plus dense à
   gauche, où se pose la cascade */
static void draw_menu_backdrop(const Game *g, SDL_Renderer *r) {
    title_draw(&g->title, r, &g->font, &g->sprites, g->time, false);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, 120);
    SDL_RenderFillRect(r, NULL);
    ui_veil_left(r);
}

static void draw_header(const Game *g, SDL_Renderer *r, const char *text) {
    const float hsc = 64.0f / 66.0f, hw = (float)font_width(&g->font, FONT_POP_XL, text) * hsc;
    ui_pop_text(r, &g->font, FONT_POP_XL, 40.0f + hw * 0.5f, 81.0f, hsc, 0.0f, -4.0f, 0.0f, CH_GOLD, CH_INK, CH_PINK, 5.0f, 5.0f,
                text);
}

/* Copie `text` dans `out`, coupée par « ... » pour tenir dans maxw px (sans casser un caractère UTF-8) */
static void fit_text(const Font *f, FontFace face, const char *text, int maxw, char *out, size_t cap) {
    snprintf(out, cap, "%s", text);
    size_t n = strlen(out);
    while (n > 4 && font_width(f, face, out) > maxw) {
        n--;
        while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80) n--;
        out[n] = '\0';
        if (n > 3) out[n - 1] = out[n - 2] = out[n - 3] = '.';
    }
}

/* Cartouche « nouvelle version » du menu principal (annonce seulement : la mise à jour se fait par VitaDB ou la page de
   téléchargement). Les nouveautés sont tronquées pour tenir sur une ligne. */
static void draw_update_banner(const Game *g, SDL_Renderer *r) {
    if (!g->settings.update_check || update_state(g->update) != UPDATE_AVAILABLE) return;
    const FontRot flat = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    const SDL_FRect box = { SCREEN_W - 452.0f, 352.0f, 412.0f, 104.0f };
    ui_round_rect_rot(r, (SDL_FRect){ box.x - 2.0f, box.y - 2.0f, box.w + 4.0f, box.h + 4.0f }, 16.0f, flat, CH_GOLD);
    ui_round_rect_rot(r, box, 14.0f, flat, CH_INK);
    char head[64];
    snprintf(head, sizeof head, "NEW VERSION  %s", g->update->version);
    font_draw(&g->font, r, FONT_CARD_NAME, box.x + 18.0f, box.y + 12.0f, ALIGN_LEFT, CH_GOLD, head);
    const char *where = g->update->url[0] ? g->update->url : "dancing-every-night.pages.dev";
    if (!strncmp(where, "https://", 8)) where += 8;
    char line[sizeof g->update->notes];
    fit_text(&g->font, FONT_CARD, g->update->notes, (int)box.w - 36, line, sizeof line);
    font_draw(&g->font, r, FONT_CARD, box.x + 18.0f, box.y + 48.0f, ALIGN_LEFT, CH_WHITE, line);
    fit_text(&g->font, FONT_CARD, where, (int)box.w - 36, line, sizeof line);
    font_draw(&g->font, r, FONT_CARD, box.x + 18.0f, box.y + 74.0f, ALIGN_LEFT, CH_CYAN, line);
}

static void draw_main(const Game *g, SDL_Renderer *r) {
    draw_menu_backdrop(g, r);
    draw_header(g, r, "MAIN MENU");
    draw_cascade(g, r, MAIN_LABELS, MAIN_COUNT, g->main_cursor, 202.0f, 78.0f);
    draw_update_banner(g, r);
    const float hy = SCREEN_H - 26.0f;
    float hx = ui_button_hint_pop(r, &g->font, &g->sprites, 34.0f, hy, (const Button[]){BTN_UP, BTN_DOWN}, 2, "CHOOSE") + 18.0f;
    hx = ui_button_hint_pop(r, &g->font, &g->sprites, hx, hy, (const Button[]){BTN_CROSS}, 1, "OK") + 18.0f;
    ui_button_hint_pop(r, &g->font, &g->sprites, hx, hy, (const Button[]){BTN_CIRCLE}, 1, "BACK");
}

static void draw_library(const Game *g, SDL_Renderer *r) {
    draw_menu_backdrop(g, r);
    draw_header(g, r, "LIBRARY");
    library_draw(g->library, r, &g->font, &g->sprites);
}

static void draw_options(const Game *g, SDL_Renderer *r) {
    draw_menu_backdrop(g, r);
    draw_header(g, r, "OPTIONS");
    const float y0 = 202.0f, step = 78.0f;
    draw_cascade(g, r, OPT_LABELS, OPT_COUNT, g->opt_cursor, y0, step);

    /* valeur du décalage, dans une pilule d'encre cerclée de cyan à droite de son libellé */
    char val[24];
    snprintf(val, sizeof val, "%+d MS", g->settings.audio_offset_ms);
    const float px = 470.0f, pw = 150.0f, ph = 46.0f, pcy = y0;
    const FontRot flat = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    ui_round_rect_rot(r, (SDL_FRect){px - 2.0f, pcy - ph * 0.5f - 2.0f, pw + 4.0f, ph + 4.0f}, ph * 0.5f + 2.0f, flat,
                      ui_alpha(CH_CYAN, g->opt_sel == OPT_OFFSET ? 230 : 110));
    ui_round_rect_rot(r, (SDL_FRect){px, pcy - ph * 0.5f, pw, ph}, ph * 0.5f, flat, CH_INK);
    ui_pop_text(r, &g->font, FONT_POP, px + pw * 0.5f, pcy, 28.0f / 38.0f, 1.0f, 0.0f, 0.0f, CH_CYAN, UI_NONE, UI_NONE, 0, 0, val);
    if (g->opt_sel == OPT_OFFSET) {   /* flèches de réglage */
        shapes_filled_triangle(r, px - 18.0f, pcy, 9.0f, -1.5707963f, CH_WHITE);
        shapes_filled_triangle(r, px + pw + 18.0f, pcy, 9.0f, 1.5707963f, CH_WHITE);
    }
    {   /* vérification des mises à jour : pastille ON / OFF sur sa ligne */
        const float ucy = y0 + 2.0f * step, uw = 110.0f;
        ui_round_rect_rot(r, (SDL_FRect){px - 2.0f, ucy - ph * 0.5f - 2.0f, uw + 4.0f, ph + 4.0f}, ph * 0.5f + 2.0f, flat,
                          ui_alpha(CH_CYAN, g->opt_sel == OPT_UPDATES ? 230 : 110));
        ui_round_rect_rot(r, (SDL_FRect){px, ucy - ph * 0.5f, uw, ph}, ph * 0.5f, flat, CH_INK);
        ui_pop_text(r, &g->font, FONT_POP, px + uw * 0.5f, ucy, 28.0f / 38.0f, 1.0f, 0.0f, 0.0f,
                    g->settings.update_check ? CH_CYAN : ui_alpha(CH_WHITE, 150), UI_NONE, UI_NONE, 0, 0,
                    g->settings.update_check ? "ON" : "OFF");
    }
    const char *help = g->opt_sel == OPT_OFFSET ? "HITS JUDGED LATE? LOWER IT  \xC2\xB7  JUDGED EARLY? RAISE IT"
                     : g->opt_sel == OPT_UPDATES ? "ON: ASKS THE WEBSITE AT START IF A NEW VERSION EXISTS. NOTHING ELSE IS SENT."
                                                 : "TAP ALONG A CLICK TO MEASURE YOUR OFFSET";
    font_draw(&g->font, r, FONT_CARD, 62.0f, 438.0f, ALIGN_LEFT, ui_alpha(CH_WHITE, 204), help);

    const float hy = SCREEN_H - 26.0f;
    float hx = ui_button_hint_pop(r, &g->font, &g->sprites, 34.0f, hy, (const Button[]){BTN_UP, BTN_DOWN}, 2, "CHOOSE") + 18.0f;
    if (g->opt_sel == OPT_OFFSET) {
        hx = ui_button_hint_pop(r, &g->font, &g->sprites, hx, hy, (const Button[]){BTN_LEFT, BTN_RIGHT}, 2, "1 MS") + 18.0f;
        hx = ui_button_hint_pop(r, &g->font, &g->sprites, hx, hy, (const Button[]){BTN_L, BTN_R}, 2, "10 MS") + 18.0f;
    } else {
        hx = ui_button_hint_pop(r, &g->font, &g->sprites, hx, hy, (const Button[]){BTN_CROSS}, 1,
                                g->opt_sel == OPT_UPDATES ? "SWITCH" : "START") + 18.0f;
    }
    ui_button_hint_pop(r, &g->font, &g->sprites, hx, hy, (const Button[]){BTN_CIRCLE}, 1, "BACK");
}

/* Compte à rebours de reprise : chiffre citron en relief qui tombe à chaque seconde dans un anneau qui se vide ;
   le jeu reste visible derrière */
static void draw_countdown(const Game *g, SDL_Renderer *r) {
    const float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
    int n = (int)((g->countdown_ms + 999.0f) / 1000.0f);
    float frac = fmodf(g->countdown_ms, 1000.0f) / 1000.0f;   /* 1 -> 0 pendant chaque seconde */
    if (frac <= 0.0f) frac = 1.0f;
    float since = 1.0f - frac;
    float pop = since < 0.18f ? 1.0f - since / 0.18f : 0.0f;

    shapes_circle_outline(r, cx, cy, 122.0f, 16.0f, CH_INK);
    shapes_arc_outline(r, cx, cy, 122.0f, 122.0f, 9.0f, 0.0f, 360.0f * frac, CH_PINK);
    char buf[8];
    snprintf(buf, sizeof buf, "%d", n);
    const float s = 170.0f / 148.0f * (1.0f + 0.5f * pop * pop);
    ui_pop_text(r, &g->font, FONT_POP_HUGE, cx, cy, s, 0.0f, 0.0f, 0.0f, UI_NONE, UI_NONE, CH_CYAN, 13.0f, 13.0f, buf);
    ui_pop_text(r, &g->font, FONT_POP_HUGE, cx, cy, s, 0.0f, 0.0f, 0.0f, CH_GOLD, CH_INK, CH_PINK, 7.0f, 7.0f, buf);

    const float lsc = 16.0f / 14.0f, trk = 6.0f;
    float lw = ((float)font_width_tracked(&g->font, FONT_LABEL, "GET READY", trk / lsc) + trk / lsc) * lsc;
    const FontRot flat = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    ui_round_rect_rot(r, (SDL_FRect){cx - lw * 0.5f - 16.0f, 410.0f, lw + 32.0f, 30.0f}, 15.0f, flat, CH_INK);
    draw_tracked(g, r, FONT_LABEL, cx - lw * 0.5f, 425.0f, lsc, trk, flat, CH_WHITE, "GET READY");
}

void game_draw(const Game *g, SDL_Renderer *r) {
    if (g->screen == SCREEN_TITLE) {
        title_draw(&g->title, r, &g->font, &g->sprites, g->time, true);
        return;
    }
    if (g->screen == SCREEN_MAIN)    { draw_main(g, r); return; }
    if (g->screen == SCREEN_LIBRARY) { draw_library(g, r); return; }
    if (g->screen == SCREEN_OPTIONS) { draw_options(g, r); return; }
    if (g->screen == SCREEN_CALIB) {
        calib_draw(&g->calib, r, &g->font, &g->sprites, g->settings.audio_offset_ms);
        return;
    }
    if (g->screen == SCREEN_PLAY && g->loading > 0) {
        draw_loading(g, r);
        return;
    }
    if (g->screen == SCREEN_MENU) {
        menu_draw(&g->menu, r, &g->font, &g->sprites);
        return;
    }

    rhythm_draw(&g->rhythm, r, &g->font, &g->sprites, &g->background,
                g->entry >= 0 ? g->menu.entries[g->entry].best_score : -1, g->new_best, game_night(g));
    if (g->paused) draw_pause(g, r);
    else if (g->countdown_ms > 0.0f) draw_countdown(g, r);
}
