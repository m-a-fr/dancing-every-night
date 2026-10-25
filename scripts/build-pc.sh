#!/usr/bin/env bash
# Version PC distribuable du jeu, pour le système sur lequel le script tourne (macOS, Linux, Windows via MSYS2 MinGW) :
#   1. SDL2 compilée en statique depuis ses sources (une seule fois, dans build-pc/) : rien à installer pour le joueur ;
#   2. le jeu en Release, lié à cette SDL2 ;
#   3. un paquet dans dist/ avec les seuls assets commités (comme make release pour la Vita) :
#        macOS   : <Nom>-<version>-macos.zip   (« Dancing Every Night.app », Intel + Apple Silicon)
#        Linux   : <Nom>-<version>-linux.tar.gz
#        Windows : <Nom>-<version>-windows.zip
# Utilisé par la CI (.github/workflows/builds.yml) ; marche aussi en local : make pc
set -euo pipefail
cd "$(dirname "$0")/.."

SDL_VERSION=2.30.9
OUT="$PWD/build-pc"
GAME_NAME=$(sed -n 's/.*set(GAME_NAME *"\(.*\)").*/\1/p' CMakeLists.txt)
VERSION=$(sed -n 's/.*set(GAME_VERSION *"\(.*\)").*/\1/p' CMakeLists.txt)
NAME=$(printf '%s' "$GAME_NAME" | tr -cd 'A-Za-z0-9')      # DancingEveryNight

case "$(uname -s)" in
  Darwin) PLATFORM=macos ;;
  Linux) PLATFORM=linux ;;
  MINGW*|MSYS*) PLATFORM=windows ;;
  *) echo "système non pris en charge : $(uname -s)"; exit 1 ;;
esac
EXTRA=()
if [ "$PLATFORM" = macos ]; then   # binaire universel, macOS 11 et plus
  EXTRA=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)
fi
if [ "$PLATFORM" = windows ]; then EXTRA=(-G "MSYS Makefiles"); fi
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
mkdir -p "$OUT" dist

# ---- 1. SDL2 statique ----
SDL_PREFIX="$OUT/sdl-$SDL_VERSION-$PLATFORM"
if [ ! -f "$SDL_PREFIX/.done" ]; then
  echo "==> SDL2 $SDL_VERSION (statique)"
  rm -rf "$OUT/SDL2-$SDL_VERSION" "$OUT/sdl-build"
  curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz" | tar xz -C "$OUT"
  log="$OUT/sdl-build.log"   # journal complet (avertissements de SDL) ; affiché seulement en cas d'échec
  { cmake -S "$OUT/SDL2-$SDL_VERSION" -B "$OUT/sdl-build" "${EXTRA[@]}" -DCMAKE_BUILD_TYPE=Release \
          -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" &&
    cmake --build "$OUT/sdl-build" -j "$JOBS" && cmake --install "$OUT/sdl-build"; } > "$log" 2>&1 \
    || { tail -40 "$log"; exit 1; }
  touch "$SDL_PREFIX/.done"
fi

# ---- 2. Le jeu ----
echo "==> $GAME_NAME $VERSION ($PLATFORM)"
cmake -S . -B "$OUT/game" "${EXTRA[@]}" -DCMAKE_BUILD_TYPE=Release -DGAME_STATIC_SDL=ON -DGAME_COPY_ASSETS=OFF \
      -DCMAKE_PREFIX_PATH="$SDL_PREFIX" > /dev/null
cmake --build "$OUT/game" -j "$JOBS"
BIN="$OUT/game/vitagame"
[ "$PLATFORM" = windows ] && BIN="$BIN.exe"

# ---- 3. Paquet ----
STAGE="$OUT/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
if [ "$PLATFORM" = macos ]; then
  ROOT="$STAGE/$GAME_NAME.app/Contents"
  DATA="$ROOT/Resources"
  mkdir -p "$ROOT/MacOS" "$DATA"
  cp "$BIN" "$ROOT/MacOS/$NAME"
  strip -x "$ROOT/MacOS/$NAME"
  # icône : celle de la Vita (128 px), agrandie pour les tailles Retina
  ICONSET="$OUT/icon.iconset"
  rm -rf "$ICONSET" && mkdir -p "$ICONSET"
  for s in 16 32 128 256 512; do
    sips -s format png -z $s $s sce_sys/icon0.png --out "$ICONSET/icon_${s}x${s}.png" > /dev/null
    sips -s format png -z $((s * 2)) $((s * 2)) sce_sys/icon0.png --out "$ICONSET/icon_${s}x${s}@2x.png" > /dev/null
  done
  iconutil -c icns "$ICONSET" -o "$DATA/icon.icns"
  cat > "$ROOT/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>$GAME_NAME</string>
  <key>CFBundleDisplayName</key><string>$GAME_NAME</string>
  <key>CFBundleIdentifier</key><string>io.github.m-a-fr.$NAME</string>
  <key>CFBundleExecutable</key><string>$NAME</string>
  <key>CFBundleIconFile</key><string>icon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.music-games</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
  SAVE_HINT="~/Library/Application Support/$GAME_NAME/save/"
else
  DATA="$STAGE/$NAME"
  mkdir -p "$DATA"
  if [ "$PLATFORM" = windows ]; then cp "$BIN" "$DATA/$NAME.exe"; strip "$DATA/$NAME.exe"
  else cp "$BIN" "$DATA/$NAME"; strip "$DATA/$NAME"; fi
  SAVE_HINT="the save/ folder next to the game"
fi

# seuls les assets commités : jamais les morceaux personnels (ignorés par git)
git archive --format=tar HEAD assets | tar -x -C "$DATA"
./scripts/official-audio.sh stage "$DATA/assets"   # l'audio des chansons officielles n'est pas dans le dépôt (voir le script)
mkdir -p "$DATA/assets/songs"
cp LICENSE LICENSE-ASSETS.md "$DATA/"
cat > "$DATA/README.txt" <<TXT
$GAME_NAME $VERSION — PC version

Controls: arrows / WASD, Space or J (cross), K (circle), U (square), I (triangle), Q / E (scratch),
Enter (start), Backspace (select). A game controller works too.

Add songs: copy a song .zip into the import/ folder of $SAVE_HINT
(created at first launch), then restart the game. The song is unpacked into save/songs/.

Code: GNU GPL v3 (LICENSE). Graphics and other assets: see LICENSE-ASSETS.md.
TXT

PKG="dist/$NAME-$VERSION-$PLATFORM"
rm -f "$PKG.zip" "$PKG.tar.gz"
case "$PLATFORM" in
  macos)
    codesign --force --deep -s - "$STAGE/$GAME_NAME.app"     # signature ad hoc : exigée sur Apple Silicon
    ditto -c -k --keepParent "$STAGE/$GAME_NAME.app" "$PKG.zip"
    PKG="$PKG.zip" ;;
  linux) (cd "$STAGE" && tar czf "../../$PKG.tar.gz" "$NAME"); PKG="$PKG.tar.gz" ;;
  windows) (cd "$STAGE" && cmake -E tar cf "../../$PKG.zip" --format=zip "$NAME"); PKG="$PKG.zip" ;;
esac
echo "==> $PKG ($(du -h "$PKG" | cut -f1))"
