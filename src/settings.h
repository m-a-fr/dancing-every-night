#ifndef SETTINGS_H
#define SETTINGS_H

/* Réglages du joueur, gardés d'une session à l'autre dans SAVE_DIR "settings.txt" (une ligne « CLÉ valeur » par
   réglage ; les clés inconnues sont ignorées, les absentes gardent leur valeur par défaut). */

#define SETTINGS_OFFSET_MIN  -300
#define SETTINGS_OFFSET_MAX   300

typedef struct {
    int audio_offset_ms;   /* calibration de latence (Audio.calib_ms) : < 0 = compense un son entendu en retard */
} Settings;

/* Vita : au premier lancement d'une version qui a changé de dossier de sauvegarde, recopie les réglages et
   les scores de l'ancien dossier (LEGACY_SAVE_DIR). Sans effet ailleurs ou si le nouveau dossier est déjà rempli. */
void settings_migrate_legacy(void);
/* Valeurs par défaut, puis celles du fichier s'il existe */
void settings_load(Settings *s);
void settings_save(const Settings *s);
/* Borne un décalage dans [SETTINGS_OFFSET_MIN, SETTINGS_OFFSET_MAX] */
int  settings_clamp_offset(int ms);

#endif
