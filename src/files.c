#include "files.h"
#include "config.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#ifdef __vita__
#  include <psp2/io/fcntl.h>
#  include <psp2/io/stat.h>
#else
#  include "sdl.h"
#  include "config.h"
#  include <stdlib.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif
#ifdef _WIN32
#  include <direct.h>
#  define mkdir(p, mode) _mkdir(p)   /* Windows : pas de droits d'accès */
#endif

/* Copie de `path` sans '/' final (sceIoRmdir / rmdir n'en veulent pas) */
static void strip_slash(char *out, size_t size, const char *path) {
    snprintf(out, size, "%s", path);
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/') out[--n] = '\0';
}

bool files_is_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return false;
    closedir(d);
    return true;
}

bool files_mkdir(const char *dir) {
    char p[256];
    strip_slash(p, sizeof p, dir);
#ifdef __vita__
    sceIoMkdir(p, 0777);
#else
    mkdir(p, 0777);
#endif
    return files_is_dir(p);
}

bool files_remove(const char *file) {
#ifdef __vita__
    return sceIoRemove(file) >= 0;
#else
    return unlink(file) == 0;
#endif
}

bool files_rename(const char *from, const char *to) {
    char a[256], b[256];
    strip_slash(a, sizeof a, from);
    strip_slash(b, sizeof b, to);
#ifdef __vita__
    return sceIoRename(a, b) >= 0;
#else
    return rename(a, b) == 0;
#endif
}

bool files_stat(const char *path, long long *size, long long *mtime) {
#ifdef __vita__
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return false;
    const SceDateTime *t = &st.st_mtime;
    *size = (long long)st.st_size;
    *mtime = ((((((long long)t->year * 13 + t->month) * 32 + t->day) * 24 + t->hour) * 60 + t->minute) * 60 + t->second)
             * 1000000LL + t->microsecond;
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *size = (long long)st.st_size;
    *mtime = (long long)st.st_mtime;
#endif
    return true;
}

static bool remove_dir(const char *dir) {
    char p[256];
    strip_slash(p, sizeof p, dir);
#ifdef __vita__
    return sceIoRmdir(p) >= 0;
#else
    return rmdir(p) == 0;
#endif
}

bool files_mkdtemp(char *tmpl) {
#if defined(__vita__)
    (void)tmpl;
    return false;
#elif defined(_WIN32)
    size_t n = strlen(tmpl);
    if (n < 6) return false;
    for (int attempt = 0; attempt < 100; attempt++) {
        unsigned v = (unsigned)SDL_GetPerformanceCounter() ^ ((unsigned)rand() << 8) ^ (unsigned)attempt;
        for (int i = 0; i < 6; i++, v /= 36) tmpl[n - 6 + i] = "abcdefghijklmnopqrstuvwxyz0123456789"[v % 36];
        if (_mkdir(tmpl) == 0) return true;
    }
    return false;
#else
    return mkdtemp(tmpl) != NULL;
#endif
}

void files_enter_game_dir(void) {
#ifndef __vita__
    if (files_is_dir("assets")) return;   /* lancé depuis le dossier du jeu (make run, dossier portable) */
    char *base = SDL_GetBasePath();       /* dossier du programme ; Contents/Resources/ dans un .app */
    if (!base) return;
#  ifdef __APPLE__
    /* .app : il peut tourner depuis un emplacement en lecture seule (Gatekeeper), les sauvegardes vont dans
       ~/Library/Application Support/<nom du jeu>/, avec un lien assets -> Resources/assets refait à chaque lancement */
    char assets[1024];
    snprintf(assets, sizeof assets, "%sassets", base);
    const bool bundle = strstr(base, ".app/Contents/Resources/") != NULL && files_is_dir(assets);
    char *pref = bundle ? SDL_GetPrefPath(NULL, GAME_NAME) : NULL;
    if (pref && chdir(pref) == 0) {
        unlink("assets");
        if (symlink(assets, "assets") != 0) fprintf(stderr, "files: lien vers %s impossible\n", assets);
    } else if (chdir(base) != 0) fprintf(stderr, "files: dossier %s inaccessible\n", base);
    SDL_free(pref);
#  else
    if (chdir(base) != 0) fprintf(stderr, "files: dossier %s inaccessible\n", base);
#  endif
    SDL_free(base);
#endif
}

bool files_remove_tree(const char *dir) {
    char base[256];
    strip_slash(base, sizeof base, dir);
    DIR *d = opendir(base);
    if (!d) return true;   /* déjà absent */
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char child[256];
        if (snprintf(child, sizeof child, "%s/%s", base, e->d_name) >= (int)sizeof child) continue;
        if (files_is_dir(child)) files_remove_tree(child);
        else files_remove(child);
    }
    closedir(d);
    return remove_dir(base) || !files_is_dir(base);
}

const char *asset_resolve(const char *rel, char *buf, size_t size) {
    snprintf(buf, size, CUSTOM_DIR "%s", rel);
    long long bytes, mtime;
    if (!files_stat(buf, &bytes, &mtime)) snprintf(buf, size, ASSET_DIR "%s", rel);
    return buf;
}
