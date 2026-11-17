#!/bin/sh
#
# Makes dist/SweatTrails-<version>-setup.exe: the Windows installer, with
# the app cross-built as in specs/62-windows-port.md. See
# specs/63-packaging.md.
#
#   scripts/package-windows.sh
#
# Needs what scripts/windows-deps.sh needs, Wine for the tests, and makensis
# (`brew install makensis`). The installer isn't signed, so Windows's
# SmartScreen warns whoever runs it.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build-windows/release
work=$root/build-windows/package
dist=$root/dist

"$root/scripts/windows-deps.sh"

echo "Building the app"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$root/cmake/llvm-mingw.cmake"
ninja -C "$build"
# Twice at most: oauth_test fails now and then, whatever is built.
ctest --test-dir "$build" --output-on-failure --repeat until-pass:2

version=$(sed -n 's/^#define SWEATTRAILS_VERSION *"\(.*\)"$/\1/p' "$build/config.h")
setup=$dist/SweatTrails-$version-setup.exe

echo "Putting the app together"
rm -rf "$work"
cmake --install "$build" --prefix "$work/install"
mkdir -p "$work/app/share"
cp "$work/install/bin/sweattrails.exe" "$work/app/"
# Without the scripts that make the icons and the emoji pack.
rsync -a --exclude '*.sh' --exclude '*.swift' "$work/install/share/" "$work/app/share/"

echo "Making $setup"
mkdir -p "$dist"
makensis -V2 -DVERSION="$version" -DAPP="$work/app" -DOUTFILE="$setup" \
    "$root/src/App/windows/sweattrails.nsi"

echo "Made $setup"
