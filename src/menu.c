#include "menu.h"
#include "config.h"
#include "chart.h"
#include "ui.h"
#include "files.h"
#include <math.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Records : scores_v2.txt depuis le score pondéré par le combo (2026-10-12). L'ancien scores.txt (points plats, autre
   échelle) n'est ni lu ni effacé : ses valeurs ne seraient pas comparables. */
#define SCORES_FILE SAVE_DIR "scores_v2.txt"
#define CARD_IN_S   0.38f   /* entrée de la fiche */
#define THUMB_W       72.0f    /* pochettes de la fiche d'un pack (= taille de décodage des vignettes) */
#define THUMB_H       41.0f
#define THUMB_COUNT    4

/* Ordre des difficultés d'un morceau : ORDER s'il est donné (l'éditeur le pose sur toutes), sinon les noms
   connus dans cet ordre (« EASY 2 » avec EASY), puis les noms libres ; à égalité, par nombre de notes */
static int difficulty_rank(const char *d) {
    static const char *ORDER[] = {"EASY", "NORMAL", "HARD", "EXPERT"};
    int n = (int)(sizeof ORDER / sizeof ORDER[0]);
    for (int i = 0; i < n; i++) {   /* « EASY 2 » se range avec EASY */
        size_t len = strlen(ORDER[i]);
        if (SDL_strncasecmp(d, ORDER[i], len) == 0 && (d[len] == '\0' || d[len] == ' ')) return i;
    }
    return n;
}

/* Deux cartes du même morceau : même map (ID), ou même titre pour les cartes sans ID (format 0) */
static bool same_song(const MenuEntry *x, const MenuEntry *y) {
    if (x->id[0] && y->id[0]) return strcmp(x->id, y->id) == 0;
    return !x->id[0] && !y->id[0] && strcmp(x->title, y->title) == 0;
}

static int cmp_entries(const void *a, const void *b) {
    const MenuEntry *x = a, *y = b;
    int c = strcmp(x->title, y->title);
    if (c) return c;
    c = strcmp(x->id, y->id);   /* deux maps de même titre restent deux morceaux */
    if (c) return c;
    int ox = x->order > 0 ? x->order : INT_MAX, oy = y->order > 0 ? y->order : INT_MAX;
    if (ox != oy) return ox < oy ? -1 : 1;
    c = difficulty_rank(x->difficulty) - difficulty_rank(y->difficulty);
    return c ? c : x->notes - y->notes;
}

/* Remplit m->songs avec les chansons (cartes consécutives du même morceau) du pack `pack`
   (chaîne vide = SINGLES), ou toutes si pack == NULL. */
static void list_songs(Menu *m, const char *pack) {
    m->song_count = 0;
    for (int i = 0; i < m->entry_count; ) {
        int j = i + 1;
        while (j < m->entry_count && same_song(&m->entries[j], &m->entries[i])) j++;
        if (!pack || strcmp(m->entries[i].pack, pack) == 0)
            m->songs[m->song_count++] = (MenuSong){ i, j - i };
        i = j;
    }
}

/* Packs nommés par ordre alphabétique, SINGLES (sans nom) en dernier */
static int cmp_packs(const void *a, const void *b) {
    const MenuPack *x = a, *y = b;
    if ((x->name[0] == '\0') != (y->name[0] == '\0')) return x->name[0] ? -1 : 1;
    return SDL_strcasecmp(x->name, y->name);
}

/* Construit la liste des packs depuis la liste de TOUTES les chansons (m->songs doit être complète) */
static void build_packs(Menu *m) {
    m->pack_count = 0;
    m->has_packs = false;
    for (int s = 0; s < m->song_count; s++) {
        const MenuEntry *e = &m->entries[m->songs[s].first];
        int p = 0;
        while (p < m->pack_count && strcmp(m->packs[p].name, e->pack) != 0) p++;
        if (p == m->pack_count) {
            m->packs[m->pack_count] = (MenuPack){0};
            snprintf(m->packs[m->pack_count].name, sizeof m->packs[0].name, "%s", e->pack);
            m->packs[m->pack_count].first_entry = m->songs[s].first;
            m->pack_count++;
        }
        m->packs[p].song_count++;
        if (e->pack[0]) m->has_packs = true;
    }
    qsort(m->packs, (size_t)m->pack_count, sizeof m->packs[0], cmp_packs);
}

/* Clé d'avant le format de map 1 (cartes à plat dans assets/charts/) : « <ID>_<difficulté>.chart » */
static bool legacy_key_matches(const MenuEntry *e, const char *key) {
    char legacy[96], diff[24];
    size_t n = 0;
    for (; e->difficulty[n] && n + 1 < sizeof diff; n++)
        diff[n] = (char)((e->difficulty[n] >= 'A' && e->difficulty[n] <= 'Z') ? e->difficulty[n] + 32 : e->difficulty[n]);
    diff[n] = '\0';
    snprintf(legacy, sizeof legacy, "%s_%s.chart", e->id, diff);
    return e->id[0] && strcmp(legacy, key) == 0;
}

/* Records : une ligne « clé score ». Les clés d'avant le format 1 sont reprises au passage ; celles sans carte
   installée sont gardées (une map supprimée puis réimportée retrouve son record). */
static void load_scores(Menu *m) {
    FILE *f = fopen(SCORES_FILE, "r");
    if (!f) return;
    char key[96];
    int score, capacity = 0;
    while (fscanf(f, "%95s %d", key, &score) == 2) {
        bool used = false;
        for (int i = 0; i < m->entry_count; i++) {
            MenuEntry *e = &m->entries[i];
            if (strcmp(e->score_key, key) == 0 || legacy_key_matches(e, key)) {
                if (score > e->best_score) e->best_score = score;
                used = true;
            }
        }
        if (used) continue;
        if (m->other_score_count == capacity) {
            int grown_cap = capacity ? capacity * 2 : 32;
            MenuScore *grown = realloc(m->other_scores, sizeof *grown * (size_t)grown_cap);
            if (!grown) break;
            m->other_scores = grown;
            capacity = grown_cap;
        }
        MenuScore *o = &m->other_scores[m->other_score_count++];
        snprintf(o->key, sizeof o->key, "%s", key);
        o->score = score;
    }
    fclose(f);
}

static void save_scores(const Menu *m) {
    files_mkdir(SAVE_DIR);
    FILE *f = fopen(SCORES_FILE, "w");
    if (!f) { fprintf(stderr, "menu: impossible d'écrire %s\n", SCORES_FILE); return; }
    for (int i = 0; i < m->entry_count; i++)
        if (m->entries[i].best_score > 0)
            fprintf(f, "%s %d\n", m->entries[i].score_key, m->entries[i].best_score);
    for (int i = 0; i < m->other_score_count; i++)
        fprintf(f, "%s %d\n", m->other_scores[i].key, m->other_scores[i].score);
    fclose(f);
}

/* Ajoute une carte à m->entries (tableau agrandi au besoin) ; false si elle est illisible ou la mémoire manque */
/* Index des cartes (SAVE_DIR "index.txt") : les champs du menu de chaque carte, pour ne pas relire des centaines de
   .chart à chaque lancement. Une ligne par carte, champs séparés par des tabulations ; une ligne ne sert que si la
   taille et la date du fichier n'ont pas changé. L'index est réécrit dès qu'une carte a dû être relue. */
#define INDEX_FILE    SAVE_DIR "index.txt"
#define INDEX_HEADER  "DNE-INDEX 2"   /* 2 : ORDER ajouté (champ 16) */

static int cmp_entry_path(const void *a, const void *b) {
    return strcmp(((const MenuEntry *)a)->path, ((const MenuEntry *)b)->path);
}

/* Coupe `line` sur les tabulations (champs vides gardés) ; retourne le nombre de champs */
static int split_tabs(char *line, char **fields, int max) {
    int n = 0;
    fields[n++] = line;
    for (char *c = line; *c && n < max; c++)
        if (*c == '\t') { *c = '\0'; fields[n++] = c + 1; }
    return n;
}

static void index_load(Menu *m) {
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return;
    char line[1024];
    if (!fgets(line, sizeof line, f) || strncmp(line, INDEX_HEADER, strlen(INDEX_HEADER)) != 0) { fclose(f); return; }
    int capacity = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *v[16];
        if (split_tabs(line, v, 16) != 16) continue;
        if (m->index_count == capacity) {
            int grown_cap = capacity ? capacity * 2 : 128;
            MenuEntry *grown = realloc(m->index, sizeof *grown * (size_t)grown_cap);
            if (!grown) break;
            m->index = grown;
            capacity = grown_cap;
        }
        MenuEntry *e = &m->index[m->index_count++];
        *e = (MenuEntry){0};
        snprintf(e->path, sizeof e->path, "%s", v[0]);
        e->file_size = strtoll(v[1], NULL, 10);
        e->file_mtime = strtoll(v[2], NULL, 10);
        snprintf(e->id, sizeof e->id, "%s", v[3]);
        snprintf(e->title, sizeof e->title, "%s", v[4]);
        snprintf(e->artist, sizeof e->artist, "%s", v[5]);
        snprintf(e->pack, sizeof e->pack, "%s", v[6]);
        snprintf(e->difficulty, sizeof e->difficulty, "%s", v[7]);
        e->bpm = strtof(v[8], NULL);
        e->offset_ms = atoi(v[9]);
        e->notes = atoi(v[10]);
        snprintf(e->background, sizeof e->background, "%s", v[11]);
        e->background_dim = atoi(v[12]);
        snprintf(e->audio, sizeof e->audio, "%s", v[13]);
        e->preview_ms = atoi(v[14]);
        e->order = atoi(v[15]);
    }
    fclose(f);
    qsort(m->index, (size_t)m->index_count, sizeof *m->index, cmp_entry_path);
}

