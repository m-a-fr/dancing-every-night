#!/usr/bin/env bash
# Première installation : envoie le .vpk dans ux0:data/ ; l'installer ensuite depuis VitaShell.
# À refaire seulement si tu changes le TITLEID, le nom, l'icône ou la LiveArea.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f .env ] && set -a && . ./.env && set +a
: "${VITA_IP:?Définis VITA_IP (fichier .env ou export VITA_IP=...)}"

curl -sS --ftp-method nocwd -T build-vita/vitagame.vpk "ftp://$VITA_IP:1337/ux0:/data/"
echo "==> vitagame.vpk envoyé dans ux0:data/"
echo "    Sur la Vita : VitaShell > ux0:data/vitagame.vpk > Croix pour installer."
