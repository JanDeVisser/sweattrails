#!/bin/sh
# Makes sweattrails.png, the window icon on Linux and Windows,
# sweattrails.icns, the macOS bundle's icon, and sweattrails.ico, the
# Windows program's and installer's, from sweattrails.svg. Run it
# after changing the SVG, on a Mac, and commit what it makes: the build
# doesn't run it.
set -e
cd "$(dirname "$0")"

iconset=$(mktemp -d)/sweattrails.iconset
mkdir "$iconset"
render() {
    swift svg2png.swift sweattrails.svg "$1" "$2"
}
for size in 16 32 128 256 512; do
    render "$iconset/icon_${size}x${size}.png" $size
    render "$iconset/icon_${size}x${size}@2x.png" $((size * 2))
done
iconutil --convert icns --output sweattrails.icns "$iconset"
render sweattrails.png 256
ico=$(dirname "$iconset")/ico
mkdir "$ico"
for size in 16 24 32 48 64 256; do
    render "$ico/$size.png" $size
done
swift png2ico.swift sweattrails.ico "$ico/16.png" "$ico/24.png" "$ico/32.png" \
    "$ico/48.png" "$ico/64.png" "$ico/256.png"
rm -r "$(dirname "$iconset")"
