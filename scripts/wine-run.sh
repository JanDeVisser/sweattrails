#!/bin/sh
#
# Runs a Windows program under Wine: how ctest runs the Windows build's
# tests (cmake/llvm-mingw.cmake). Wine is $WINE, ~/opt/wine-bin, or wine on
# the PATH.
#
# The Wine prefix, Wine's C: drive and registry, is build-windows/wineprefix
# unless WINEPREFIX says otherwise, so that tests don't touch ~/.wine. Its
# first use takes a while, to set it up.

set -e

if [ -z "$WINE" ]; then
    if [ -x "$HOME/opt/wine-bin" ]; then
        WINE="$HOME/opt/wine-bin"
    else
        WINE=wine
    fi
fi

root=$(cd "$(dirname "$0")/.." && pwd)
export WINEPREFIX="${WINEPREFIX:-$root/build-windows/wineprefix}"
# Wine's own messages, and the Vulkan driver's, aren't the program's.
export WINEDEBUG="${WINEDEBUG:--all}"
export MVK_CONFIG_LOG_LEVEL="${MVK_CONFIG_LOG_LEVEL:-0}"
# No "Wine is updating the prefix" window.
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"

exec "$WINE" "$@"
