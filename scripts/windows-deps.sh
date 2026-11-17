#!/bin/sh
#
# Builds the Windows build's libraries, from source, as static libraries:
# zlib, freetype, raylib, OpenSSL (for the login's HTTPS server) and curl
# (with Schannel, Windows's own TLS, so there's no CA bundle to ship). See
# specs/62-windows-port.md. The releases are in scripts/deps.sh.
#
#   scripts/windows-deps.sh [prefix]
#
# The prefix defaults to build-windows/deps, where cmake/llvm-mingw.cmake
# looks. A library that's already there is skipped: remove its stamp file,
# <prefix>/stamps/<name>, to build it again. Needs llvm-mingw
# (LLVM_MINGW_ROOT, default ~/opt/llvm-mingw), cmake, ninja, make and perl.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
prefix=${1:-$root/build-windows/deps}
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)
export LLVM_MINGW_ROOT=${LLVM_MINGW_ROOT:-$HOME/opt/llvm-mingw}
toolchain=$root/cmake/llvm-mingw.cmake
target=x86_64-w64-mingw32
sources=$prefix/src
stamps=$prefix/stamps
mkdir -p "$sources" "$stamps"
. "$root/scripts/deps.sh"

# Configures, builds and installs the CMake project in $sources/<name>.
cmake_build() {
    name=$1
    shift
    cmake -S "$sources/$name" -B "$sources/$name/build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DSWEATTRAILS_WINDOWS_DEPS="$prefix" \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        "$@"
    ninja -C "$sources/$name/build" install
}

if ! done_before zlib; then
    fetch zlib "$zlib_url" "$zlib_file" "$zlib_sha256"
    cmake_build zlib -DZLIB_BUILD_EXAMPLES=OFF
    # zlib always builds a DLL too, and calls the static library zlibstatic.
    # Only the static one, under the name the others look for.
    rm -f "$prefix/bin/libzlib.dll" "$prefix/lib/libzlib.dll.a"
    mv "$prefix/lib/libzlibstatic.a" "$prefix/lib/libz.a"
    touch "$stamps/zlib"
fi

if ! done_before freetype; then
    fetch freetype "$freetype_url" "$freetype_file" "$freetype_sha256"
    # The app's emoji are pictures of its own, so no PNG fonts, and no
    # HarfBuzz: raylib doesn't shape text.
    cmake_build freetype -DFT_REQUIRE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    touch "$stamps/freetype"
fi

if ! done_before raylib; then
    fetch raylib "$raylib_url" "$raylib_file" "$raylib_sha256"
    cmake_build raylib -DPLATFORM=Desktop -DBUILD_EXAMPLES=OFF
    touch "$stamps/raylib"
fi

if ! done_before openssl; then
    fetch openssl "$openssl_url" "$openssl_file" "$openssl_sha256"
    (
        cd "$sources/openssl"
        PATH="$LLVM_MINGW_ROOT/bin:$PATH" ./Configure mingw64 no-shared no-tests no-docs \
            --cross-compile-prefix=$target- CC=clang CXX=clang++ \
            --prefix="$prefix" --libdir=lib
        PATH="$LLVM_MINGW_ROOT/bin:$PATH" make -j"$jobs"
        PATH="$LLVM_MINGW_ROOT/bin:$PATH" make install_sw
    )
    touch "$stamps/openssl"
fi

if ! done_before curl; then
    fetch curl "$curl_url" "$curl_file" "$curl_sha256"
    # HTTPS with Schannel, and only what the app uses.
    cmake_build curl -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DBUILD_CURL_EXE=OFF \
        -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF \
        -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
        -DCURL_DISABLE_LDAP=ON -DZLIB_ROOT="$prefix"
    touch "$stamps/curl"
fi

echo "The Windows libraries are in $prefix"
