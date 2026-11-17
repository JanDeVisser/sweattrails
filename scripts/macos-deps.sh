#!/bin/sh
#
# Builds the libraries the packaged macOS app links statically, from
# source: freetype, raylib and OpenSSL (for the login's HTTPS server), at
# the releases in scripts/deps.sh, the same as on Windows. curl, zlib and
# libc++ are macOS's own. They're built for Apple silicon and the oldest
# macOS the app runs on, SWEATTRAILS_MACOS_MINIMUM in CMakeLists.txt. See
# specs/63-packaging.md.
#
#   scripts/macos-deps.sh [prefix]
#
# The prefix defaults to build-macos/deps. Configure the app with
# -DSWEATTRAILS_MACOS_DEPS=<prefix> to use them. A library that's already
# there is skipped, unless its release changed: remove its stamp file in
# <prefix>/stamps to build it again. Needs cmake, ninja, make and perl.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
prefix=${1:-$root/build-macos/deps}
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
sources=$prefix/src
stamps=$prefix/stamps
mkdir -p "$sources" "$stamps"
. "$root/scripts/deps.sh"

minimum=$(sed -n 's/^set(SWEATTRAILS_MACOS_MINIMUM \(.*\))$/\1/p' "$root/CMakeLists.txt")
if [ -z "$minimum" ]; then
    echo "SWEATTRAILS_MACOS_MINIMUM isn't in CMakeLists.txt" >&2
    exit 1
fi
# A library built for an older macOS than that is built again.
stamp_suffix=-macos$minimum

# Configures, builds and installs the CMake project in $sources/<name>.
# Homebrew's libraries are ignored, so nothing is built against them.
cmake_build() {
    name=$1
    shift
    cmake -S "$sources/$name" -B "$sources/$name/build" -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_PREFIX_PATH="$prefix" \
        -DCMAKE_IGNORE_PREFIX_PATH="/opt/homebrew;/usr/local" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$minimum" \
        -DBUILD_SHARED_LIBS=OFF \
        "$@"
    ninja -C "$sources/$name/build" install
}

if ! done_before freetype "$freetype_file"; then
    fetch freetype "$freetype_url" "$freetype_file" "$freetype_sha256"
    # As on Windows: with zlib, which is macOS's here, and nothing else.
    cmake_build freetype -DFT_REQUIRE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    mark_built freetype "$freetype_file"
fi

if ! done_before raylib "$raylib_file"; then
    fetch raylib "$raylib_url" "$raylib_file" "$raylib_sha256"
    cmake_build raylib -DPLATFORM=Desktop -DBUILD_EXAMPLES=OFF
    mark_built raylib "$raylib_file"
fi

if ! done_before openssl "$openssl_file"; then
    fetch openssl "$openssl_url" "$openssl_file" "$openssl_sha256"
    (
        cd "$sources/openssl"
        ./Configure darwin64-arm64-cc no-shared no-tests no-docs \
            -mmacosx-version-min="$minimum" \
            --prefix="$prefix" --libdir=lib
        make -j"$jobs"
        make install_sw
    )
    mark_built openssl "$openssl_file"
fi

echo "The macOS libraries are in $prefix"
