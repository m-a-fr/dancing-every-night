#!/usr/bin/env bash
# Met à niveau, dans l'image Docker vitasdk/vitasdk (CI), les bibliothèques de l'annonce des mises à jour (src/update.c) :
# libcurl, OpenSSL, zlib, zstd. Celles de l'image ne vont pas ensemble (libcurl demande des symboles que son OpenSSL n'a
# pas) ; la VitaSDK de développement a des versions cohérentes (curl 8.17, OpenSSL 1.0.2, zlib 1.3.2, zstd 1.5.7).
# Lancé par scripts/build-vita.sh dans le conteneur ; sans effet utile en dehors.
set -uo pipefail
: "${VITASDK:?VITASDK non défini}"
LIB="$VITASDK/arm-vita-eabi/lib"
echo "==> Bibliothèques réseau avant : $(ls "$LIB" | grep -E '^lib(curl|ssl|crypto|zstd|z)\.a$' | tr '\n' ' ')"
strings "$LIB/libcrypto.a" 2>/dev/null | grep -m1 -E '^OpenSSL [0-9]' || true
# L'image a libcurl 8.17 (écrite pour OpenSSL 1.0.2) mais OpenSSL 1.1.1 : les deux paquets openssl sont en conflit, on
# remplace le second par le premier, comme sur la VitaSDK de développement.
# Cette image change : si sa libcurl demande déjà l'API d'OpenSSL 1.1 (OPENSSL_sk_num…), ses paquets vont ensemble et on
# n'y touche pas (l'échange a fait échouer l'édition de liens sur OPENSSL_sk_num le 2026-11-01).
# (sortie capturée avant le grep : avec pipefail, « nm | grep -q » échoue par SIGPIPE même quand le symbole est là)
CURL_SYMS=$("$VITASDK/bin/arm-vita-eabi-nm" "$LIB/libcurl.a" 2>/dev/null || true)
echo "==> symboles libcurl lus : $(printf '%s' "$CURL_SYMS" | wc -l) lignes"
if grep -q -E ' U (OPENSSL_sk_num|OpenSSL_version_num)' <<<"$CURL_SYMS"; then
  echo "==> libcurl de l'image vise OpenSSL 1.1 : on garde ses bibliothèques"
else
  # -Rdd : sans contrôle des dépendances (cpython, boost, libzip… en demandent un, mais le conteneur est jetable et n'en a pas besoin)
  vdpm pacman -- -Rdd --noconfirm openssl-1.1.1 || true
  yes | vdpm install --force openssl || echo "==> vdpm n'a pas pu installer openssl 1.0.2"
fi
strings "$LIB/libcrypto.a" 2>/dev/null | grep -m1 -E '^OpenSSL [0-9]' || true
echo "==> Bibliothèques réseau après : $(ls "$LIB" | grep -E '^lib(curl|ssl|crypto|zstd|z)\.a$' | tr '\n' ' ')"
