#!/usr/bin/env bash
# Version en ligne de l'éditeur de cartes (Cloudflare Pages) : site statique dans build-site/.
# L'éditeur y tourne sans serveur Python (tools/editor/local.js) : maps gardées dans le navigateur, entrée et
# sortie par .zip. Pour l'essayer en local : make editor-site, puis http://localhost:8766 (éditeur sur /editor/)
set -euo pipefail
cd "$(dirname "$0")/.."

# Plan du site : /  accueil · /songs/  bibliothèque · /guide.html  guide du créateur · /editor/  éditeur (ses fichiers y sont
# regroupés, chemins relatifs inchangés) · /api/submit  envoi depuis l'éditeur (functions/, pas dans ce dossier)
OUT=build-site
rm -rf "$OUT"
mkdir -p "$OUT/fonts" "$OUT/assets/ui" "$OUT/editor/vendor" "$OUT/editor/fonts"
cp tools/editor/guide.html tools/editor/guide.fr.html tools/editor/guide.css "$OUT/"   # guide du créateur (EN, FR)
cp tools/editor/fonts/* "$OUT/fonts/"   # polices de la charte (tools/editor_fonts.py)
cp assets/ui/pad_icons.png "$OUT/assets/ui/"   # mêmes icônes de touches que le jeu
cp sce_sys/icon0.png "$OUT/favicon.png"         # icône du jeu (celle de la Vita)
cp release/latest.json "$OUT/"                  # fiche de la dernière version, lue par le jeu (src/update.c)
cp tools/editor/social.jpg "$OUT/"              # aperçu quand le lien est partagé (écran titre du jeu)
E="$OUT/editor"
cp tools/editor/index.html tools/editor/i18n.js tools/editor/local.js tools/editor/tempo.js tools/editor/mapper-worker.js "$E/"
cp tools/mapper.py "$E/"   # détection du BPM et brouillon automatique, exécutés par Pyodide
cp -R tools/editor/vendor/. "$E/vendor/"
cp tools/editor/fonts/* "$E/fonts/"
cp sce_sys/icon0.png "$E/favicon.png"

# Accueil (/) et bibliothèque de chansons (library/catalog.json) : pages statiques dans songs/ ; LIBRARY_DEMO=1 : catalogue fictif, pour l'essayer
python3 tools/library_site.py "$OUT" $([ "${LIBRARY_DEMO:-0}" = 1 ] && echo --demo)

# En-têtes Cloudflare Pages : isolation cross-origin (SharedArrayBuffer, pour ffmpeg.wasm multithread)
cat > "$OUT/_headers" <<'HEADERS'
/*
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp
HEADERS
cat >> "$OUT/_headers" <<'HEADERS'
/latest.json
  Cache-Control: no-cache
HEADERS

echo "==> Site de l'éditeur : $OUT/ ($(find "$OUT" -type f | wc -l | tr -d ' ') fichiers, $(du -sh "$OUT" | cut -f1))"
