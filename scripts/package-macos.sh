#!/bin/sh
#
# Makes dist/SweatTrails-<version>.dmg: the app, built with the libraries
# from scripts/macos-deps.sh, signed, and notarized when it can be. See
# specs/63-packaging.md.
#
#   scripts/package-macos.sh
#
# SWEATTRAILS_SIGN_IDENTITY is the "Developer ID Application" identity to
# sign with (see `security find-identity -v -p codesigning`). Without it,
# the app is signed ad hoc, which runs on Apple silicon, but Gatekeeper
# warns about it.
#
# SWEATTRAILS_NOTARY_PROFILE is the keychain profile notarytool uses, made
# once with `xcrun notarytool store-credentials`. With it and the identity,
# the .dmg is notarized and the ticket stapled to it.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build-macos/release
work=$root/build-macos/package
dist=$root/dist
identity=${SWEATTRAILS_SIGN_IDENTITY:-}
profile=${SWEATTRAILS_NOTARY_PROFILE:-}

"$root/scripts/macos-deps.sh"

echo "Building the app"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSWEATTRAILS_MACOS_DEPS="$root/build-macos/deps"
ninja -C "$build"
# Twice at most: oauth_test fails now and then, whatever is built.
ctest --test-dir "$build" --output-on-failure --repeat until-pass:2

version=$(sed -n 's/^#define SWEATTRAILS_VERSION *"\(.*\)"$/\1/p' "$build/config.h")
app=$work/dmg/SweatTrails.app
dmg=$dist/SweatTrails-$version.dmg

echo "Putting SweatTrails.app together"
rm -rf "$work"
cmake --install "$build" --prefix "$work/install"
mkdir -p "$work/dmg"
cp -R "$work/install/bundle/sweattrails.app" "$app"
mkdir -p "$app/Contents/Resources/share"
# Without the scripts that make the icons and the emoji pack.
rsync -a --exclude '*.sh' --exclude '*.swift' "$work/install/share/" "$app/Contents/Resources/share/"

# Everything the app links has to come with macOS.
if otool -L "$app/Contents/MacOS/sweattrails" | tail -n +2 | grep -vE '^[[:space:]]+(/usr/lib/|/System/Library/)'; then
    echo "The app links the libraries above, which macOS doesn't have" >&2
    exit 1
fi

# Notarizing needs both. Apple checks the app, and then the .dmg, and
# each gets its ticket stapled to it: the app keeps its own once it's
# dragged out of the .dmg, so Gatekeeper needn't ask Apple at its first
# start.
notarizing=
if [ -n "$identity" ] && [ -n "$profile" ]; then
    notarizing=yes
fi

# Sends `file` to Apple's notary service, and stops unless it's accepted.
# notarytool's own failures, such as an unknown profile, stop it too, after
# saying what they were.
notarize() {
    file=$1
    echo "Notarizing $(basename "$file"): this can take a few minutes"
    result=$(xcrun notarytool submit "$file" --keychain-profile "$profile" --wait --output-format json) || true
    status=$(echo "$result" | plutil -extract status raw - 2>/dev/null) || true
    if [ "$status" != "Accepted" ]; then
        echo "$result" >&2
        echo "Notarizing $(basename "$file") failed. Apple's log: xcrun notarytool log <id> --keychain-profile $profile" >&2
        exit 1
    fi
}

if [ -n "$identity" ]; then
    echo "Signing with $identity"
    codesign --force --options runtime --timestamp --sign "$identity" "$app"
else
    echo "Signing ad hoc: SWEATTRAILS_SIGN_IDENTITY isn't set"
    codesign --force --sign - "$app"
fi
codesign --verify --deep --strict "$app"

if [ -n "$notarizing" ]; then
    # notarytool takes an app as a zip.
    ditto -c -k --keepParent "$app" "$work/SweatTrails.zip"
    notarize "$work/SweatTrails.zip"
    xcrun stapler staple "$app"
fi

echo "Making $dmg"
ln -s /Applications "$work/dmg/Applications"
mkdir -p "$dist"
rm -f "$dmg"
# UDZO, which every macOS opens.
diskutil image create from --format UDZO --volumeName "SweatTrails $version" "$work/dmg" "$dmg"
if [ -n "$identity" ]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
fi

if [ -n "$notarizing" ]; then
    notarize "$dmg"
    xcrun stapler staple "$dmg"
    spctl --assess --type open --context context:primary-signature --verbose "$dmg"
    spctl --assess --type execute --verbose "$app"
else
    echo "Not notarized: that needs SWEATTRAILS_SIGN_IDENTITY and SWEATTRAILS_NOTARY_PROFILE."
    echo "Gatekeeper will warn whoever opens it."
fi

echo "Made $dmg"
