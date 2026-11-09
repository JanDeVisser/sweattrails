#!/bin/sh
# Makes twemoji.zip, the emoji the app draws, from a Twemoji release:
# its 72x72 PNGs, stored as they are, as PNGs don't compress further, and
# the graphics' licence next to it. Run it by hand to update Twemoji,
# and commit what it makes: the build doesn't run it, and stays offline.
# See specs/42-emoji.md.
set -e
version=${1:-17.0.3}
cd "$(dirname "$0")"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fsSL "https://github.com/jdecked/twemoji/archive/refs/tags/v$version.tar.gz" | tar -xz -C "$work"
assets="$work/twemoji-$version/assets/72x72"
if [ ! -d "$assets" ]; then
    echo "No assets/72x72 in Twemoji $version" >&2
    exit 1
fi
rm -f twemoji.zip
(cd "$assets" && zip -q -0 -X "$OLDPWD/twemoji.zip" ./*.png)
cp "$work/twemoji-$version/LICENSE-GRAPHICS" LICENSE-GRAPHICS
echo "$version" > VERSION
echo "Twemoji $version: $(unzip -l twemoji.zip | tail -1)"
