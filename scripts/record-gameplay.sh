#!/bin/sh
# Enregistre une vidéo de gameplay (autoplay, 60 fps) avec la musique, prête pour YouTube.
#   scripts/record-gameplay.sh assets/charts/night_of_nights_expert.chart [sortie.mp4]
# Nécessite ffmpeg et un build desktop (make desktop).
set -e
CHART="$1"
[ -n "$CHART" ] || { echo "usage: $0 carte.chart [sortie.mp4]" >&2; exit 1; }
NAME=$(basename "$CHART" .chart)
OUT="${2:-gameplay_$NAME.mp4}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CHART_ABS=$(cd "$(dirname "$CHART")" && pwd)/$(basename "$CHART")
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
AUDIO="$ROOT/assets/$(sed -n 's/^AUDIO[[:space:]]*//p' "$CHART_ABS" | head -1)"
TMP="$ROOT/build-desktop/record_$NAME.mp4"

# La 1re image enregistrée correspond à t = -SONG_LEAD_IN_MS + 1 image : la musique démarre 2500 - 16,7 ms après.
LEAD_IN_MS=$(sed -n 's/^#define SONG_LEAD_IN_MS[[:space:]]*\([0-9]*\).*/\1/p' "$ROOT/src/rhythm.h")
DELAY_MS=$(awk "BEGIN { printf \"%d\", $LEAD_IN_MS - 1000/60 }")

echo "Rendu des images ($NAME)..."
(cd "$ROOT/build-desktop" && SDL_AUDIODRIVER=dummy VITAGAME_RECORD="$TMP" ./vitagame "$CHART_ABS" 2>/dev/null)

echo "Ajout de la musique..."
DUR=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$TMP")
FADE_START=$(awk "BEGIN { print $DUR - 3 }")
ffmpeg -loglevel error -y -i "$TMP" -i "$AUDIO" \
    -filter_complex "[1:a]adelay=${DELAY_MS}|${DELAY_MS},apad,atrim=0:${DUR},afade=t=out:st=${FADE_START}:d=3[a];[0:v]scale=1920:1088:flags=lanczos[v]" \
    -map "[v]" -map "[a]" -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -c:a aac -b:a 256k \
    -movflags +faststart "$OUT"
rm -f "$TMP"
echo "OK : $OUT"