static void index_save(const Menu *m) {
    files_mkdir(SAVE_DIR);
    FILE *f = fopen(INDEX_FILE, "w");
    if (!f) return;
    fprintf(f, "%s\n", INDEX_HEADER);
    for (int i = 0; i < m->entry_count; i++) {
        const MenuEntry *e = &m->entries[i];
        fprintf(f, "%s\t%lld\t%lld\t%s\t%s\t%s\t%s\t%s\t%.4f\t%d\t%d\t%s\t%d\t%s\t%d\t%d\n", e->path, e->file_size,
                e->file_mtime, e->id, e->title, e->artist, e->pack, e->difficulty, e->bpm, e->offset_ms, e->notes,
                e->background, e->background_dim, e->audio, e->preview_ms, e->order);
    }
    fclose(f);
}

/* Champs de la carte `path` : depuis l'index s'il est à jour, sinon en lisant le .chart. false si illisible. */
static bool entry_fields(Menu *m, MenuEntry *en, const char *path, const char *file) {
    long long size = -1, mtime = -1;
    files_stat(path, &size, &mtime);
    MenuEntry key;
    snprintf(key.path, sizeof key.path, "%s", path);
    const MenuEntry *hit = m->index_count ? bsearch(&key, m->index, (size_t)m->index_count, sizeof *m->index, cmp_entry_path) : NULL;
    if (hit && hit->file_size == size && hit->file_mtime == mtime && size >= 0) {
        *en = *hit;
        return true;
    }
    Chart c;
    if (!chart_load(&c, path)) return false;
    *en = (MenuEntry){0};
    snprintf(en->path, sizeof en->path, "%s", path);
    en->file_size = size;
    en->file_mtime = mtime;
    snprintf(en->id, sizeof en->id, "%s", c.id);
    if (snprintf(en->title, sizeof en->title, "%s", c.title[0] ? c.title : file) >= (int)sizeof en->title)
        en->title[sizeof en->title - 1] = '\0';   /* titre tronqué : sans conséquence */
    snprintf(en->artist, sizeof en->artist, "%s", c.artist);
    snprintf(en->pack, sizeof en->pack, "%s", c.pack);
    snprintf(en->difficulty, sizeof en->difficulty, "%s", c.difficulty[0] ? c.difficulty : "-");
    en->bpm = c.bpm;
    en->offset_ms = c.offset_ms;
    en->notes = c.note_count;
    snprintf(en->background, sizeof en->background, "%s", c.background_path);
    en->background_dim = c.background_dim;
    snprintf(en->audio, sizeof en->audio, "%s", c.audio_path);
    en->preview_ms = c.preview_ms;
    en->order = c.order;
    chart_free(&c);
    m->index_dirty = true;
    return true;
}

/* Ajoute une carte à m->entries (tableau agrandi au besoin) ; false si elle est illisible ou la mémoire manque */
static bool add_entry(Menu *m, const char *path, const char *folder, const char *file, bool imported, int *capacity) {
    if (m->entry_count == *capacity) {
        int grown_cap = *capacity ? *capacity * 2 : 128;
        MenuEntry *grown = realloc(m->entries, sizeof *grown * (size_t)grown_cap);
        if (!grown) { fprintf(stderr, "menu: mémoire insuffisante, liste tronquée\n"); return false; }
        m->entries = grown;
        *capacity = grown_cap;
    }
    MenuEntry *en = &m->entries[m->entry_count];
    if (!entry_fields(m, en, path, file)) return false;
    if (imported && en->id[0])   /* une map importée ne remplace pas une map officielle de même ID */
        for (int i = 0; i < m->entry_count; i++)
            if (!m->entries[i].imported && strcmp(m->entries[i].id, en->id) == 0) {
                fprintf(stderr, "menu: %s ignorée (ID '%s' déjà pris par une map officielle)\n", path, en->id);
                return false;
            }
    snprintf(en->folder, sizeof en->folder, "%s", folder);
    if (en->id[0]) {   /* « <ID>:<DIFFICULTÉ> », espaces remplacés (une clé = un mot dans le fichier des records) */
        snprintf(en->score_key, sizeof en->score_key, "%s:%s", en->id, en->difficulty);
        for (char *k = en->score_key; *k; k++) if (*k == ' ') *k = '_';
    } else {
        snprintf(en->score_key, sizeof en->score_key, "%s", file);
    }
    en->imported = imported;
    m->entry_count++;
    return true;
}

static bool is_chart_name(const char *name) {
    size_t n = strlen(name);
    return n >= 7 && name[0] != '.' && strcmp(name + n - 6, ".chart") == 0;
}

/* Parcourt root/<ID>/<fichier>.chart : une map = un dossier (voir chart.h). Dossier absent = aucune map. */
static void scan_root(Menu *m, const char *root, bool imported, int *capacity) {
    DIR *d = opendir(root);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char folder[CHART_PATH];
        if (snprintf(folder, sizeof folder, "%s%s/", root, e->d_name) >= (int)sizeof folder) continue;
        DIR *md = opendir(folder);
        if (!md) continue;   /* pas un dossier */
        struct dirent *f;
        while ((f = readdir(md))) {
            if (!is_chart_name(f->d_name)) continue;
            char path[CHART_PATH], file[96];
            if (snprintf(path, sizeof path, "%s%s", folder, f->d_name) >= (int)sizeof path) continue;
            if (snprintf(file, sizeof file, "%s/%s", e->d_name, f->d_name) >= (int)sizeof file) continue;
            add_entry(m, path, folder, file, imported, capacity);
        }
        closedir(md);
    }
    closedir(d);
}

static void load_entries(Menu *m);

void menu_init(Menu *m, SDL_Renderer *r) {
    *m = (Menu){0};
    m->renderer = r;
    bgcache_init(&m->bgc, r);
    m->bgc.grayscale = true;   /* bichromie : teinte posée au dessin (ui_duotone) */
    bgcache_init(&m->thumbs, r);
    m->thumbs.thumb_w = (int)THUMB_W;
    m->thumbs.thumb_h = (int)THUMB_H;
    m->thumbs.slot_count = BGCACHE_SLOTS_MAX;   /* 16 vignettes : le pack survolé et ses voisins (~12 Ko chacune) */
    preview_init(&m->preview);
    m->info_song = -1;
    m->info_t = 1.0f;
    m->change_t = 10.0f;   /* à l'ouverture : ni éclaboussure qui éclate ni gerbe */
    m->deco_prev = -1;
    load_entries(m);
}

/* Liste des cartes (maps officielles puis importées), morceaux, packs et records */
static void load_entries(Menu *m) {
    int capacity = 0;
    Uint32 scan_start = SDL_GetTicks();
    index_load(m);
    scan_root(m, ASSET_DIR "songs/", false, &capacity);   /* maps officielles, dans le jeu */
    scan_root(m, USER_SONGS_DIR, true, &capacity);         /* maps importées par le joueur */
    if (m->index_dirty || m->entry_count != m->index_count) index_save(m);   /* carte relue, ajoutée ou retirée */
    free(m->index);
    m->index = NULL;
    m->index_count = 0;

    /* au plus une chanson par carte (une par pack au pire) : tableaux dimensionnés une fois pour toutes */
    size_t slots = (size_t)(m->entry_count > 0 ? m->entry_count : 1);
    m->songs = calloc(slots, sizeof *m->songs);
    m->packs = calloc(slots, sizeof *m->packs);
    if (!m->songs || !m->packs) {
        fprintf(stderr, "menu: mémoire insuffisante\n");
        free(m->entries); free(m->songs); free(m->packs);
        m->entries = NULL; m->songs = NULL; m->packs = NULL;
        m->entry_count = m->song_count = m->pack_count = 0;
        return;
    }

    qsort(m->entries, (size_t)m->entry_count, sizeof m->entries[0], cmp_entries);
    list_songs(m, NULL);
    build_packs(m);
    load_scores(m);
    fprintf(stderr, "menu: %d cartes, %d chansons, %d packs (%u ms)\n", m->entry_count, m->song_count,
            m->has_packs ? m->pack_count : 0, (unsigned)(SDL_GetTicks() - scan_start));
}

void menu_shutdown(Menu *m) {
    menu_suspend(m);
    bgcache_shutdown(&m->bgc);
    bgcache_shutdown(&m->thumbs);
    preview_shutdown(&m->preview);
    free(m->entries); free(m->songs); free(m->packs); free(m->other_scores);
    m->entries = NULL; m->songs = NULL; m->packs = NULL; m->other_scores = NULL;
    m->entry_count = m->song_count = m->pack_count = m->other_score_count = 0;
}

void menu_suspend(Menu *m) {
    preview_close(&m->preview);
    bgcache_clear(&m->bgc);
    bgcache_clear(&m->thumbs);
    m->bg_shown[0] = '\0';
    m->bg = NULL;
}

