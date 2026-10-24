#!/usr/bin/env bash
# Audio des chansons officielles : il n'est PAS dans le dépôt public (les labels autorisent la distribution du fichier
# dans le jeu, pas seul). Il vit dans le dépôt privé m-a-fr/dne-official-audio, lu par la CI avec une clé de déploiement
# en lecture seule (secret OFFICIAL_AUDIO_KEY). En local, les fichiers sont dans assets/songs/<ID>/audio.ogg (ignorés par git).
#
#   official-audio.sh stage <assets>   copie les audio présents dans le dossier d'assets d'un build de release
#                                      (make release, scripts/build-pc.sh) ; REQUIRE_AUDIO=1 : échec s'il en manque
#   official-audio.sh place <dossier>  CI : dépose <dossier>/<ID>/audio.ogg dans assets/songs/<ID>/ avant le build
#
# Sans l'audio, le jeu tourne : ces chansons sont muettes (métronome seul), et make check signale le fichier manquant.
set -euo pipefail
cd "$(dirname "$0")/.."

case "${1:-}" in
  stage)
    dest="${2:?usage : $0 stage <assets>}"; missing=0
    for id in $(git ls-files 'assets/songs/*/*.chart' | cut -d/ -f3 | sort -u); do
      src="assets/songs/$id/audio.ogg"
      if [ -f "$src" ]; then
        mkdir -p "$dest/songs/$id" && cp "$src" "$dest/songs/$id/audio.ogg"
      else
        echo "==> ATTENTION : $src absent : la chanson $id sera muette (métronome seul)"; missing=$((missing + 1))
      fi
    done
    if [ "$missing" -gt 0 ] && [ "${REQUIRE_AUDIO:-0}" = 1 ]; then echo "==> audio officiel manquant : build refusé"; exit 1; fi ;;
  place)
    from="${2:?usage : $0 place <dossier>}"; n=0
    for f in "$from"/*/audio.ogg; do
      [ -f "$f" ] || continue
      id="$(basename "$(dirname "$f")")"
      mkdir -p "assets/songs/$id" && cp "$f" "assets/songs/$id/audio.ogg"; n=$((n + 1))
    done
    echo "==> audio officiel : $n fichier(s) déposé(s)"; [ "$n" -gt 0 ] ;;
  *) echo "usage : $0 stage <assets> | place <dossier>"; exit 2 ;;
esac
