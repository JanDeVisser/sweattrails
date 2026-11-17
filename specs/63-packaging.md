# Packaging for macOS and Windows

Status: Ready

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
Mac. The Windows build comes from `62-windows-port.md`, which is Done.

## Feature

### What there is now

- `ninja -C build install` puts `sweattrails.app` in `build/bundle` and
  `share/` in `build/share`. `CMAKE_INSTALL_PREFIX` is forced to the build
  directory.
- `SWEATTRAILS_DATADIR` is that `build/share` path, compiled into the
  binary (`SweatTrails.cpp`, `Activity.cpp`, `ActivitySelector.cpp`). On
  another machine the path doesn't exist. That's as true of the Windows
  build, where it's a Mac path that only works under Wine (drive `Z:`).
- The macOS binary links raylib, freetype and OpenSSL from
  `/opt/homebrew` (`otool -L`). Users won't have Homebrew. curl, zlib and
  libc++ come from `/usr/lib`, which is fine. Its minimum macOS
  (`LC_BUILD_VERSION minos`) is the SDK's, 27.2, as nothing sets
  `CMAKE_OSX_DEPLOYMENT_TARGET`.
- The Windows build (spec 62) is cross-compiled with llvm-mingw, from
  static libraries that `scripts/windows-deps.sh` builds from pinned,
  checksummed sources: `sweattrails.exe` needs only Windows's own DLLs.
  Its tests run under Wine. What's missing for Windows is finding
  `share\` and the installer.
- OpenSSL can't go: `OAuth.cpp` runs the login's loopback HTTPS *server*
  (Wahoo only redirects to `https://`, and Strava's login comes back to
  the same address). curl's TLS (Secure Transport, Schannel) is client
  only.
- Every online service already asks a lot of the user: Garmin Connect a
  key file, Wahoo and Strava an app of their own registered with the
  service, a `settings.json` edited by hand, and for those two a
  certificate made with mkcert. A "non-developer" can install the app and
  open FIT files; the online services stay for the determined.

### Finding `share/` at runtime

The app looks for its data at runtime instead of at a path fixed at build
time:

- macOS: `SweatTrails.app/Contents/Resources/share`.
- Windows: `share\` next to `sweattrails.exe`.
- Development: the build-time path, as now, when the above doesn't exist.

So a packaged app doesn't care about `CMAKE_INSTALL_PREFIX`. Whether it
stays forced to the build directory for development, and how the
packaging scripts stage the app (such as `cmake --install --prefix
<staging>`), is the Developer's to choose.

### Version

The version is set once, in `CMakeLists.txt`, as
`project(sweattrails VERSION 1.0.0)`, replacing the unused
`sweattrails_VERSION_MAJOR`/`MINOR`. The `Info.plist`
(`CFBundleShortVersionString`, `CFBundleVersion`), the installer and its
entry in Windows's installed apps, and the package file names
(`SweatTrails-1.0.0.dmg`, `SweatTrails-1.0.0-setup.exe`) take it from
there. Jan bumps it, and tags the release commit `v1.0.0`. Taking it from
`git describe` was the alternative; it was turned down because a build
without the repository would have no version.

### Publishing

The repository is public. The packages go to GitHub Releases, uploaded
by Jan with `gh release create`: the scripts only make them, so nothing
is published without Jan running that. The README links to the
releases page.

### macOS: a signed, notarized `.dmg`

- raylib, freetype and OpenSSL are linked statically, built from source
  by a macOS twin of `scripts/windows-deps.sh`, at the same pinned
  versions. Nothing points at `/opt/homebrew`, there are no dylibs to
  copy, rewrite and sign, both platforms ship the same library versions,
  and it's what a universal binary would need later. Bundling Homebrew's
  dylibs (dylibbundler, `fixup_bundle`) was the alternative: less to
  build, but the versions would be whatever Homebrew has that day.
  curl, zlib and libc++ stay the system's.
- The app is signed with a Developer ID certificate and the hardened
  runtime, and notarized and stapled, so Gatekeeper opens it without
  warnings. Jan is getting an Apple Developer account. Until it's there,
  and for anyone else building the package, the script works without
  one: with no signing identity given, it signs ad hoc (which Apple
  silicon needs anyway) and skips notarizing, and says so.
- The `.dmg` shows the app and an Applications link to drag it to.
- It runs on the oldest macOS whose system libc++ has what the app uses.
  The Developer finds that version, sets `CMAKE_OSX_DEPLOYMENT_TARGET` to
  it, and records in the Architecture which library feature sets the
  floor. Today nothing sets it, and the binary needs the SDK's 27.2.
- Apple silicon only: Intel Macs wait for the universal binary.

### Windows: cross-built installer

- Built as in spec 62. The installer ships `sweattrails.exe` and
  `share\`.
- An NSIS installer (`makensis` runs on macOS) with a Start menu
  shortcut and an uninstaller, which Windows lists under installed apps.
- It installs for the user only, in
  `%LOCALAPPDATA%\Programs\SweatTrails`, without administrator rights or
  a UAC prompt, as VS Code and Discord do. The app's data is the user's
  anyway.
- Uninstalling removes the program, not the user's activities and
  settings in `%APPDATA%\sweattrails` and `%LOCALAPPDATA%\sweattrails`,
  as dragging the app to the Trash does on macOS.
- x64 only. Windows 10 and 11, as the manifest says; Windows on ARM runs
  it emulated.

### Version 1 scope

- Runtime `share/` lookup.
- macOS `.dmg`, signed and notarized.
- Windows NSIS installer.
- A script that builds each package with one command:
  `scripts/package-macos.sh` (`hdiutil`, `codesign`, `notarytool`) and
  `scripts/package-windows.sh` (`makensis`, with a `.nsi` file in the
  repo). Plain scripts rather than CPack: each step is visible, and
  signing and notarizing is scripting anyway, which CPack's DragNDrop and
  NSIS generators make harder to follow.
- README: installing from the `.dmg` and the installer, and the Windows
  steps for mkcert (`winget install FiloSottile.mkcert`, `mkcert
  -install`, and making the certificate in `%APPDATA%\sweattrails`) next
  to the macOS ones. The installer doesn't help with mkcert: the people
  who log in to Wahoo or Strava have already registered an app of their
  own there and edited `settings.json`.
- *(deferred)* Windows code signing (Azure Trusted Signing with jsign, or
  a certificate). Until then, users see the SmartScreen warning.
- *(deferred)* Building releases on GitHub Actions.
- *(deferred)* Universal (arm64 + x86_64) macOS binary.
- *(deferred)* Homebrew cask, winget.
- *(deferred)* Auto-update.
- *(deferred, a spec of its own)* The app making and trusting its own
  localhost certificate, so the logins need no mkcert.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
