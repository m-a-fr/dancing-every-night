#include "chart.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define INITIAL_CAPACITY 64
#define PREVIEW_MAX_SECTIONS   48
#define PREVIEW_MIN_FRACTION   0.30f   /* le refrain cherché commence après 30 % du morceau (pas l'intro) */
#define PREVIEW_MAX_FRACTION   0.70f   /* et de préférence avant 70 % (premier refrain plutôt que le final) */
#define PREVIEW_FALLBACK       0.52f   /* sans sections : refrain d'un OP TV, mesuré vers 50-55 % */

/* Identifiant de map : lettres, chiffres, '_' et '-' seulement (c'est aussi un nom de dossier) */
static bool valid_id(const char *id) {
    if (!id[0]) return false;
    for (const char *c = id; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
            return false;
    return true;
}

/* Chemin complet d'un média. Format 1 : simple nom de fichier du dossier de la carte (pas de '/', ni de ':' ni
   de nom caché) : une map ne peut pas lire hors de son dossier. Format 0 : relatif à assets/. */
static void resolve_media(char *out, size_t size, const char *name, const char *dir, int format,
                          const char *path, const char *key) {
    out[0] = '\0';
    if (!name[0]) return;
    if (format >= 1) {
        if (name[0] == '.' || strpbrk(name, "/\\:")) {
            fprintf(stderr, "chart '%s' : %s '%s' refusé (nom de fichier du dossier de la map attendu)\n", path, key, name);
            return;
        }
        snprintf(out, size, "%s%s", dir, name);
    } else {
        snprintf(out, size, "%s%s", ASSET_DIR, name);
    }
}

/* Arc gauche = croix directionnelle, arc droit = boutons face */
static bool lane_is_left(Button b) {
    return b == BTN_UP || b == BTN_LEFT || b == BTN_DOWN;
}

static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
}

/* Temps de trajet par défaut : mesuré sur le jeu de référence (≈ 1,5 s en EASY, NORMAL et HARD : la vitesse
   ne dépend pas de la difficulté). Le paramètre difficulty reste pour pouvoir différencier plus tard. */
int chart_default_approach_ms(const char *difficulty) {
    (void)difficulty;
    return 1500;
}

/* Énergie d'une section annotée par tools/mapper.py ("# --- Partie G (intense) ---") : 2 intense, 1 forte, 0 autre */
static int section_level(const char *comment) {
    if (!strstr(comment, "---")) return -1;
    if (strstr(comment, "(intense)")) return 2;
    if (strstr(comment, "(forte)")) return 1;
    return 0;
}

/* Début de l'aperçu du menu (ms dans la carte) : PREVIEW explicite, sinon la première section la plus énergique
   après l'intro (le refrain, d'après les annotations du mapper), sinon une fraction du morceau calée sur un temps */
static int pick_preview(float preview_beat, const int *sec_ms, const int *sec_lvl, int sections,
                        const Note *notes, int count, float bpm) {
    float ms_per_beat = 60000.0f / bpm;
    if (preview_beat >= 0.0f) return (int)lroundf(preview_beat * ms_per_beat);
    int last = 0;
    for (int i = 0; i < count; i++) if (notes[i].time_ms > last) last = notes[i].time_ms;
    int min_ms = (int)(last * PREVIEW_MIN_FRACTION), max_ms = (int)(last * PREVIEW_MAX_FRACTION);
    for (int pass = 0; pass < 2; pass++, max_ms = last)   /* d'abord dans la fenêtre préférée, puis jusqu'à la fin */
        for (int lvl = 2; lvl >= 1; lvl--)
            for (int i = 0; i < sections; i++)
                if (sec_lvl[i] == lvl && sec_ms[i] >= min_ms && sec_ms[i] <= max_ms) return sec_ms[i];
    return (int)(floorf(last * PREVIEW_FALLBACK / ms_per_beat) * ms_per_beat);
}

static bool lane_from_name(const char *name, Button *out) {
    static const struct { const char *name; Button btn; } TABLE[] = {
        {"TRIANGLE", BTN_TRIANGLE}, {"CIRCLE", BTN_CIRCLE}, {"CROSS", BTN_CROSS},
        {"UP", BTN_UP}, {"LEFT", BTN_LEFT}, {"DOWN", BTN_DOWN},
    };
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) {
        if (strcmp(name, TABLE[i].name) == 0) { *out = TABLE[i].btn; return true; }
    }
    return false;
}