/* Onglet PACKS, niveau 1 : la liste affichée est celle des packs, pas des chansons */
static bool packs_level(const Menu *m) { return m->tab == MENU_TAB_PACKS && !m->in_pack; }
static int  list_count(const Menu *m) { return packs_level(m) ? m->pack_count : m->song_count; }
static int  list_sel(const Menu *m)   { return packs_level(m) ? m->pack : m->song; }

/* Carte dont dépend le fond d'écran : la sélection, ou la première chanson du pack survolé */
static const MenuEntry *background_entry(const Menu *m) {
    if (packs_level(m)) return &m->entries[m->packs[m->pack].first_entry];
    return &m->entries[m->songs[m->song].first + m->diff];
}

/* Carte affichée à la position `idx` de la liste courante (packs ou chansons) */
static const MenuEntry *entry_at(const Menu *m, int idx) {
    if (packs_level(m)) return &m->entries[m->packs[idx].first_entry];
    return &m->entries[m->songs[idx].first + (idx == m->song ? m->diff : 0)];
}

/* Nom de pack « Catégorie - Saison » : la cascade montre la saison, la fiche la catégorie. Sans « - », le nom
   entier est la saison et la catégorie est vide. */
static const char *pack_season(const char *name) {
    const char *dash = strstr(name, " - ");
    return dash ? dash + 3 : name;
}

static void pack_category(const char *name, char *out, size_t size) {
    const char *dash = strstr(name, " - ");
    size_t n = dash ? (size_t)(dash - name) : 0;
    if (n >= size) n = size - 1;
    memcpy(out, name, n);
    out[n] = '\0';
}

/* Morceaux du pack p (première carte de chacun, dans l'ordre des titres) : les `max` premiers dans out, écart de
   tempo et nombre de cartes du pack ; retourne le nombre de morceaux */
static int pack_songs(const Menu *m, int p, int *out, int max, float *bmin, float *bmax, int *charts) {
    const char *name = m->packs[p].name;
    int n = 0;
    *charts = 0;
    for (int i = 0; i < m->entry_count; i++) {
        const MenuEntry *e = &m->entries[i];
        if (strcmp(e->pack, name) != 0) continue;
        if (*charts == 0 || e->bpm < *bmin) *bmin = e->bpm;
        if (*charts == 0 || e->bpm > *bmax) *bmax = e->bpm;
        (*charts)++;
        if (i > 0 && same_song(e, &m->entries[i - 1])) continue;   /* autre difficulté du même morceau */
        if (n < max) out[n] = i;
        n++;
    }
    return n;
}

/* Vignettes des pochettes (liste des packs) : le pack survolé puis ses voisins */
static void update_thumbs(Menu *m) {
    char paths[BGCACHE_QUEUE][BGCACHE_PATH];
    const char *ptrs[BGCACHE_QUEUE] = {0};
    int dims[BGCACHE_QUEUE] = {0}, n = 0;
    if (packs_level(m) && m->pack_count > 0) {
        const int order[3] = { 0, 1, -1 };
        for (int k = 0; k < 3; k++) {
            int p = (m->pack + order[k] + m->pack_count) % m->pack_count, songs[THUMB_COUNT], charts;
            float lo, hi;
            int c = pack_songs(m, p, songs, THUMB_COUNT, &lo, &hi, &charts);
            for (int s = 0; s < c && s < THUMB_COUNT && n < BGCACHE_QUEUE; s++) {
                if (!m->entries[songs[s]].background[0]) continue;
                snprintf(paths[n], BGCACHE_PATH, "%s", m->entries[songs[s]].background);
                ptrs[n] = paths[n];
                n++;
            }
        }
    }
    bgcache_want(&m->thumbs, ptrs, dims, n);
    bgcache_update(&m->thumbs);
    for (int k = 0; k < n; k++) bgcache_get(&m->thumbs, ptrs[k]);   /* récemment utilisées : gardées en priorité */
}

/* Image de fond : demande l'image de la sélection puis celles de ses voisines (décodées en arrière-plan), et
   affiche celle de la sélection dès qu'elle est prête. Les difficultés d'une même chanson partagent l'image. */
static void update_background(Menu *m) {
    if (m->entry_count == 0 || list_count(m) == 0) return;
    const MenuEntry *e = background_entry(m);
    char paths[5][BGCACHE_PATH];
    const char *ptrs[5];
    int dims[5], n = 0, sel = list_sel(m), count = list_count(m);
    const int order[5] = { 0, 1, -1, 2, -2 };
    for (int k = 0; k < 5; k++) {
        int idx = sel + order[k];
        if (idx < 0 || idx >= count) continue;
        const MenuEntry *x = k == 0 ? e : entry_at(m, idx);
        if (!x->background[0]) continue;
        snprintf(paths[n], BGCACHE_PATH, "%s", x->background);
        ptrs[n] = paths[n];
        dims[n++] = 0;   /* pas d'assombrissement : la bichromie et le voile d'encre (menu_draw) s'en chargent */
    }
    bgcache_want(&m->bgc, ptrs, dims, n);
    bgcache_update(&m->bgc);

    if (!e->background[0]) m->bg_shown[0] = '\0';
    else if (n > 0 && bgcache_get(&m->bgc, paths[0])) snprintf(m->bg_shown, sizeof m->bg_shown, "%s", paths[0]);
    m->bg = bgcache_get(&m->bgc, m->bg_shown);   /* sinon l'image précédente reste jusqu'à ce que la nouvelle soit prête */
}

/* Aperçu musical : la chanson de la sélection (la première du pack survolé), à partir de son refrain.
   Les difficultés d'une même chanson partagent la piste : changer de difficulté ne relance pas l'extrait. */
static void update_preview(Menu *m, float dt) {
    char path[PREVIEW_PATH] = "";
    int start_ms = 0;
    if (m->entry_count > 0 && list_count(m) > 0) {
        const MenuEntry *e = background_entry(m);
        snprintf(path, sizeof path, "%s", e->audio);
        start_ms = e->preview_ms;
    }
    preview_want(&m->preview, path, start_ms);
    preview_update(&m->preview, dt);

    /* horloge du tempo : avance seule, et se recale sur la musique dès que l'extrait joue */
    m->beat_ms += dt * 1000.0f;
    int pos = preview_position_ms(&m->preview, path);
    if (pos >= 0) {
        float err = (float)pos - m->beat_ms;
        if (fabsf(err) > 120.0f) m->beat_ms = (float)pos;   /* l'extrait vient de démarrer : saut */
        else m->beat_ms += err * 0.1f;                      /* sinon correction douce */
    }
}

/* La cascade saute à la nouvelle liste (au lieu de glisser d'une liste à l'autre) et la fiche se renouvelle */
static void snap_list(Menu *m) {
    m->cursor = (float)list_sel(m);
    m->info_song = -1;
    m->info_t = 1.0f;
    m->change_t = 10.0f;   /* pas d'éclaboussure ni de gerbe en changeant de liste */
    m->deco_prev = -1;
}

/* Revient à la liste de tous les morceaux, en gardant le morceau de la carte `keep` s'il y est */
static void show_all_songs(Menu *m, const MenuEntry *keep) {
    list_songs(m, NULL);
    m->in_pack = false;
    if (keep)
        for (int s = 0; s < m->song_count; s++)
            if (same_song(&m->entries[m->songs[s].first], keep)) { m->song = s; break; }
    if (m->song >= m->song_count) m->song = 0;
    int max_diff = m->song_count ? m->songs[m->song].count - 1 : 0;
    if (m->diff > max_diff) m->diff = max_diff;
}

static bool tab_available(const Menu *m, MenuTab t) {
    return t == MENU_TAB_ALL || (t == MENU_TAB_PACKS && m->has_packs);
}
static bool tabs_shown(const Menu *m) { return m->has_packs; }

/* Onglet suivant (dir = 1, R) ou précédent (dir = -1, L) parmi ceux qui existent */
static void switch_tab(Menu *m, int dir) {
    const MenuEntry *keep = m->song_count > 0 ? &m->entries[m->songs[m->song].first] : NULL;   /* entries ne bouge pas */
    int t = (int)m->tab;
    do t = (t + dir + MENU_TAB_COUNT) % MENU_TAB_COUNT; while (!tab_available(m, (MenuTab)t));
    m->tab = (MenuTab)t;
    show_all_songs(m, keep);   /* PACKS s'ouvre toujours sur la liste des packs */
    snap_list(m);
}

/* Supprime le dossier de la map sélectionnée (importée seulement), puis relit la liste. Les records restent dans le
   fichier : la map les retrouve si elle est réimportée. */
static void delete_selected_map(Menu *m) {
    const MenuEntry *e = &m->entries[m->songs[m->song].first];
    if (!e->imported) return;
    char folder[CHART_PATH];
    snprintf(folder, sizeof folder, "%s", e->folder);
    int song = m->song;
    menu_suspend(m);   /* l'extrait musical et les images de cette map ne doivent plus être ouverts */
    if (!files_remove_tree(folder)) fprintf(stderr, "menu: suppression incomplète de %s\n", folder);
    free(m->entries); free(m->songs); free(m->packs); free(m->other_scores);
    m->entries = NULL; m->songs = NULL; m->packs = NULL; m->other_scores = NULL;
    m->entry_count = m->song_count = m->pack_count = m->other_score_count = 0;
    load_entries(m);
    if (!tab_available(m, m->tab)) m->tab = MENU_TAB_ALL;
    m->in_pack = false;
    show_all_songs(m, NULL);
    m->song = m->song_count ? (song < m->song_count ? song : m->song_count - 1) : 0;
    m->diff = 0;
    snap_list(m);
    preview_open(&m->preview);
    m->just_deleted = true;
}

