#ifndef NET_H
#define NET_H
#include <stdbool.h>
#include <stddef.h>
#include "sdl.h"

/* Accès réseau de la bibliothèque de chansons (library.h), HTTPS seulement. Moteur selon le système (voir net.c) :
   libcurl + OpenSSL sur Vita, libcurl du système sur macOS, libcurl chargée à l'exécution sur Linux, WinHTTP sur Windows.
   Les fonctions bloquent : à appeler depuis un fil d'exécution, jamais depuis la boucle de jeu. */

/* Ce système sait-il faire des requêtes ? Faux sur Linux sans libcurl, ou dans une compilation sans réseau (GAME_NET=OFF) */
bool net_supported(void);

/* Journal du réseau (Vita : SAVE_DIR net_log.txt, réécrit à chaque lancement ; ailleurs : rien) */
void net_log(const char *fmt, ...);

/* Vrai si une requête peut aboutir : Vita connectée au Wi-Fi ; sur PC, vrai si le moteur est là (la connexion
   elle-même ne se teste qu'en essayant) */
bool net_online(void);

/* GET en HTTPS. Retourne le corps dans un tampon alloué (à libérer avec free), terminé par un octet nul, et sa taille
   dans *len ; NULL en cas d'échec ou si la réponse dépasse max_bytes. Sur Vita, un certificat que la console ne sait pas
   contrôler (horloge fausse) est accepté au second essai : seules des données publiques en sont lues, vérifiées
   ensuite (forme du JSON, empreinte SHA-256 des zips). */
char *net_get(const char *url, int max_bytes, int *len);

/* Interface des moteurs (net.c) : réception par morceaux, rappel qui retourne false pour interrompre */
typedef bool (*NetWriteFn)(const void *data, size_t n, void *user);
typedef struct { int status; char why[120]; } NetResult;   /* code HTTP et raison lisible (journal de la Vita) */

/* Avancement d'un téléchargement : écrit par le fil qui télécharge, lu par l'affichage ; cancel mis à 1 pour l'interrompre */
typedef struct { SDL_atomic_t done, total, cancel; } NetProgress;

/* Télécharge `url` dans le fichier `path` (créé ou écrasé ; supprimé en cas d'échec ou d'annulation). max_bytes plafonne
   la taille reçue. Retourne true si le fichier est complet. */
bool net_download(const char *url, const char *path, int max_bytes, NetProgress *progress);

#endif
