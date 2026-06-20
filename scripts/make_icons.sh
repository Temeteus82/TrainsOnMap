#!/usr/bin/env bash
# Regenerate the application icon raster assets from the SVG master.
#
#   resources/icon/appicon.svg  ->  appicon.ico  (multi-size, Windows .exe + runtime)
#                                    appicon.png  (256px, fallback / docs)
#
# Requires ImageMagick 7 with the rsvg delegate (high-fidelity SVG rendering).
# Run from anywhere:  bash scripts/make_icons.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
icondir="$here/resources/icon"
svg="$icondir/appicon.svg"

MAGICK="${MAGICK:-magick}"
command -v "$MAGICK" >/dev/null || { echo "ImageMagick ('magick') not found"; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Render a crisp master, then Lanczos-downsample each frame (sharper than letting
# the SVG rasterizer redraw tiny sizes directly).
"$MAGICK" -background none "$svg" -resize 1024x1024 "$tmp/m1024.png"

sizes=(16 24 32 48 64 128 256)
frames=()
for s in "${sizes[@]}"; do
    "$MAGICK" "$tmp/m1024.png" -filter Lanczos -resize "${s}x${s}" "$tmp/i$s.png"
    frames+=("$tmp/i$s.png")
done

# Multi-resolution ICO (PNG-compressed frames).
"$MAGICK" "${frames[@]}" "$icondir/appicon.ico"
# 256px PNG for the runtime window icon / README.
cp "$tmp/i256.png" "$icondir/appicon.png"

echo "wrote $icondir/appicon.ico ($(IFS=,; echo "${sizes[*]}")) and appicon.png"
