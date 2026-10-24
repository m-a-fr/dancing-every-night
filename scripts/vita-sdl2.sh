#!/usr/bin/env bash
# Compile et installe dans $VITASDK la SDL2 de la Vita telle que la VitaSDK de développement l'a : version
# fixe, rendu natif GXM (SDL_VIDEO_RENDER_VITA_GXM), sans PVR ni vitaGL. Lancé par scripts/build-vita.sh dans
# l'image Docker vitasdk/vitasdk (CI) quand VITA_SDL2=<version> est donné : la SDL2 de l'image passe par vitaGL,
# nettement moins fluide sur la console (constaté le 2026-10-22).
set -euo pipefail
VERSION="${1:?usage: vita-sdl2.sh <version, ex. 2.32.8>}"
: "${VITASDK:?VITASDK non défini}"
WORK=/tmp/vita-sdl2
rm -rf "$WORK" && mkdir -p "$WORK"
cd "$WORK"
# cmake sert aussi à télécharger et décompresser : ni curl ni wget ne sont garantis dans l'image
cat > download.cmake <<'CMAKE'
file(DOWNLOAD "${URL}" sdl2.tar.gz STATUS st TLS_VERIFY ON)
list(GET st 0 code)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "téléchargement de SDL2 impossible : ${st}")
endif()
CMAKE
cmake -DURL="https://github.com/libsdl-org/SDL/releases/download/release-$VERSION/SDL2-$VERSION.tar.gz" -P download.cmake
cmake -E tar xzf sdl2.tar.gz
cmake -S "SDL2-$VERSION" -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$VITASDK/arm-vita-eabi" \
      -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DVIDEO_VITA_PIB=OFF -DVIDEO_VITA_PVR=OFF > build.log 2>&1 \
  && cmake --build build -j "$(nproc 2>/dev/null || echo 4)" >> build.log 2>&1 \
  && cmake --install build >> build.log 2>&1 \
  || { tail -40 build.log; exit 1; }
grep -q "define SDL_VIDEO_RENDER_VITA_GXM 1" "$VITASDK/arm-vita-eabi/include/SDL2/SDL_config.h"
echo "==> SDL2 $VERSION (rendu GXM) installée dans $VITASDK"