void menu_update(Menu *m, const Input *in, float dt) {
    m->time += dt;
    if (m->entry_count == 0 || m->song_count == 0) return;

    if (m->confirm_delete) {   /* fenêtre de confirmation : rien d'autre ne bouge */
        if (in->pressed[BTN_CROSS]) { m->confirm_delete = false; delete_selected_map(m); }
        else if (in->pressed[BTN_CIRCLE]) m->confirm_delete = false;
        return;
    }
    if (tabs_shown(m) && in->pressed[BTN_L]) switch_tab(m, -1);
    else if (tabs_shown(m) && in->pressed[BTN_R]) switch_tab(m, 1);

    if (packs_level(m)) {
        if (in->pressed[BTN_DOWN]) m->pack = (m->pack + 1) % m->pack_count;
        if (in->pressed[BTN_UP])   m->pack = (m->pack + m->pack_count - 1) % m->pack_count;
        if (in->pressed[BTN_CIRCLE]) m->back_requested = true;   /* retour au menu principal */
        if (in->pressed[BTN_CROSS]) {   /* ouvre le pack : ses morceaux deviennent la liste */
            list_songs(m, m->packs[m->pack].name);
            m->in_pack = true;
            m->song = 0;
            snap_list(m);
        }
    } else {
        if (in->pressed[BTN_DOWN]) m->song = (m->song + 1) % m->song_count;
        if (in->pressed[BTN_UP])   m->song = (m->song + m->song_count - 1) % m->song_count;
        int max_diff = m->songs[m->song].count - 1;
        if (in->pressed[BTN_RIGHT] && m->diff < max_diff) m->diff++;
        if (in->pressed[BTN_LEFT] && m->diff > 0) m->diff--;
        if (m->diff > max_diff) m->diff = max_diff;
        if (in->pressed[BTN_CROSS]) m->start_requested = true;
        if (in->pressed[BTN_TRIANGLE] && m->entries[m->songs[m->song].first].imported) m->confirm_delete = true;
        if (!m->in_pack && in->pressed[BTN_CIRCLE]) m->back_requested = true;   /* retour au menu principal */
        if (m->in_pack && in->pressed[BTN_CIRCLE]) {   /* retour à la liste des packs */
            const MenuEntry *keep = &m->entries[m->songs[m->song].first];
            show_all_songs(m, keep);   /* hors d'un pack, m->songs redevient la liste de TOUS les morceaux */
            snap_list(m);
        }
    }

    /* animations : la cascade rejoint la sélection par le chemin le plus court (la liste boucle), la fiche se
       renouvelle */
    int sel = list_sel(m), count = list_count(m);
    float gap = (float)sel - m->cursor;
    if (gap > count * 0.5f) m->cursor += (float)count;
    else if (gap < -count * 0.5f) m->cursor -= (float)count;
    ui_approach(&m->cursor, (float)sel, 14.0f, dt);
    if (m->info_song != sel) {   /* nouvel élément : la fiche rentre par la droite (pas au changement de liste) */
        if (m->info_song >= 0) { m->info_t = 0.0f; m->change_t = 0.0f; }
        m->deco_prev = m->info_song;
        m->info_song = sel;
    }
    m->change_t = m->change_t + dt > 10.0f ? 10.0f : m->change_t + dt;
    m->info_t = m->info_t + dt / CARD_IN_S > 1.0f ? 1.0f : m->info_t + dt / CARD_IN_S;
    update_thumbs(m);
    update_background(m);   /* après la lecture des boutons : la nouvelle sélection s'affiche dès cette image */
    update_preview(m, dt);
}

const MenuEntry *menu_take_selection(Menu *m) {
    if (!m->start_requested || m->song_count == 0) { m->start_requested = false; return NULL; }
    m->start_requested = false;
    return &m->entries[m->songs[m->song].first + m->diff];
}

int menu_find_entry(const Menu *m, const char *path) {
    for (int i = 0; i < m->entry_count; i++)
        if (strcmp(m->entries[i].path, path) == 0) return i;
    return -1;
}

bool menu_record_score(Menu *m, int entry, int score) {
    if (entry < 0 || entry >= m->entry_count || score <= m->entries[entry].best_score) return false;
    m->entries[entry].best_score = score;
    save_scores(m);
    return true;
}

/* ---------------------------------------------------------------- Dessin */

/* Fiche « Cascade pop » : carte d'encre penchée en bas à droite, ombre rose décalée */
#define CARD_W       350.0f
#define CARD_H_SONG  122.0f   /* fiche d'un morceau */
#define CARD_H_PACK  164.0f   /* fiche d'un pack : avec les pochettes et le début de la liste */
#define CARD_X       (SCREEN_W - 24.0f - CARD_W)
#define CARD_BOTTOM  (SCREEN_H - 26.0f)
#define CARD_PAD      20.0f
#define CARD_TEXT_W  310.0f
#define CARD_PILLS_W 322.0f     /* rangée des pastilles de difficulté (EASY..EXPERT : 317 px) */
#define MENU_PILLS_MAX 32       /* difficultés affichées sur la fiche d'un morceau */
#define CARD_TAN       0.1405f   /* tan 8° : inclinaison de la carte */
#define CARD_SLIDE    60.0f     /* px parcourus à l'entrée */

/* Cascade « Cascade pop » (maquette validée le 14/10/2026) : titres en capitales le long d'une diagonale inclinée
   de 10°, la sélection énorme en jaune cernée d'encre, ses voisines en contour blanc de plus en plus petites et
   pâles. Chaque position est interpolée entre les rangs entiers : la cascade glisse quand la sélection change. */
#define CASC_X        40.0f    /* pivot de la cascade */
#define CASC_Y       262.0f
#define CASC_ANGLE   -10.0f    /* monte vers la droite */
#define CASC_INDENT   18.0f    /* retrait du titre sélectionné */
#define CASC_STEP_X   22.0f    /* décalage horizontal par rang */
#define CASC_SEL_MAX  66.0f    /* taille du titre sélectionné (réduit pour tenir dans CASC_SEL_W) */
#define CASC_SEL_MIN  34.0f
#define CASC_SEL_W   560.0f
#define CASC_NEAR     38.0f    /* voisins directs */
#define CASC_FAR      26.0f    /* au-delà */
#define CASC_OTHER_W 600.0f    /* largeur maximale d'un voisin */

/* Libellé (en capitales) de l'élément `idx` de la liste affichée */
static void cascade_label(const Menu *m, int idx, char *out, size_t size) {
    const char *name;
    if (packs_level(m)) name = m->packs[idx].name[0] ? pack_season(m->packs[idx].name) : "SINGLES";
    else                name = m->entries[m->songs[idx].first].title;
    ui_upper(name, out, size);
}

/* Taille du titre sélectionné : la plus grande qui tient dans CASC_SEL_W, bornée */
static float selected_size(const Font *font, const char *label) {
    float w = (float)font_width(font, FONT_POP_XL, label);
    float size = w > 0.0f ? CASC_SEL_MAX * CASC_SEL_W / w : CASC_SEL_MAX;
    return size > CASC_SEL_MAX ? CASC_SEL_MAX : size < CASC_SEL_MIN ? CASC_SEL_MIN : size;
}

/* Centre vertical (repère de la cascade) du rang entier o != 0 ; sel_h = taille du titre sélectionné.
   Au-dessus, un écart en plus laisse la place à la bannière NIGHT. */
