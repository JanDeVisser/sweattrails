# What scripts/windows-deps.sh and scripts/macos-deps.sh share: the
# libraries' releases, so both platforms ship the same versions, and how
# they're fetched and built. Sourced, not run. The caller sets $sources and
# $stamps, and may set $stamp_suffix.
# See specs/63-packaging.md.

zlib_url=https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
zlib_file=zlib-1.3.1.tar.gz
zlib_sha256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23

freetype_url=https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz
freetype_file=freetype-2.15.0.tar.xz
freetype_sha256=93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5

raylib_url=https://github.com/raysan5/raylib/archive/refs/tags/6.2.tar.gz
raylib_file=raylib-6.2.tar.gz
raylib_sha256=df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee

openssl_url=https://github.com/openssl/openssl/releases/download/openssl-4.1.0/openssl-4.1.0.tar.gz
openssl_file=openssl-4.1.0.tar.gz
openssl_sha256=50c17baf54cb008f6ce42d50deb4b28050a1aad711636cabd4b241c7e90cc2fd

curl_url=https://curl.se/download/curl-8.23.0.tar.xz
curl_file=curl-8.23.0.tar.xz
curl_sha256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5

jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc)

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

# The stamp that says library `name` is built from release `file`. It's
# named after the release, and $stamp_suffix (such as the macOS version
# it's built for), so that after a bump the library is built again.
stamp() {
    echo "$stamps/$2${stamp_suffix:-}"
}

done_before() {
    if [ -f "$(stamp "$1" "$2")" ]; then
        echo "$1: already built"
        return 0
    fi
    echo "$1: building"
    return 1
}

# Marks library `name` built from release `file`, and forgets the others.
mark_built() {
    rm -f "$stamps/$1" "$stamps/$1"-*
    touch "$(stamp "$1" "$2")"
}
