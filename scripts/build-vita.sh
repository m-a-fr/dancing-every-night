#!/usr/bin/env bash
# Compile le .vpk et l'eboot.bin pour PS Vita.
# Utilise VitaSDK local si $VITASDK est défini, sinon l'image Docker officielle.
#   (défaut)   build de dev dans build-vita/ : embarque tout assets/, morceaux personnels compris
#   RELEASE=1  build de release dans build-release/ : n'embarque que les assets suivis par git,
#              puis copie le .vpk dans dist/<nom>-<version>.vpk
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=build-vita
ASSETS_DIR=assets
if [ "${RELEASE:-0}" = 1 ]; then
  BUILD_DIR=build-release
  ASSETS_DIR=build-release/assets
  # Seuls les fichiers suivis par git partent dans la release : les morceaux personnels (ignorés) restent dehors
  if [ -n "$(git status --porcelain -- assets)" ]; then
    echo "==> Attention : assets/ a des changements non commités ; la release n'embarque que la version commitée"
  fi
  rm -rf "$ASSETS_DIR"
  mkdir -p "$BUILD_DIR"
  git archive --format=tar HEAD assets | tar -x -C "$BUILD_DIR"
  ./scripts/official-audio.sh stage "$ASSETS_DIR"   # l'audio des chansons officielles n'est pas dans le dépôt (voir le script)
  mkdir -p "$ASSETS_DIR/songs"    # maps officielles : un dossier par map (vide tant qu'il n'y en a pas)
  echo "==> Release : $(find "$ASSETS_DIR" -type f | wc -l | tr -d ' ') fichiers d'assets ($(du -sh "$ASSETS_DIR" | cut -f1))"
fi

# VITA_PAD_BYTES : remplissage ajouté au binaire (voir plus bas et CMakeLists.txt)
build_cmd() {
  echo "cmake -S . -B $BUILD_DIR -DBUILD_VITA=ON -DCMAKE_BUILD_TYPE=Release -DVITA_PAD_BYTES=$1 -DGAME_ASSETS_DIR=$ASSETS_DIR && cmake --build $BUILD_DIR -j"
}

# vita.cmake (fourni par VitaSDK) assemble les chemins du VPK dans une seule
# chaîne puis les redécoupe sur les espaces : un chemin de projet contenant
# un espace casse l'étape de packaging (vita_create_vpk). Contournement :
# builder via un lien symbolique vers un chemin sans espace ; le contenu
# réel (build-vita/, vpk, eboot) reste dans le dossier du projet.
project_dir="$(pwd)"
if [[ "$project_dir" == *' '* ]]; then
  link_dir="$HOME/.cache/vitagame-src-$(basename "$project_dir" | tr -c 'A-Za-z0-9' '-')"
  mkdir -p "$(dirname "$link_dir")"
  ln -sfn "$project_dir" "$link_dir"
  cd "$link_dir"
fi

# VitaSDK installé à l'emplacement habituel mais variable non définie (nouveau terminal, make...) : on le prend
if [ -z "${VITASDK:-}" ] && [ -x "$HOME/vitasdk/bin/arm-vita-eabi-gcc" ]; then
  export VITASDK="$HOME/vitasdk"
  export PATH="$VITASDK/bin:$PATH"
fi

run_build() {
  local cmd; cmd="$(build_cmd "$1")"
  if [ -n "${VITASDK:-}" ] && [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ]; then
    echo "==> VitaSDK local ($VITASDK)"
    bash -c "$cmd"
  else
    echo "==> VitaSDK via Docker (vitasdk/vitasdk)"
    local user=(-u "$(id -u):$(id -g)")
    if [ -n "${VITA_SDL2:-}" ]; then
      # SDL2 de la VitaSDK de développement (rendu GXM) à la place de celle de l'image : voir scripts/vita-sdl2.sh.
      # En root, pour écrire dans la VitaSDK de l'image (conteneur jetable)
      cmd="bash scripts/vita-sdl2.sh $VITA_SDL2 && $cmd"
      user=()
    fi
    docker run --rm --platform linux/amd64 \
      -v "$PWD":/src -w /src \
      "${user[@]}" \
      vitasdk/vitasdk bash -c "$cmd"
  fi
}

# vita-elf-create échoue ("Cannot allocate ... segment 1 overlaps") quand il reste moins de
# ~2,6 Ko entre la fin du code et la page de 4 Ko suivante : pure coïncidence de taille. Dans ce
# cas on relance en ajoutant un bloc de remplissage qui décale la fin du code. Pas de 300 octets
# (jamais un multiple de 4096, qui ne changerait rien) : la fenêtre qui marche est étroite (le
# 2026-10-15, seul 3700 passait sur 0..3700 au pas de 300 ; l'ancien pas de 1400 la ratait).
PADS="0 300 600 900 1200 1500 1800 2100 2400 2700 3000 3300 3600 3700 3900 4200"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
for pad in $PADS; do
  if run_build "$pad" 2>&1 | tee "$log"; [ "${PIPESTATUS[0]}" -eq 0 ]; then
    [ "$pad" -gt 0 ] && echo "==> (remplissage de $pad octets ajouté pour contourner le chevauchement de segments)"
    break
  fi
  grep -q "segment 1 overlaps" "$log" || exit 1
  echo "==> Chevauchement de segments : nouvel essai avec un remplissage plus grand"
  [ "$pad" = "${PADS##* }" ] && exit 1
done

echo "==> OK : $BUILD_DIR/vitagame.vpk et $BUILD_DIR/eboot.bin"

if [ "${RELEASE:-0}" = 1 ]; then
  name=$(sed -n 's/.*set(GAME_NAME *"\(.*\)").*/\1/p' CMakeLists.txt | tr -cd 'A-Za-z0-9')
  version=$(sed -n 's/.*set(GAME_VERSION *"\(.*\)").*/\1/p' CMakeLists.txt)
  mkdir -p dist
  cp "$BUILD_DIR/vitagame.vpk" "dist/$name-$version.vpk"
  echo "==> Release : dist/$name-$version.vpk ($(du -h "dist/$name-$version.vpk" | cut -f1))"
fi
