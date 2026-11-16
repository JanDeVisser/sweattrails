# Packaging for macOS and Windows

Status: Draft

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Someone who isn't a developer can download SweatTrails and install it: a
`.dmg` on macOS, an installer `.exe` on Windows. Both are built on Jan's
Mac. The Windows build depends on `62-windows-port.md`. The macOS part
does not, and can be done first.

## Feature

### What there is now

- `ninja -C build install` puts `sweattrails.app` in `build/bundle` and
  `share/` in `build/share`. `CMAKE_INSTALL_PREFIX` is forced to the build
  directory.
- `SWEATTRAILS_DATADIR` is that `build/share` path, compiled into the
  binary (`SweatTrails.cpp`, `Activity.cpp`, `ActivitySelector.cpp`). On
  another machine the path doesn't exist.
- The binary links raylib, freetype and OpenSSL from `/opt/homebrew`
  (`otool -L`). Users won't have Homebrew. curl, zlib and iconv come from
  `/usr/lib`, which is fine.

### Finding `share/` at runtime

The app looks for its data at runtime instead of at a path fixed at build
time:

- macOS: `SweatTrails.app/Contents/Resources/share`.
- Windows: `share\` next to `sweattrails.exe`.
- Development: the build-time path, as now, when the above doesn't exist.

### macOS: a signed, notarized `.dmg`

- raylib, freetype and OpenSSL are bundled in `Contents/Frameworks` (or
  linked statically), so nothing points at `/opt/homebrew`.
- The app is signed with a Developer ID certificate and the hardened
  runtime, and notarized and stapled, so Gatekeeper opens it without
  warnings.
- The `.dmg` shows the app and an Applications link to drag it to.

### Windows: cross-built installer

- Built on macOS with llvm-mingw (clang + libc++), via a CMake toolchain
  file.
- Dependencies built from source (FetchContent) or linked statically, so
  the installer only ships `sweattrails.exe`, any DLLs next to it, and
  `share\`.
- An NSIS installer (`makensis` runs on macOS) with Start menu shortcut
  and uninstaller.
- The tests run under Wine.

### Version 1 scope

- Runtime `share/` lookup.
- macOS `.dmg`, signed and notarized.
- Windows NSIS installer.
- A script or CPack setup that builds each package with one command.
- *(deferred)* Windows code signing (Azure Trusted Signing with jsign, or
  a certificate). Until then, users see the SmartScreen warning.
- *(deferred)* Building releases on GitHub Actions.
- *(deferred)* Universal (arm64 + x86_64) macOS binary.
- *(deferred)* Homebrew cask, winget.
- *(deferred)* Auto-update.

## Open questions

- Wahoo logins need a certificate made with mkcert (see `62-windows-port.md`).
  How do Windows users get it: README instructions (`winget install
  mkcert`, `mkcert -install`), or does the installer help?
- CPack (DragNDrop and NSIS generators) or plain scripts
  (`hdiutil`/`create-dmg`, `makensis`)?
- Bundle the Homebrew dylibs (`dylibbundler` / `fixup_bundle`) or build
  raylib and freetype statically with FetchContent? Static is simpler to
  ship and needed for a universal binary anyway.
- Does OpenSSL have to be in the app, or can TLS go through curl only
  (Secure Transport on macOS, Schannel on Windows)? What does `OAuth.cpp`
  use OpenSSL for?
- Is there an Apple Developer account ($99/yr) for signing and
  notarizing? Without one: ship unsigned and document right-click → Open?
- Which macOS versions to support (`CMAKE_OSX_DEPLOYMENT_TARGET`)? C++26
  library features may need a recent one.
- Version numbers: where does the release version come from (the
  `sweattrails_VERSION_*` variables in `CMakeLists.txt`, a git tag)?
- Where do the downloads go: GitHub Releases?
- `CMAKE_INSTALL_PREFIX` is forced to the build directory. Does that stay,
  with packaging using a separate staging directory?

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
