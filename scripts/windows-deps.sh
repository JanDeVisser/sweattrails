#!/bin/sh
#
# Builds the Windows build's libraries, from source, as static libraries:
# zlib, freetype, raylib, OpenSSL (for the login's HTTPS server) and curl
# (with Schannel, Windows's own TLS, so there's no CA bundle to ship). See
# specs/62-windows-port.md.
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
jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
mkdir -p "$sources" "$stamps"

# Downloads `url` as `file`, checks it's the release it should be, and
# unpacks it into $sources/<name>.
fetch() {
    name=$1 url=$2 file=$3 sha256=$4
    if [ ! -f "$sources/$file" ]; then
        curl -sSfL -o "$sources/$file.part" "$url"
        mv "$sources/$file.part" "$sources/$file"
    fi
    if ! echo "$sha256  $sources/$file" | shasum -a 256 -c - >/dev/null; then
        echo "$file isn't the release it should be: its SHA-256 is wrong" >&2
        exit 1
    fi
    rm -rf "${sources:?}/$name"
    mkdir -p "$sources/$name"
    tar -xf "$sources/$file" -C "$sources/$name" --strip-components 1
}

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

done_before() {
    if [ -f "$stamps/$1" ]; then
        echo "$1: already built"
        return 0
    fi
    echo "$1: building"
    return 1
}

if ! done_before zlib; then
    fetch zlib https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz \
        zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    cmake_build zlib -DZLIB_BUILD_EXAMPLES=OFF
    # zlib always builds a DLL too, and calls the static library zlibstatic.
    # Only the static one, under the name the others look for.
    rm -f "$prefix/bin/libzlib.dll" "$prefix/lib/libzlib.dll.a"
    mv "$prefix/lib/libzlibstatic.a" "$prefix/lib/libz.a"
    touch "$stamps/zlib"
fi

if ! done_before freetype; then
    fetch freetype https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz \
        freetype-2.15.0.tar.xz 93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5
    # The app's emoji are pictures of its own, so no PNG fonts, and no
    # HarfBuzz: raylib doesn't shape text.
    cmake_build freetype -DFT_REQUIRE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    touch "$stamps/freetype"
fi

if ! done_before raylib; then
    fetch raylib https://github.com/raysan5/raylib/archive/refs/tags/6.2.tar.gz \
        raylib-6.2.tar.gz df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee
    cmake_build raylib -DPLATFORM=Desktop -DBUILD_EXAMPLES=OFF
    touch "$stamps/raylib"
fi

if ! done_before openssl; then
    fetch openssl https://github.com/openssl/openssl/releases/download/openssl-4.1.0/openssl-4.1.0.tar.gz \
        openssl-4.1.0.tar.gz 50c17baf54cb008f6ce42d50deb4b28050a1aad711636cabd4b241c7e90cc2fd
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
    fetch curl https://curl.se/download/curl-8.23.0.tar.xz \
        curl-8.23.0.tar.xz 8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
    # HTTPS with Schannel, and only what the app uses.
    cmake_build curl -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DBUILD_CURL_EXE=OFF \
        -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF \
        -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
        -DCURL_DISABLE_LDAP=ON -DZLIB_ROOT="$prefix"
    touch "$stamps/curl"
fi

echo "The Windows libraries are in $prefix"
