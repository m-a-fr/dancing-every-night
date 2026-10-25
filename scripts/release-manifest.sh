#!/usr/bin/env bash
# Fiche de la dernière version (release/latest.json, publiée avec le site sur /latest.json) lue par le jeu au démarrage
# pour annoncer une mise à jour (src/update.c). À lancer APRÈS avoir publié la release, puis commiter et pousser :
#   scripts/release-manifest.sh DancingEveryNight-0.9.1.vpk "New: ..." "Nouveau : ..."
# La version vient de CMakeLists.txt. Le .vpk est celui de la release publiée (empreinte SHA-256 et taille).
set -euo pipefail
cd "$(dirname "$0")/.."
VPK="${1:?usage: release-manifest.sh fichier.vpk \"notes en anglais\" \"notes en français\"}"
NOTES_EN="${2:?notes en anglais (une phrase)}"
NOTES_FR="${3:?notes en français (une phrase)}"
VERSION=$(sed -n 's/^set(GAME_VERSION *"\(.*\)").*/\1/p' CMakeLists.txt)
python3 - "$VPK" "$VERSION" "$NOTES_EN" "$NOTES_FR" <<'PY'
import hashlib, json, sys, datetime
from pathlib import Path
vpk, version, en, fr = sys.argv[1:5]
data = Path(vpk).read_bytes()
tag = f"v{version}"
repo = "https://github.com/m-a-fr/dancing-every-night"
manifest = {
    "version": version,
    "date": datetime.date.today().isoformat(),
    "url": f"{repo}/releases/tag/{tag}",
    "notes_en": en, "notes_fr": fr,
    "vpk_url": f"{repo}/releases/download/{tag}/{Path(vpk).name}",
    "sha256": hashlib.sha256(data).hexdigest(),
    "size": len(data),
}
Path("release/latest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(manifest, ensure_ascii=False, indent=2))
PY