static float rank_y(int o, float sel_h) {
    int a = o < 0 ? -o : o;
    float extra = (float)(a - 1) * 34.0f + (a == 1 ? 18.0f : 30.0f);
    return o < 0 ? -(sel_h * 0.5f + 62.0f + extra) : sel_h * 0.5f + 26.0f + extra;
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* ---- Éclaboussure, gerbe, décor et tempo (maquette v3) ----
   Tout est tiré d'un hachage du titre : un morceau a toujours la même éclaboussure et le même décor, et rien n'est
   gardé en mémoire (recalculé à chaque image, quelques dizaines de points). */

#define SPLAT_S      0.46f   /* l'éclaboussure éclate */
#define BURST_S      0.70f   /* la gerbe de confettis */
#define BURST_COUNT  14
#define DECO_MOVE_S  0.60f   /* le décor rejoint ses nouvelles places */
#define DECO_COUNT   8       /* 5 losanges, 3 étoiles */
#define GEO_MAX      400     /* sommets d'un lot de géométrie (chaque polygone en prend 2n + 1 avec sa frange lissée) */

static Uint32 hash_str(const char *s, const char *salt) {
    Uint32 h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    for (; *salt; salt++) { h ^= (unsigned char)*salt; h *= 16777619u; }
    return h;
}

/* Générateur pseudo-aléatoire (mulberry32) : 0 <= x < 1 */
static float rng_next(Uint32 *state) {
    Uint32 t = (*state += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (float)(t ^ (t >> 14)) / 4294967296.0f;
}

/* Lot de triangles colorés, envoyé en un seul SDL_RenderGeometry */
typedef struct { SDL_Vertex v[GEO_MAX]; int idx[GEO_MAX * 3]; int nv, ni; } Geo;

/* Polygone plein à bords lissés (pts : coordonnées écran) */
static void geo_fan(Geo *g, const SDL_FPoint *pts, int n, SDL_Color c) {
    ui_poly_append(g->v, &g->nv, GEO_MAX, g->idx, &g->ni, GEO_MAX * 3, pts, n, c);
}

static void geo_flush(Geo *g, SDL_Renderer *r) {
    if (g->ni == 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, NULL, g->v, g->nv, g->idx, g->ni);
    g->nv = g->ni = 0;
}

/* Point (x, y) du repère de la cascade -> écran (rotation autour du pivot) */
static SDL_FPoint casc_point(float x, float y) {
    const float rad = CASC_ANGLE * (3.14159265f / 180.0f), cs = cosf(rad), sn = sinf(rad);
    float dx = x - CASC_X, dy = y - CASC_Y;
    return (SDL_FPoint){ CASC_X + dx * cs - dy * sn, CASC_Y + dx * sn + dy * cs };
}

/* Forme : étoile à quatre branches et pentagone d'éclat, en fractions de leur boîte */
static const float STAR_PTS[8][2] = { {.5f, 0}, {.62f, .38f}, {1, .5f}, {.62f, .62f}, {.5f, 1}, {.38f, .62f}, {0, .5f}, {.38f, .38f} };
static const float SHARD_PTS[5][2] = { {.5f, 0}, {1, .38f}, {.72f, 1}, {.18f, .82f}, {0, .3f} };

/* Pose une forme (pts en fractions d'une boîte w x h) centrée en (cx, cy), agrandie de `scale`, tournée de `deg` */
static void geo_shape(Geo *g, const float (*pts)[2], int n, float cx, float cy, float w, float h, float scale, float deg,
                      SDL_Color c) {
    SDL_FPoint out[8];
    const float rad = deg * (3.14159265f / 180.0f), cs = cosf(rad), sn = sinf(rad);
    for (int i = 0; i < n; i++) {
        float x = (pts[i][0] - 0.5f) * w * scale, y = (pts[i][1] - 0.5f) * h * scale;
        out[i] = (SDL_FPoint){ cx + x * cs - y * sn, cy + x * sn + y * cs };
    }
    geo_fan(g, out, n, c);
}

static void geo_diamond(Geo *g, float cx, float cy, float side, float deg, SDL_Color c) {
    static const float SQ[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    geo_shape(g, SQ, 4, cx, cy, side, side, 1.0f, deg + 45.0f, c);
}

/* Marges de l'éclaboussure autour du titre : les deux premiers tirages du hachage */
static void splash_pads(const char *label, float *pad_x, float *pad_y) {
    Uint32 st = hash_str(label, "");
    *pad_x = 34.0f + rng_next(&st) * 14.0f;
    *pad_y = 28.0f + rng_next(&st) * 6.0f;
}

/* Tempo : k = impulsion de chaque temps (montée brève, retombée douce), kd = la même sur le temps fort (1 sur 4) */
typedef struct { float k, kd; } Beat;

static Beat menu_beat(const Menu *m) {
    if (list_count(m) == 0) return (Beat){0, 0};
    const MenuEntry *e = background_entry(m);
    if (e->bpm <= 0.0f) return (Beat){0, 0};
    float beats = (m->beat_ms - (float)e->offset_ms) * e->bpm / 60000.0f;
    float n = floorf(beats), ph = beats - n;
    float env = ph < 0.08f ? sinf(ph / 0.08f * 1.5707963f) : expf(-(ph - 0.08f) * 4.5f);
    long beat = (long)n;
    return (Beat){ env, beat % 4 == 0 ? env : 0.0f };
}

/* Éclaboussure rose derrière le titre sélectionné : rectangle intérieur dont le bord est bosselé, avec des pointes
   jusqu'au bord de la boîte et des entailles, plus quelques éclats autour. (left, cy) = début du titre et milieu
   de ses majuscules dans le repère de la cascade ; title_w, title_h = taille du titre ; splat 0..1 = éclatement. */
static void draw_splash(SDL_Renderer *r, const char *label, float left, float cy, float title_w, float title_h,
                        float splat_t, Beat beat) {
    Geo g; g.nv = g.ni = 0;   /* sur la pile (~5 Ko) : pas d'état caché entre deux images */
    Uint32 st = hash_str(label, "");
    float pad_x = 34.0f + rng_next(&st) * 14.0f, pad_y = 28.0f + rng_next(&st) * 6.0f;
    rng_next(&st);   /* tirage de l'inclinaison, nulle dans la maquette validée */
    const float w = title_w + pad_x * 2.0f, h = title_h + pad_y * 2.0f, bx = left - pad_x, by = cy - h * 0.5f;

    /* éclatement : part de (0,15 ; 0,35), dépasse un peu, se pose ; plus le battement */
    float t = splat_t / SPLAT_S > 1.0f ? 1.0f : splat_t / SPLAT_S, u = t - 1.0f;
    float e = 1.0f + 2.70158f * u * u * u + 1.70158f * u * u;   /* easeOutBack */
    float sx = lerpf(0.15f, 1.0f, e), sy = lerpf(0.35f, 1.0f, e), tilt = -6.0f * (1.0f - e);
    float al = t < 0.6f ? 0.2f + 0.8f * t / 0.6f : 1.0f;
    SDL_Color c = ui_alpha(CH_PINK, (Uint8)(255.0f * al));
    const float pulse = 1.0f + beat.k * 0.03f + beat.kd * 0.05f;
    const float rad = tilt * (3.14159265f / 180.0f), cs = cosf(rad), sn = sinf(rad);
    const float ccx = bx + w * 0.5f, ccy = by + h * 0.5f;
#define SPLASH_XF(px, py, k) casc_point(ccx + (((px) - w * 0.5f) * sx * (k)) * cs - (((py) - h * 0.5f) * sy * (k)) * sn, \
                                        ccy + (((px) - w * 0.5f) * sx * (k)) * sn + (((py) - h * 0.5f) * sy * (k)) * cs)

    /* contour : points régulièrement espacés sur le rectangle intérieur, repoussés le long de la normale */
    SDL_FPoint pts[48];
    const float mg = 14.0f, iw = w - 2.0f * mg, ih = h - 2.0f * mg, per = 2.0f * (iw + ih);
    int n = (int)(per / 26.0f + 0.5f);
    if (n < 16) n = 16;
    if (n > 48) n = 48;
    for (int i = 0; i < n; i++) {
        float d = ((float)i + rng_next(&st) * 0.4f) / (float)n * per, x, y, nx, ny;
        if (d < iw)                 { x = mg + d;                 y = mg;                   nx = 0;  ny = -1; }
        else if (d < iw + ih)       { x = w - mg;                 y = mg + d - iw;          nx = 1;  ny = 0; }
        else if (d < 2 * iw + ih)   { x = w - mg - (d - iw - ih); y = h - mg;               nx = 0;  ny = 1; }
        else                        { x = mg;                     y = h - mg - (d - 2 * iw - ih); nx = -1; ny = 0; }
        float off = (rng_next(&st) - 0.5f) * 8.0f;   /* bord irrégulier */
        if (rng_next(&st) < 0.16f) off = mg - 1.0f;   /* pointe */
        else if (rng_next(&st) < 0.12f) off = -9.0f;  /* entaille */
        pts[i] = SPLASH_XF(x + nx * off, y + ny * off, pulse);
    }
    geo_fan(&g, pts, n, c);

    /* éclats projetés sur les côtés (ils éclatent avec l'éclaboussure, sans battre) */
    int shards = 3 + (int)(rng_next(&st) * 3.0f);
    for (int k = 0; k < shards; k++) {
        float sz = 7.0f + rng_next(&st) * 12.0f;
        float side = rng_next(&st) < 0.5f ? -1.0f : 1.0f;
        rng_next(&st);   /* angle inutilisé dans la maquette */
        float sh = sz * (0.5f + rng_next(&st) * 0.6f), deg = rng_next(&st) * 360.0f;
        float lx = side < 0 ? -10.0f - rng_next(&st) * 26.0f : w + rng_next(&st) * 26.0f;
        float ly = h * (0.15f + rng_next(&st) * 0.7f);
        SDL_FPoint q[5];
        const float drad = deg * (3.14159265f / 180.0f), dc = cosf(drad), ds = sinf(drad);
        for (int j = 0; j < 5; j++) {
            float px = (SHARD_PTS[j][0] - 0.5f) * sz, py = (SHARD_PTS[j][1] - 0.5f) * sh;
            q[j] = SPLASH_XF(lx + sz * 0.5f + px * dc - py * ds, ly + sh * 0.5f + px * ds + py * dc, 1.0f);
        }
        geo_fan(&g, q, 5, c);
    }
#undef SPLASH_XF
    geo_flush(&g, r);
}

/* Gerbe de confettis lancée depuis le titre à chaque changement de morceau */
static void draw_burst(SDL_Renderer *r, const char *label, float ox, float oy, float change_t) {
    if (change_t >= BURST_S) return;
    Geo g; g.nv = g.ni = 0;   /* sur la pile (~5 Ko) : pas d'état caché entre deux images */
    const SDL_Color cols[4] = { CH_GOLD, CH_CYAN, CH_WHITE, CH_PINK };
    Uint32 st = hash_str(label, "#burst");
    float t = change_t / BURST_S, e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    for (int i = 0; i < BURST_COUNT; i++) {
        float a = rng_next(&st) * 6.2831853f, dist = 90.0f + rng_next(&st) * 160.0f;
        float spin = rng_next(&st) * 360.0f - 180.0f;
        SDL_FPoint p = casc_point(ox + cosf(a) * dist * e, oy + sinf(a) * dist * 0.7f * e);
        SDL_Color c = ui_alpha(cols[i % 4], (Uint8)(255.0f * (1.0f - e)));
        float scale = lerpf(1.0f, 0.3f, e), deg = spin * e + CASC_ANGLE;
        if (i % 3 == 0) geo_shape(&g, STAR_PTS, 8, p.x, p.y, 12.0f, 12.0f, scale, deg, c);
        else            geo_diamond(&g, p.x, p.y, 12.0f * scale, deg, c);
    }
    geo_flush(&g, r);
}

/* Places du décor pour un titre : hors des zones de lecture (cascade, fiche, onglets, aides), espacées */
static bool deco_blocked(float x, float y) {
    if (x < 480.0f && y > 80.0f && y < 480.0f) return true;   /* cascade */
    if (x > 560.0f && y > 370.0f) return true;                /* fiche */
    if (x > 640.0f && y < 80.0f) return true;                 /* onglets */
    return y > 500.0f;                                        /* aides de boutons */
}

static void deco_places(const char *label, float out[DECO_COUNT][2]) {
    Uint32 st = hash_str(label, "#deco");
    for (int i = 0; i < DECO_COUNT; i++) {
        float x, y;
        bool clash;
        int tries = 0;
        do {
            x = 420.0f + rng_next(&st) * 510.0f;
            y = 30.0f + rng_next(&st) * 470.0f;
            clash = deco_blocked(x, y);
            for (int j = 0; j < i && !clash; j++)
                clash = hypotf(out[j][0] - x, out[j][1] - y) < 90.0f;
        } while (++tries < 80 && clash);
        out[i][0] = x; out[i][1] = y;
    }
}

/* Décor : 5 losanges et 3 étoiles qui dérivent lentement et grossissent sur chaque temps */
static void draw_deco(const Menu *m, SDL_Renderer *r, Beat beat) {
    Geo g; g.nv = g.ni = 0;   /* sur la pile (~5 Ko) : pas d'état caché entre deux images */
    const SDL_Color conf[5] = { CH_CYAN, CH_GOLD, CH_PINK, CH_CYAN, CH_PINK };
    static const float STAR_SIZE[3] = { 24.0f, 15.0f, 19.0f };
    char label[160];
    float cur[DECO_COUNT][2], prev[DECO_COUNT][2];
    cascade_label(m, list_sel(m), label, sizeof label);
    deco_places(label, cur);
    float t = m->change_t / DECO_MOVE_S;
    bool moving = m->deco_prev >= 0 && m->deco_prev < list_count(m) && t < 1.0f;
    if (moving) {
        cascade_label(m, m->deco_prev, label, sizeof label);
        deco_places(label, prev);
    }
    float e = moving ? 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t) : 1.0f;
    for (int i = 0; i < DECO_COUNT; i++) {
        float x = cur[i][0], y = cur[i][1];
        if (moving) { x = lerpf(prev[i][0], x, e); y = lerpf(prev[i][1], y, e); }
        /* dérive en va-et-vient (7 s, 9 s à contretemps pour un élément sur deux) */
        float period = (i % 2) ? 9.0f : 7.0f, f = fmodf(m->time, 2.0f * period) / period;
        if (f > 1.0f) f = 2.0f - f;
        if (i % 2) f = 1.0f - f;
        float u = 0.5f - 0.5f * cosf(f * 3.14159265f);
        x += -16.0f * u; y += 20.0f * u;
        if (i < 5) {
            geo_diamond(&g, x + 7.0f, y + 7.0f, 14.0f * (1.0f + beat.k * 0.5f), 0.0f, conf[i]);
        } else {
            float sz = STAR_SIZE[i - 5];
            geo_shape(&g, STAR_PTS, 8, x + sz * 0.5f, y + sz * 0.5f, sz, sz, 1.0f + beat.k * 0.55f, beat.k * 25.0f, CH_GOLD);
        }
    }
    geo_flush(&g, r);
}

static void draw_cascade(const Menu *m, SDL_Renderer *r, const Font *font, Beat beat) {
    const int count = list_count(m);
    if (count == 0) return;
    const FontRot rot = { CASC_ANGLE, CASC_X, CASC_Y, 0.0f, 0.0f };
    char sel_label[160], label[160], fitted[160];
    cascade_label(m, list_sel(m), sel_label, sizeof sel_label);
    const float sel_h = selected_size(font, sel_label);
    float sel_x = CASC_X + CASC_INDENT, sel_y = 0.0f, sel_size = sel_h;   /* où se trouve le titre sélectionné */

    /* rangs visibles (au plus 8) : position, taille et poids des deux styles */
    typedef struct { int i; float x, y, size, solid, hollow; } Rank;
    Rank it[8];
    int n = 0;
    for (int i = 0; i < count && n < 8; i++) {
        /* rang continu par rapport au curseur, ramené au plus court (la liste boucle) */
        float d = fmodf((float)i - m->cursor, (float)count);
        if (d < -count * 0.5f) d += (float)count;
        else if (d >= count * 0.5f) d -= (float)count;
        float a = fabsf(d);
        if (a >= 4.0f) continue;

        /* position : interpolée entre les rangs entiers qui encadrent d */
        int lo = (int)floorf(d), hi = lo + 1;
        float t = d - (float)lo;
        float y = lerpf(lo == 0 ? 0.0f : rank_y(lo, sel_h), hi == 0 ? 0.0f : rank_y(hi, sel_h), t);
        float x = CASC_X + CASC_INDENT + d * CASC_STEP_X;
        cascade_label(m, i, label, sizeof label);
        float own = selected_size(font, label);
        float size = a < 1.0f ? lerpf(own, CASC_NEAR, a) : a < 2.0f ? lerpf(CASC_NEAR, CASC_FAR, a - 1.0f) : CASC_FAR;
        if (i == list_sel(m)) { sel_x = x; sel_y = y; sel_size = size; }

        /* sélection pleine, fondue vers le contour en s'éloignant ; voisins : contour blanc, plein jusqu'au rang 1,
           60 % au rang 2, puis s'efface */
        float solid = 1.0f - a * 1.6f;
        float hollow = a * 1.6f > 1.0f ? 1.0f : a * 1.6f;
        float fade = a <= 1.0f ? 1.0f : a <= 2.0f ? lerpf(1.0f, 0.6f, a - 1.0f)
                   : a <= 3.0f ? 0.6f * lerpf(1.0f, 0.5f, a - 2.0f) : 0.3f * (4.0f - a);
        it[n++] = (Rank){ i, x, y, size, solid, hollow * fade };
    }

    /* 1. voisins en contour (sous l'éclaboussure, comme dans la maquette) */
    for (int k = 0; k < n; k++) {
        if (it[k].hollow <= 0.0f) continue;
        cascade_label(m, it[k].i, label, sizeof label);
        float sc = it[k].size / CASC_NEAR;
        float w = (float)font_width(font, FONT_POP, label) * sc;
        if (w > CASC_OTHER_W) sc *= CASC_OTHER_W / w;
        float top = CASC_Y + it[k].y - font_cap_height(font, FONT_POP) * sc * 0.5f;
        font_draw_hollow_rot(font, r, FONT_POP, it[k].x, top, sc, rot, ui_alpha(CH_WHITE, (Uint8)(255.0f * it[k].hollow)),
                             label);
    }

    /* 2. éclaboussure et gerbe, attachées au titre sélectionné */
    float ssc = sel_size / CASC_SEL_MAX;
    ui_fit_text(font, FONT_POP_XL, sel_label, (int)(CASC_SEL_W / ssc), fitted, sizeof fitted);
    float title_w = (float)font_width(font, FONT_POP_XL, fitted) * ssc;
    draw_splash(r, sel_label, sel_x, CASC_Y + sel_y, title_w, sel_size, m->change_t, beat);
    draw_burst(r, sel_label, CASC_X + CASC_INDENT + title_w * 0.5f, CASC_Y, m->change_t);

    /* 3. titre sélectionné : jaune cerné d'encre, ombre d'encre */
    for (int k = 0; k < n; k++) {
        if (it[k].solid <= 0.0f) continue;
        cascade_label(m, it[k].i, label, sizeof label);
        float sc = it[k].size / CASC_SEL_MAX;
        int max_w = (int)(CASC_SEL_W / sc);
        ui_fit_text(font, FONT_POP_XL, label, max_w > 0 ? max_w : 1, fitted, sizeof fitted);
        float top = CASC_Y + it[k].y - font_cap_height(font, FONT_POP_XL) * sc * 0.5f;
        Uint8 al = (Uint8)(255.0f * it[k].solid);
        font_draw_ink_rot(font, r, FONT_POP_XL, it[k].x, top, sc, rot, ui_alpha(CH_GOLD, al), ui_alpha(CH_INK, al),
                          ui_alpha(CH_INK, al), 5.0f * sc, 5.0f * sc, fitted);
    }

    /* 4. bannière « NIGHT nn / NN » (« PACK » dans la liste des packs) au-dessus de l'éclaboussure, inclinée avec la
       cascade ; elle accompagne le titre pendant que la cascade glisse */
    const FontRot tr = { CASC_ANGLE, CASC_X, CASC_Y, 3.0f, 0.0f };
    char num[16], total[16];
    snprintf(num, sizeof num, "%02d", list_sel(m) + 1);
    snprintf(total, sizeof total, " / %d", count);
    const char *parts[3] = { packs_level(m) ? "PACK " : "NIGHT ", num, total };
    const SDL_Color cols[3] = { CH_GOLD, CH_WHITE, CH_GOLD };
    float w = 0.0f, pad_x, pad_y;
    for (int k = 0; k < 3; k++) w += (float)font_width_tracked(font, FONT_LABEL, parts[k], tr.tracking) + (k < 2 ? tr.tracking : 0.0f);
    splash_pads(sel_label, &pad_x, &pad_y);
    const float bh = 26.0f, bx = sel_x - 6.0f, by = CASC_Y + sel_y - sel_size * 0.5f - pad_y - 16.0f;
    ui_fill_rect_rot(r, (SDL_FRect){bx, by, w + 20.0f, bh}, tr, CH_INK);
    float tx = bx + 10.0f, ty = by + (bh - font_cap_height(font, FONT_LABEL)) * 0.5f;
    for (int k = 0; k < 3; k++) {
        font_draw_ink_rot(font, r, FONT_LABEL, tx, ty, 1.0f, tr, cols[k], UI_NONE, UI_NONE, 0, 0, parts[k]);
        tx += (float)font_width_tracked(font, FONT_LABEL, parts[k], tr.tracking) + tr.tracking;
    }
}

/* x d'un élément de la fiche à la hauteur y : le contenu suit l'inclinaison de la carte (haut top, hauteur h) */
static float card_lean_x(float y, float top, float h) { return -CARD_TAN * (y - (top + h * 0.5f)); }

/* Ligne de texte de la fiche : réduite pour tenir dans CARD_TEXT_W (shrink) ou coupée avec « … » ; cy = milieu des
   majuscules ; scale < 1 = texte plus petit que la face */
static void card_line(const Font *font, SDL_Renderer *r, FontFace face, float x, float cy, float top, float h,
                      float scale, SDL_Color c, const char *text, bool shrink) {
    char buf[200];
    float w = (float)font_width(font, face, text) * scale;
    if (shrink && w > CARD_TEXT_W) scale *= CARD_TEXT_W / w;
    if (!shrink) { ui_fit_text(font, face, text, (int)(CARD_TEXT_W / scale), buf, sizeof buf); text = buf; }
    float cap_top = cy - font_cap_height(font, face) * 0.5f;
    font_draw_ex(font, r, face, x + card_lean_x(cy, top, h), cap_top, ALIGN_LEFT, scale, c, UI_NONE, text);
}

/* Fiche d'un pack : catégorie et nombre de nuits, écart de tempo et nombre de cartes, pochettes des quatre premiers
   morceaux (vignettes décodées en arrière-plan ; cadre rose tant qu'elles ne sont pas prêtes), début de la liste */
static void draw_pack_card(const Menu *m, SDL_Renderer *r, const Font *font, float x, float top, float h, float e) {
    const MenuPack *pack = &m->packs[m->pack];
    Uint8 a = (Uint8)(255.0f * e);
    char up[160], line[200], cat[64];
    int songs[THUMB_COUNT], charts = 0;
    float bmin = 0.0f, bmax = 0.0f;
    int nights = pack_songs(m, m->pack, songs, THUMB_COUNT, &bmin, &bmax, &charts);

    pack_category(pack->name, cat, sizeof cat);
    if (cat[0]) snprintf(line, sizeof line, nights == 1 ? "%s \xC2\xB7 %d NIGHT" : "%s \xC2\xB7 %d NIGHTS", cat, nights);
    else        snprintf(line, sizeof line, nights == 1 ? "%d NIGHT" : "%d NIGHTS", nights);
    ui_upper(line, up, sizeof up);
    card_line(font, r, FONT_CARD_NAME, x, top + 31.0f, top, h, 1.0f, ui_alpha(CH_WHITE, a), up, true);

    if (bmin == bmax) snprintf(line, sizeof line, "%.0f BPM", bmin);
    else              snprintf(line, sizeof line, "%.0f\xE2\x80\x93%.0f BPM", bmin, bmax);
    snprintf(line + strlen(line), sizeof line - strlen(line), charts == 1 ? "  \xC2\xB7  %d CHART" : "  \xC2\xB7  %d CHARTS",
             charts);
    card_line(font, r, FONT_CARD, x, top + 61.0f, top, h, 1.0f, ui_alpha(CH_MUTED, (Uint8)(CH_MUTED.a * e)), line, false);

    /* pochettes */
    const float ty = top + 82.0f;
    float tx = x + card_lean_x(ty + THUMB_H * 0.5f, top, h);
    for (int k = 0; k < nights && k < THUMB_COUNT; k++, tx += THUMB_W + 6.0f) {
        SDL_FRect rc = { tx, ty, THUMB_W, THUMB_H };
        ui_panel(r, (SDL_FRect){rc.x - 2.0f, rc.y - 2.0f, rc.w + 4.0f, rc.h + 4.0f}, 5.0f, ui_alpha(CH_WHITE, (Uint8)(64.0f * e)),
                 UI_NONE, 0.0f);
        const Background *th = NULL;
        char path[BGCACHE_PATH];
        if (m->entries[songs[k]].background[0]) {
            snprintf(path, sizeof path, "%s", m->entries[songs[k]].background);
            th = bgcache_peek(&m->thumbs, path);
        }
        if (th && th->tex) {
            SDL_SetTextureAlphaMod(th->tex, a);
            SDL_RenderCopyF(r, th->tex, NULL, &rc);
        } else {
            ui_skew_box(r, rc, 0.0f, ui_alpha(CH_PINK, (Uint8)(150.0f * e)));
        }
    }

    /* début de la liste des morceaux */
    line[0] = '\0';
    for (int k = 0; k < nights && k < 3; k++)
        snprintf(line + strlen(line), sizeof line - strlen(line), "%s%s", k ? " \xC2\xB7 " : "", m->entries[songs[k]].title);
    if (nights > 3) snprintf(line + strlen(line), sizeof line - strlen(line), " \xE2\x80\xA6");
    card_line(font, r, FONT_CARD, x, top + 141.0f, top, h, 0.8f, ui_alpha(CH_WHITE, (Uint8)(178.0f * e)), line, false);
}

/* Fiche : carte d'encre penchée qui entre par la droite. Morceau : artiste, BPM · notes · record, difficultés en
   pastilles (sélectionnée pleine, cyan). Liste des packs : draw_pack_card. */
static void draw_card(const Menu *m, SDL_Renderer *r, const Font *font) {
    float t = 1.0f - m->info_t, e = 1.0f - t * t * t;
    float off = (1.0f - e) * CARD_SLIDE;
    Uint8 a = (Uint8)(255.0f * e);
    const float h = packs_level(m) ? CARD_H_PACK : CARD_H_SONG, top = CARD_BOTTOM - h;
    const float lean = h * 0.5f * CARD_TAN;
    ui_skew_box(r, (SDL_FRect){CARD_X + off + 8.0f, top + 8.0f, CARD_W, h}, lean, ui_alpha(CH_PINK, a));
    ui_skew_box(r, (SDL_FRect){CARD_X + off, top, CARD_W, h}, lean, ui_alpha(CH_INK, a));

    const float x = CARD_X + off + CARD_PAD;
    if (packs_level(m)) { draw_pack_card(m, r, font, x, top, h, e); return; }

    char up[160], line[160];
    const MenuSong *sg = &m->songs[m->song];
    const MenuEntry *en = &m->entries[sg->first + m->diff];
    ui_upper(en->artist[0] ? en->artist : "UNKNOWN ARTIST", up, sizeof up);
    card_line(font, r, FONT_CARD_NAME, x, top + 31.0f, top, h, 1.0f, ui_alpha(CH_WHITE, a), up, true);

    char best[32] = "\xE2\x80\x94 \xE2\x80\x94";
    if (en->best_score > 0) ui_format_score(en->best_score, best, sizeof best);
    snprintf(line, sizeof line, "%.0f BPM  \xC2\xB7  %d NOTES  \xC2\xB7  BEST %s", en->bpm, en->notes, best);
    card_line(font, r, FONT_CARD, x, top + 61.0f, top, h, 1.0f, ui_alpha(CH_MUTED, (Uint8)(CH_MUTED.a * e)), line, false);

    /* pastilles de difficulté ; trop nombreuses pour CARD_PILLS_W : une fenêtre qui suit la sélection, flèches de côté */
    const float cy = top + 95.0f, ph = 24.0f, gap = 6.0f, arrow_w = 14.0f;
    float px = x + card_lean_x(cy, top, h), widths[MENU_PILLS_MAX], total = 0.0f;
    const int count = sg->count < MENU_PILLS_MAX ? sg->count : MENU_PILLS_MAX;
    for (int i = 0; i < count; i++) {
        ui_upper(m->entries[sg->first + i].difficulty, up, sizeof up);
        widths[i] = (float)font_width(font, FONT_CARD_BOLD, up) + 22.0f;
        total += widths[i] + (i ? gap : 0.0f);
    }
    int first = 0, last = count - 1;
    if (total > CARD_PILLS_W) {
        const float room = CARD_PILLS_W - 2.0f * arrow_w;
        int sel = m->diff < count ? m->diff : count - 1;
        float used = widths[sel];
        first = last = sel;
        for (bool grew = true; grew; ) {   /* élargit autour de la sélection, à droite d'abord */
            grew = false;
            if (last + 1 < count && used + gap + widths[last + 1] <= room) { used += gap + widths[++last]; grew = true; }
            if (first > 0 && used + gap + widths[first - 1] <= room) { used += gap + widths[--first]; grew = true; }
        }
        SDL_Color ac = ui_alpha(CH_WHITE, (Uint8)(184.0f * e));
        if (first > 0) {     /* d'autres difficultés à gauche */
            const float ax = px + arrow_w * 0.5f;
            ui_poly(r, (const SDL_FPoint[]){{ax - 4.0f, cy}, {ax + 3.0f, cy - 6.0f}, {ax + 3.0f, cy + 6.0f}}, 3, ac);
        }
        if (last < count - 1) {   /* … et à droite */
            const float ax = px + arrow_w * 1.5f + used;
            ui_poly(r, (const SDL_FPoint[]){{ax + 4.0f, cy}, {ax - 3.0f, cy - 6.0f}, {ax - 3.0f, cy + 6.0f}}, 3, ac);
        }
        px += arrow_w;
    }
    for (int i = first; i <= last; i++) {
        ui_upper(m->entries[sg->first + i].difficulty, up, sizeof up);
        float w = widths[i];
        bool sel = (i == m->diff);
        if (sel) ui_panel(r, (SDL_FRect){px, cy - ph * 0.5f, w, ph}, ph * 0.5f, ui_alpha(CH_CYAN, a), UI_NONE, 0.0f);
        SDL_Color tc = sel ? ui_alpha(CH_INK, a) : ui_alpha(CH_WHITE, (Uint8)(184.0f * e));
        font_draw(font, r, FONT_CARD_BOLD, px + w * 0.5f, cy - font_cap_height(font, FONT_CARD_BOLD) * 0.5f, ALIGN_CENTER,
                  tc, up);
        px += w + gap;
    }
}

/* Pavé d'encre avec un texte en espacé, aligné à droite sur `right` ; retourne sa largeur */
static float tag_width(const Font *font, FontFace face, float tracking, float pad, const char *text) {
    return (float)font_width_tracked(font, face, text, tracking) + tracking + 2.0f * pad;
}

static void draw_tag(SDL_Renderer *r, const Font *font, FontFace face, float tracking, float pad, float x, float y, float h,
                     SDL_Color bg, SDL_Color fg, const char *text) {
    float w = tag_width(font, face, tracking, pad, text);
    ui_skew_box(r, (SDL_FRect){x, y, w, h}, 0.0f, bg);
    const FontRot flat = { 0.0f, 0.0f, 0.0f, tracking, 0.0f };
    font_draw_ink_rot(font, r, face, x + pad, y + (h - font_cap_height(font, face)) * 0.5f, 1.0f, flat, fg, UI_NONE,
                      UI_NONE, 0, 0, text);
}

/* Onglets ALL SONGS / PACKS en haut à droite (onglet actif rose), et dessous le pack ouvert ou « SELECT PACK » */
static void draw_tabs(const Menu *m, SDL_Renderer *r, const Font *font) {
    static const char *LABELS[MENU_TAB_COUNT] = { "ALL SONGS", "PACKS" };
    const float right = SCREEN_W - 24.0f;
    float x = right;
    for (int i = 0; i < MENU_TAB_COUNT; i++)
        if (tab_available(m, (MenuTab)i)) x -= tag_width(font, FONT_POP_S, 2.0f, 12.0f, LABELS[i]);
    for (int i = 0; i < MENU_TAB_COUNT; i++) {
        if (!tab_available(m, (MenuTab)i)) continue;
        bool on = ((int)m->tab == i);
        draw_tag(r, font, FONT_POP_S, 2.0f, 12.0f, x, 18.0f, 34.0f, on ? CH_PINK : ui_alpha(CH_INK, 204), CH_WHITE,
                 LABELS[i]);
        x += tag_width(font, FONT_POP_S, 2.0f, 12.0f, LABELS[i]);
    }
    char up[96];
    if (m->in_pack) ui_upper(m->packs[m->pack].name[0] ? m->packs[m->pack].name : "SINGLES", up, sizeof up);
    else if (packs_level(m)) snprintf(up, sizeof up, "SELECT PACK");
    else return;
    float w = tag_width(font, FONT_LABEL, 1.0f, 12.0f, up);
    draw_tag(r, font, FONT_LABEL, 1.0f, 12.0f, right - w, 52.0f, 26.0f, ui_alpha(CH_INK, 204),
             ui_alpha(CH_WHITE, 230), up);
}

/* Fenêtre « DELETE THIS SONG? » par-dessus le menu assombri */
static void draw_delete_confirm(const Menu *m, SDL_Renderer *r, const Font *font, const Sprites *sprites) {
    SDL_SetRenderDrawColor(r, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, 190);
    SDL_RenderFillRect(r, NULL);
    const SDL_FRect box = { SCREEN_W * 0.5f - 230.0f, SCREEN_H * 0.5f - 90.0f, 460.0f, 180.0f };
    ui_panel(r, box, 16.0f, CH_INK, ui_alpha(CH_WHITE, 102), 0.0f);
    const MenuEntry *e = &m->entries[m->songs[m->song].first];
    font_draw_ink(font, r, FONT_POP, SCREEN_W * 0.5f, box.y + 26.0f, ALIGN_CENTER, 1.0f, CH_WHITE, CH_INK, CH_PINK,
                  3.0f, 3.0f, "DELETE THIS SONG?");
    char title[64];
    ui_fit_text(font, FONT_CARD_NAME, e->title, (int)box.w - 40, title, sizeof title);
    font_draw(font, r, FONT_CARD_NAME, SCREEN_W * 0.5f, box.y + 82.0f, ALIGN_CENTER, CH_WHITE, title);
    const float hy = box.y + box.h - 30.0f;
    float w = ui_button_hint_pop_width(font, (const Button[]){BTN_CROSS}, 1, "DELETE") + 24.0f
            + ui_button_hint_pop_width(font, (const Button[]){BTN_CIRCLE}, 1, "CANCEL");
    float x = SCREEN_W * 0.5f - w * 0.5f;
    x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CROSS}, 1, "DELETE") + 24.0f;
    ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CIRCLE}, 1, "CANCEL");
}

