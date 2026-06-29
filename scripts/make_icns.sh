#!/usr/bin/env bash
# Regenerate the macOS application icon (.icns) from the SVG master.
#
#   resources/icon/appicon.svg  ->  appicon.icns  (multi-resolution macOS bundle icon)
#
# Builds a full Retina iconset (16..512 @1x and @2x) and packs it with the macOS
# native iconutil. Requires rsvg-convert (librsvg) for crisp SVG rasterization.
# Run from anywhere:  bash scripts/make_icns.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
icondir="$here/resources/icon"
svg="$icondir/appicon.svg"

command -v rsvg-convert >/dev/null || { echo "rsvg-convert (librsvg) not found"; exit 1; }
command -v iconutil >/dev/null     || { echo "iconutil not found (macOS only)"; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
iconset="$tmp/appicon.iconset"
mkdir -p "$iconset"

# name=pixel-size pairs for the Apple iconset layout.
render() { rsvg-convert -w "$2" -h "$2" "$svg" -o "$iconset/$1.png"; }
render icon_16x16       16
render icon_16x16@2x    32
render icon_32x32       32
render icon_32x32@2x    64
render icon_128x128    128
render icon_128x128@2x 256
render icon_256x256    256
render icon_256x256@2x 512
render icon_512x512    512
render icon_512x512@2x 1024

iconutil -c icns "$iconset" -o "$icondir/appicon.icns"
echo "wrote $icondir/appicon.icns"
