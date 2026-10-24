#include "settings.h"
#include "config.h"
#include "files.h"
#include <stdio.h>


#define SETTINGS_FILE SAVE_DIR "settings.txt"

int settings_clamp_offset(int ms) {
    return ms < SETTINGS_OFFSET_MIN ? SETTINGS_OFFSET_MIN : ms > SETTINGS_OFFSET_MAX ? SETTINGS_OFFSET_MAX : ms;
}

#ifdef LEGACY_SAVE_DIR
static void copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return;
    FILE *out = fopen(dst, "wb");
    if (out) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        fclose(out);
    }
    fclose(in);
}
#endif

void settings_migrate_legacy(void) {
#ifdef LEGACY_SAVE_DIR
    static const char *const FILES[] = { "settings.txt", "scores_v2.txt" };
    for (size_t i = 0; i < sizeof FILES / sizeof FILES[0]; i++) {
        char src[128], dst[128];
        snprintf(src, sizeof src, "%s%s", LEGACY_SAVE_DIR, FILES[i]);
        snprintf(dst, sizeof dst, "%s%s", SAVE_DIR, FILES[i]);
        FILE *f = fopen(dst, "r");
        if (f) { fclose(f); continue; }   /* déjà là : on n'écrase jamais */
        files_mkdir(SAVE_DIR);
        copy_file(src, dst);
    }
#endif
}

void settings_load(Settings *s) {
    *s = (Settings){ .audio_offset_ms = 0 };
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    char line[128];
    int v;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "AUDIO_OFFSET_MS %d", &v) == 1) s->audio_offset_ms = settings_clamp_offset(v);
    fclose(f);
}

void settings_save(const Settings *s) {
    files_mkdir(SAVE_DIR);
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) { fprintf(stderr, "settings: impossible d'écrire %s\n", SETTINGS_FILE); return; }
    fprintf(f, "AUDIO_OFFSET_MS %d\n", s->audio_offset_ms);
    fclose(f);
}
