#ifndef NET_H
#define NET_H
#include <stdbool.h>
#include "sdl.h"

/* Accès réseau de la bibliothèque de chansons (library.h). HTTPS par libcurl + OpenSSL sur Vita ; sur desktop, libcurl
   quand CMake l'a trouvée (HAVE_CURL), sinon rien (les paquets distribués n'embarquent pas libcurl).
   Les fonctions bloquent : à appeler depuis un fil d'exécution, jamais depuis la boucle de jeu. */

/* Journal du réseau (Vita : SAVE_DIR net_log.txt, réécrit à chaque lancement ; ailleurs : rien) */
void net_log(const char *fmt, ...);

/* Vrai si une requête peut aboutir : Vita connectée au Wi-Fi, ou desktop avec libcurl */
bool net_online(void);

/* GET en HTTPS. Retourne le corps dans un tampon alloué (à libérer avec free), terminé par un octet nul, et sa taille
   dans *len ; NULL en cas d'échec ou si la réponse dépasse max_bytes. Sur Vita, un certificat que la console ne sait pas
   contrôler (horloge fausse) est accepté au second essai : seules des données publiques en sont lues, vérifiées
   ensuite (forme du JSON, empreinte SHA-256 des zips). */
char *net_get(const char *url, int max_bytes, int *len);

/* Avancement d'un téléchargement : écrit par le fil qui télécharge, lu par l'affichage ; cancel mis à 1 pour l'interrompre */
typedef struct { SDL_atomic_t done, total, cancel; } NetProgress;

/* Télécharge `url` dans le fichier `path` (créé ou écrasé ; supprimé en cas d'échec ou d'annulation). max_bytes plafonne
   la taille reçue. Retourne true si le fichier est complet. */
bool net_download(const char *url, const char *path, int max_bytes, NetProgress *progress);

#endif
