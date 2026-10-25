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
if vdpm install --force openssl curl zlib zstd; then :
elif vdpm openssl curl zlib zstd; then :   # ancienne syntaxe de vdpm
else echo "==> vdpm n'a pas pu mettre à jour les bibliothèques"; fi
strings "$LIB/libcrypto.a" 2>/dev/null | grep -m1 -E '^OpenSSL [0-9]' || true
echo "==> Bibliothèques réseau après : $(ls "$LIB" | grep -E '^lib(curl|ssl|crypto|zstd|z)\.a$' | tr '\n' ' ')"