bool chart_load(Chart *chart, const char *path) {
    *chart = (Chart){0};

    FILE *f = fopen(path, "r");
    if (!f) return false;

    int capacity = INITIAL_CAPACITY;
    Note *notes = malloc(sizeof(Note) * (size_t)capacity);
    if (!notes) { fclose(f); return false; }

    float bpm = 0.0f;
    int offset_ms = 0, count = 0;
    char audio_path[128] = "", title[64] = "", artist[64] = "", pack[64] = "", difficulty[24] = "";
    char background_path[128] = "", video_path[128] = "";
    char id[CHART_ID_MAX] = "", mapper[64] = "", source[64] = "", license[64] = "";
    int format = 0, version = 1;
    int bad_lines = 0;   /* lignes ignorées (format inattendu, lane inconnue...) : signalées par le validateur */
    int video_offset_ms = 0, background_dim = 30, approach_ms = 0, order = 0;
    char line[128];
    int line_no = 0;
    float preview_beat = -1.0f, end_beat = -1.0f;
    int sec_ms[PREVIEW_MAX_SECTIONS], sec_lvl[PREVIEW_MAX_SECTIONS], sections = 0, pending_level = -1;

    while (fgets(line, sizeof line, f)) {
        line_no++;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#') {   /* commentaire ; une section annotée prend le temps de sa première note */
            int lvl = section_level(s);
            if (lvl >= 0) pending_level = lvl;
            continue;
        }
        if (*s == '\0' || *s == '\n') continue;

        if (sscanf(s, "FORMAT %d", &format) == 1) continue;
        if (sscanf(s, "ID %47s", id) == 1) continue;
        if (sscanf(s, "VERSION %d", &version) == 1) continue;
        if (sscanf(s, "MAPPER %63[^\n]", mapper) == 1) { rtrim(mapper); continue; }
        if (sscanf(s, "SOURCE %63[^\n]", source) == 1) { rtrim(source); continue; }
        if (sscanf(s, "LICENSE %63[^\n]", license) == 1) { rtrim(license); continue; }
        if (sscanf(s, "BPM %f", &bpm) == 1) continue;
        if (sscanf(s, "OFFSET_MS %d", &offset_ms) == 1) continue;
        if (sscanf(s, "AUDIO %127s", audio_path) == 1) continue;
        if (sscanf(s, "APPROACH_MS %d", &approach_ms) == 1) continue;
        if (sscanf(s, "BACKGROUND_DIM %d", &background_dim) == 1) continue;
        if (sscanf(s, "BACKGROUND %127s", background_path) == 1) continue;
        if (sscanf(s, "VIDEO_OFFSET_MS %d", &video_offset_ms) == 1) continue;
        if (strncmp(s, "VIDEO_FPS", 9) == 0) continue; /* ancien format .mjpg : la cadence est dans le .mpg */
        if (sscanf(s, "VIDEO %127s", video_path) == 1) continue;
        if (sscanf(s, "TITLE %63[^\n]", title) == 1) { rtrim(title); continue; }
        if (sscanf(s, "ARTIST %63[^\n]", artist) == 1) { rtrim(artist); continue; }
        if (sscanf(s, "PACK %63[^\n]", pack) == 1) { rtrim(pack); continue; }
        if (sscanf(s, "DIFFICULTY %23[^\n]", difficulty) == 1) { rtrim(difficulty); continue; }
        if (sscanf(s, "ORDER %d", &order) == 1) continue;
        if (sscanf(s, "PREVIEW %f", &preview_beat) == 1) continue;
        if (sscanf(s, "END %f", &end_beat) == 1) continue;

        /* Temps et durée en beats (voir GAME_DESIGN.md) ; BPM doit être défini avant */
        char lane_name[32], type_name[32];
        float beat = 0.0f, duration_beats = 0.0f;
        int n = sscanf(s, "%f %31s %31s %f", &beat, lane_name, type_name, &duration_beats);
        if (n < 3) {
            bad_lines++, fprintf(stderr, "chart '%s' ligne %d : ignorée (format inattendu)\n", path, line_no);
            continue;
        }

        if (bpm <= 0.0f) {
            bad_lines++, fprintf(stderr, "chart '%s' ligne %d : note avant la ligne BPM, ignorée\n", path, line_no);
            continue;
        }
        if (pending_level >= 0 && sections < PREVIEW_MAX_SECTIONS) {
            sec_ms[sections] = (int)lroundf(beat * (60000.0f / bpm));
            sec_lvl[sections++] = pending_level;
        }
        pending_level = -1;

        /* SCRATCH n'est pas une vraie lane : "<temps> SCRATCH CW|CCW" (pas de bouton/direction associé,
           un flick de stick à la place, cf. GAME_DESIGN.md). */
        if (strcmp(lane_name, "SCRATCH") == 0) {
            bool ccw;
            if (strcmp(type_name, "CW") == 0) ccw = false;
            else if (strcmp(type_name, "CCW") == 0) ccw = true;
            else {
                bad_lines++, fprintf(stderr, "chart '%s' ligne %d : SCRATCH attend CW ou CCW (reçu '%s')\n", path, line_no, type_name);
                continue;
            }
            if (count == capacity) {
                capacity *= 2;
                Note *grown = realloc(notes, sizeof(Note) * (size_t)capacity);
                if (!grown) { free(notes); fclose(f); return false; }
                notes = grown;
            }
            notes[count++] = (Note){
                .time_ms = (int)lroundf(beat * (60000.0f / bpm)), .lane = BTN_CROSS, .lane2 = BTN_CROSS,
                .type = NOTE_SCRATCH, .scratch_ccw = ccw
            };
            continue;
        }

        Button lane;
        if (!lane_from_name(lane_name, &lane)) {
            bad_lines++, fprintf(stderr, "chart '%s' ligne %d : lane '%s' inconnue\n", path, line_no, lane_name);
            continue;
        }

        NoteType type;
        Button lane2 = lane;
        if (strcmp(type_name, "DOUBLE") == 0) {
            char lane2_name[32];
            if (sscanf(s, "%f %31s %31s %31s", &beat, lane_name, type_name, lane2_name) != 4
                || !lane_from_name(lane2_name, &lane2)) {
                bad_lines++, fprintf(stderr, "chart '%s' ligne %d : DOUBLE attend deux lanes valides\n", path, line_no);
                continue;
            }
            if (lane_is_left(lane) == lane_is_left(lane2)) {
                bad_lines++, fprintf(stderr, "chart '%s' ligne %d : DOUBLE %s %s refusée (il faut une lane de chaque arc)\n",
                        path, line_no, lane_name, lane2_name);
                continue;
            }
            type = NOTE_DOUBLE;
            duration_beats = 0.0f;
        } else if (strcmp(type_name, "TAP") == 0) {
            type = NOTE_TAP;
            duration_beats = 0.0f;
        } else if (strcmp(type_name, "HOLD") == 0) {
            if (n < 4) {
                bad_lines++, fprintf(stderr, "chart '%s' ligne %d : HOLD sans durée\n", path, line_no);
                continue;
            }
            type = NOTE_HOLD;
        } else {
            bad_lines++, fprintf(stderr, "chart '%s' ligne %d : type '%s' inconnu\n", path, line_no, type_name);
            continue;
        }

        if (count == capacity) {
            capacity *= 2;
            Note *grown = realloc(notes, sizeof(Note) * (size_t)capacity);
            if (!grown) { free(notes); fclose(f); return false; }
            notes = grown;
        }
        float ms_per_beat = 60000.0f / bpm;
        notes[count++] = (Note){
            .time_ms = (int)lroundf(beat * ms_per_beat), .lane = lane, .lane2 = lane2, .type = type,
            .duration_ms = (int)lroundf(duration_beats * ms_per_beat)
        };
    }

    fclose(f);

    /* Dossier de la carte (avec le '/' final) : base des médias en format 1, et ID par défaut */
    char dir[CHART_PATH] = "";
    const char *slash = strrchr(path, '/');
    if (slash && (size_t)(slash - path + 1) < sizeof dir) {
        memcpy(dir, path, (size_t)(slash - path + 1));
        dir[slash - path + 1] = '\0';
    }
    if (format >= 1 && !valid_id(id)) {
        if (id[0]) fprintf(stderr, "chart '%s' : ID '%s' invalide, nom du dossier utilisé\n", path, id);
        id[0] = '\0';
        if (slash) {
            const char *start = slash;
            while (start > path && start[-1] != '/') start--;
            size_t n = (size_t)(slash - start);
            if (n < sizeof id) { memcpy(id, start, n); id[n] = '\0'; }
        }
        if (!valid_id(id)) id[0] = '\0';
    }

    chart->format = format;
    chart->bad_lines = bad_lines;
    snprintf(chart->id, sizeof chart->id, "%s", format >= 1 ? id : "");
    chart->version = version > 0 ? version : 1;
    snprintf(chart->mapper, sizeof chart->mapper, "%s", mapper);
    snprintf(chart->source, sizeof chart->source, "%s", source);
    snprintf(chart->license, sizeof chart->license, "%s", license);
    chart->bpm = bpm > 0.0f ? bpm : 120.0f;
    chart->offset_ms = offset_ms;
    snprintf(chart->title, sizeof chart->title, "%s", title);
    snprintf(chart->artist, sizeof chart->artist, "%s", artist);
    snprintf(chart->pack, sizeof chart->pack, "%s", pack);
    snprintf(chart->difficulty, sizeof chart->difficulty, "%s", difficulty);
    chart->order = order > 0 ? order : 0;
    resolve_media(chart->background_path, sizeof chart->background_path, background_path, dir, format, path, "BACKGROUND");
    resolve_media(chart->video_path, sizeof chart->video_path, video_path, dir, format, path, "VIDEO");
    chart->video_offset_ms = video_offset_ms;
    chart->approach_ms = approach_ms > 0 ? (approach_ms < 500 ? 500 : approach_ms > 5000 ? 5000 : approach_ms)
                                          : chart_default_approach_ms(difficulty);
    chart->background_dim = background_dim < 0 ? 0 : background_dim > 100 ? 100 : background_dim;
    resolve_media(chart->audio_path, sizeof chart->audio_path, audio_path, dir, format, path, "AUDIO");
    chart->notes = notes;
    chart->note_count = count;
    chart->end_ms = end_beat > 0.0f ? offset_ms + (int)lroundf(end_beat * (60000.0f / chart->bpm)) : -1;
    chart->preview_ms = offset_ms + pick_preview(preview_beat, sec_ms, sec_lvl, sections, notes, count, chart->bpm);
    return true;
}

void chart_free(Chart *chart) {
    free(chart->notes);
    chart->notes = NULL;
    chart->note_count = 0;
}
