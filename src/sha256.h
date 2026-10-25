#ifndef SHA256_H
#define SHA256_H
#include <stdbool.h>
#include <stddef.h>

/* Empreinte SHA-256 d'un fichier, en hexadécimal minuscule (65 octets avec le zéro final). false si le fichier est
   illisible. Sert à vérifier un zip téléchargé contre le catalogue. */
bool sha256_file(const char *path, char out[65]);
/* Idem sur un tampon en mémoire (tests) */
void sha256_buffer(const void *data, size_t len, char out[65]);

#endif
