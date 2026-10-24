#!/usr/bin/env bash
# Envoie le nouvel eboot.bin (+ assets) sur la Vita et relance le jeu, via vitacompanion.
# Prérequis : le .vpk a été installé UNE fois (scripts/install-vpk.sh) et vitacompanion tourne.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f .env ] && set -a && . ./.env && set +a

: "${VITA_IP:?Définis VITA_IP (fichier .env ou export VITA_IP=...)}"
TITLEID=$(sed -n 's/.*set(VITA_TITLEID *"\([A-Z0-9]*\)").*/\1/p' CMakeLists.txt)
FTP="ftp://$VITA_IP:1337"
CMD_PORT=1338

send_cmd() { echo "$1" | nc -w 2 "$VITA_IP" "$CMD_PORT" >/dev/null 2>&1 || true; }

echo "==> Fermeture de l'app en cours"
send_cmd "destroy"
sleep 1

echo "==> Envoi eboot.bin -> ux0:app/$TITLEID/"
curl -sS --ftp-method nocwd -T build-vita/eboot.bin "$FTP/ux0:/app/$TITLEID/"

if [ -d assets ]; then
  echo "==> Synchro des assets"
  find assets -type f ! -name '.gitkeep' | while read -r f; do
    curl -sS --ftp-method nocwd --ftp-create-dirs -T "$f" "$FTP/ux0:/app/$TITLEID/$f"
  done
fi

echo "==> Lancement de $TITLEID"
send_cmd "launch $TITLEID"
echo "==> Déployé."
