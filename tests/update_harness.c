/* Essais de src/update.c (comparaison de versions et lecture de la fiche) : compilé par tests/test_update.py */
#include "../src/update.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(update_version_compare("0.9.0", "0.9.0") == 0);
    assert(update_version_compare("0.9.1", "0.9.0") > 0);
    assert(update_version_compare("0.10.0", "0.9.9") > 0);   /* numérique, pas alphabétique */
    assert(update_version_compare("1.0", "0.9.9") > 0);
    assert(update_version_compare("0.9", "0.9.0") == 0);
    assert(update_version_compare("0.8.9", "0.9.0") < 0);

    Update u;
    memset(&u, 0, sizeof u);
    const char *newer = "{\n \"version\": \"0.9.1\",\n \"notes_en\": \"Fixes the \\\"menu\\\".\\nNew songs.\",\n \"url\": \"https://example.test/r\"\n}";
    assert(update_parse(newer, "0.9.0", &u));
    assert(!strcmp(u.version, "0.9.1"));
    assert(!strcmp(u.notes, "Fixes the \"menu\". New songs."));
    assert(!strcmp(u.url, "https://example.test/r"));
    assert(!update_parse(newer, "0.9.1", &u));                 /* déjà à jour */
    assert(!update_parse(newer, "1.0.0", &u));                 /* plus récent que la fiche */
    assert(!update_parse("{\"version\": \"0.9.1 rc<b>\"}", "0.9.0", &u));   /* version non numérique refusée */
    assert(!update_parse("pas du json", "0.9.0", &u));
    assert(!update_parse("{\"version\": \"\"}", "0.9.0", &u));
    assert(!update_parse("{\"version\": \"0.9.1", "0.9.0", &u));   /* fiche tronquée */
    /* champs absents : version annoncée, textes vides */
    assert(update_parse("{\"version\":\"0.9.2\"}", "0.9.0", &u) && !u.notes[0] && !u.url[0]);
    /* un texte trop long est coupé sans déborder */
    char big[1024] = "{\"version\":\"0.9.3\",\"notes_en\":\"";
    for (int i = 0; i < 500; i++) strcat(big, "x");
    strcat(big, "\"}");
    assert(update_parse(big, "0.9.0", &u) && strlen(u.notes) == sizeof u.notes - 1);
    puts("update ok");
    return 0;
}