void menu_draw(const Menu *m, SDL_Renderer *r, const Font *font, const Sprites *sprites) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, CH_NIGHT.r, CH_NIGHT.g, CH_NIGHT.b, 255);
    SDL_RenderClear(r);
    if (m->bg) {   /* image du morceau en bichromie (niveaux de gris au décodage + dégradé en multiply) */
        background_draw(m->bg, r);
        ui_duotone(r);
    }
    ui_veil_left(r);
    const Beat beat = menu_beat(m);
    if (beat.kd > 0.01f) {   /* temps fort : léger éclair rose (en additif, proche du « screen » de la maquette) */
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(r, CH_PINK.r, CH_PINK.g, CH_PINK.b, (Uint8)(beat.kd * 0.14f * 255.0f));
        SDL_RenderFillRect(r, NULL);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    }

    if (m->entry_count == 0) {
        font_draw_ink(font, r, FONT_POP, SCREEN_W / 2, SCREEN_H / 2 - 14, ALIGN_CENTER, 1.0f, CH_WHITE, CH_INK, UI_NONE,
                      0, 0, "NO SONGS FOUND");
        return;
    }
    if (list_count(m) > 0) draw_deco(m, r, beat);
    if (tabs_shown(m)) draw_tabs(m, r, font);
    draw_cascade(m, r, font, beat);
    draw_card(m, r, font);

    const float hy = SCREEN_H - 26.0f, gap = 18.0f;
    float x = 34.0f;
    if (packs_level(m)) {
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_UP, BTN_DOWN}, 2, "PACK") + gap;
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CROSS}, 1, "OPEN") + gap;
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CIRCLE}, 1, "BACK") + gap;
    } else {
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_UP, BTN_DOWN}, 2, "SONG") + gap;
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_LEFT, BTN_RIGHT}, 2, "DIFFICULTY") + gap;
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CROSS}, 1, "PLAY") + gap;
        x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_CIRCLE}, 1, "BACK") + gap;
        if (m->song_count && m->entries[m->songs[m->song].first].imported)
            x = ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_TRIANGLE}, 1, "DELETE") + gap;
    }
    if (tabs_shown(m)) ui_button_hint_pop(r, font, sprites, x, hy, (const Button[]){BTN_L, BTN_R}, 2, "TAB");
    if (m->confirm_delete) draw_delete_confirm(m, r, font, sprites);
}
