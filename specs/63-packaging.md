# Packaging for macOS and Windows

Status: In review

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

### The oldest macOS: 13.3

The whole project, built with `CMAKE_OSX_DEPLOYMENT_TARGET` 11.0, fails
on two libc++ features only:

- floating-point `std::to_chars`, macOS 13.3, which `std::format` uses
  for every `double`. This sets the floor.
- floating-point `std::from_chars`, macOS 26.0, in `JSON.cpp` (every
  number in a JSON file), `overlay/Layout.cpp` (twice) and
  `storage/Laps.cpp`. Jan chose 13.3 over 26.0, so these four go
  through `float_from_chars()` (`StringUtil.h`, new): the same
  arguments and result as `std::from_chars`, built on `strtod()` and
  `strtof()`. Like `from_chars`, it takes no leading space, `+` or
  hexadecimal, and says `result_out_of_range` for `ERANGE`. `strtod()`
  follows the C locale, which the app never changes.

`SWEATTRAILS_MACOS_MINIMUM` in `CMakeLists.txt` is 13.3. It goes into
the `Info.plist`, and is `CMAKE_OSX_DEPLOYMENT_TARGET` in builds with
`SWEATTRAILS_MACOS_DEPS`: the package's, and a development build pointed
at those libraries. The plan was to set it in every build, but
Homebrew's libraries are built for the macOS they're on (27), and the
linker warns about each of them, at every link: some 300 warnings in a
full build. So a newer library feature shows up when the package script
builds, or in a development build with the macOS libraries, rather than
in the Homebrew build.

### Finding `share/` at runtime

- `src/Lib/Base/FileIO.h` gets two functions:
  - `executable_path()`: the running program's path
    (`_NSGetExecutablePath()` on macOS, `GetModuleFileNameW()` on
    Windows, `/proc/self/exe` on Linux).
  - `find_data_dir(executable, fallback)`: the first of
    `<exe dir>/../Resources/share` (in a macOS bundle) and
    `<exe dir>/share` (on Windows) that's a directory, else `fallback`.
    It checks both on every platform: that keeps it simple, and lets
    the tests cover both on either platform.
- `src/App/DataDir.h/.cpp` has `data_dir()`, which is
  `find_data_dir(executable_path(), SWEATTRAILS_DATADIR)`, worked out
  once. The build-time `SWEATTRAILS_DATADIR` stays in `config.h` as
  the development fallback.
- The eight uses of `SWEATTRAILS_DATADIR` in `SweatTrails.cpp`,
  `Activity.cpp` and `ActivitySelector.cpp`, `${sweattrails_DATADIR}` in
  the font directories included, use `data_dir()`. Tests that compile
  those files get `DataDir.cpp` too.
- The development build doesn't change: `CMAKE_INSTALL_PREFIX` stays
  forced to the build directory, and `build/bundle/sweattrails.app` has
  no `Resources/share`, so it finds `build/share` through the fallback.
  The packaging scripts install into a staging directory with
  `cmake --install <dir> --prefix <staging>`.

### Version and Info.plist

- `project(sweattrails VERSION 1.0.0 LANGUAGES CXX)`. `config.h` has
  `SWEATTRAILS_VERSION` from `PROJECT_VERSION`; the `_MAJOR`/`_MINOR`
  defines, which nothing uses, go.
- The bundle's `Info.plist` comes from `src/App/macos/Info.plist.in`,
  ours rather than CMake's default: CFBundleName `SweatTrails`, the
  identifier `com.finiandarcy.sweattrails`, both versions,
  `LSMinimumSystemVersion` (13.3), and `NSHighResolutionCapable`.
- The scripts read the version from the build's `config.h`, so it's
  written down only once.

### Libraries for macOS

- `scripts/deps.sh` (new) holds what both platforms share: the pinned
  versions, URLs and SHA-256s, and the `fetch`, `cmake_build` and
  `done_before` functions. `scripts/windows-deps.sh` sources it, and so
  does `scripts/macos-deps.sh` (new), so a version is bumped in one
  place for both.
- `scripts/macos-deps.sh` builds freetype 2.15.0 (with the system's
  zlib, nothing else, as on Windows), raylib 6.2 and OpenSSL 4.1.0
  (`darwin64-arm64-cc no-shared`), all for arm64 and macOS 13.3, as
  static libraries in `build-macos/deps`.
- `CMakeLists.txt` takes `-DSWEATTRAILS_MACOS_DEPS=<prefix>`. With it,
  CMake looks for freetype, raylib and OpenSSL there only, never in
  Homebrew (`CMAKE_PREFIX_PATH`, `OPENSSL_ROOT_DIR`,
  `OPENSSL_USE_STATIC_LIBS`, `PKG_CONFIG_LIBDIR`), and links raylib's
  frameworks (Cocoa, IOKit, CoreVideo, CoreAudio, OpenGL). Without it,
  the build uses Homebrew, as now: development is unchanged.

### Packaging scripts

Both scripts build in a release directory of their own, so they don't
touch the development builds in `build` and `build-windows`, run the
tests, and put the package in `dist/` (git ignores it).

`scripts/package-macos.sh`:

1. `scripts/macos-deps.sh`.
2. Configure `build-macos/release` (Release, `SWEATTRAILS_MACOS_DEPS`),
   build, `ctest`.
3. `cmake --install` into a staging directory, then make
   `SweatTrails.app`: the bundle, renamed, with `share/` in
   `Contents/Resources/share`.
4. Check that `otool -L` shows nothing outside `/usr/lib` and
   `/System`, and stop if it does.
5. Sign: with `SWEATTRAILS_SIGN_IDENTITY` (a Developer ID Application
   identity), `codesign --options runtime --timestamp`; without it, ad
   hoc (`--sign -`), with a message that the app isn't notarized and
   Gatekeeper will warn. The app needs no entitlements: no JIT, no
   unsigned libraries.
6. `diskutil image create from` a UDZO `.dmg`, from a folder with the
   app and an `Applications` link, signed like the app. On macOS 27,
   `hdiutil create` says it's deprecated for this.
7. With `SWEATTRAILS_NOTARY_PROFILE` (a `notarytool store-credentials`
   keychain profile), `notarytool submit --wait`, then `stapler staple`.
   Without it, the step is skipped, and the script says so.

`scripts/package-windows.sh`:

1. `scripts/windows-deps.sh`.
2. Configure `build-windows/release` (Release, the llvm-mingw
   toolchain), build, `ctest` under Wine.
3. `cmake --install` into a staging directory.
4. `makensis -DVERSION=... -DSTAGING=...
   src/App/windows/sweattrails.nsi` makes
   `dist/SweatTrails-1.0.0-setup.exe`. `makensis` comes from Homebrew
   (`brew install makensis`).

`src/App/windows/sweattrails.nsi`, with the Modern UI:

- `RequestExecutionLevel user`, `InstallDir
  $LOCALAPPDATA\Programs\SweatTrails`, `SetShellVarContext current`.
- Installs `sweattrails.exe` and `share\`, a Start menu shortcut, and
  `uninstall.exe`.
- The uninstall entry in `HKCU\...\CurrentVersion\Uninstall\SweatTrails`:
  name, version, publisher, icon, install location, size.
- The uninstaller removes the install directory, the shortcut and the
  entry, and leaves `%APPDATA%\sweattrails` and
  `%LOCALAPPDATA%\sweattrails` alone.

### Windows icon

`sweattrails.exe` has no icon now, so Explorer, the Start menu and the
installed apps list show Windows's default. `share/icons/make-app-icon.sh`
also makes `sweattrails.ico` (PNGs at 16 to 256 pixels), which
`sweattrails.rc` embeds in the app and the installer uses. Jan added
this to the scope.

## Test plan

- `StringUtilTest`: `float_from_chars()`
  with what `from_chars` takes and refuses: decimals, exponents, a
  number followed by text (`10km`), empty text, a leading space or `+`,
  hexadecimal, out of range, for `double` and `float`.
- `IOTest`: `find_data_dir()` with a temporary directory laid out as a
  macOS bundle, as a Windows install, as neither (the fallback), and
  with a file where the directory should be. `executable_path()` is an
  absolute path to an existing file, whose name is the test's.
- The macOS and Windows (under Wine) tests pass, in the development
  builds and in the release builds the scripts make.
- By hand, on 2026-11-17:
  - macOS, without a signing identity: the `.dmg` (15 MB) mounts with
    `SweatTrails.app` and the Applications link; `share/` is in
    `Contents/Resources`; the app is signed ad hoc as
    `com.finiandarcy.sweattrails`, `codesign --verify --deep --strict`
    passes, `otool -L` shows only `/usr/lib` and `/System`, and `minos`
    is 13.3. Started from the mounted `.dmg`, with no `share/` at the
    build-time path, it runs and reads the activity store. Not seen yet:
    the window itself (no screen capture here), and Gatekeeper with a
    Developer ID, which Jan doesn't have yet.
  - Windows, under Wine in a new prefix: the installer (12 MB) installs
    silently (`/S`) into `%LOCALAPPDATA%\Programs\SweatTrails`, with the
    Start menu shortcut and the uninstall entry (name, version,
    publisher, icon, size). The installed app reads its fonts, emoji,
    icon and settings from its own `share\` (`WINEDEBUG=+file`), and
    makes its store in `%LOCALAPPDATA%\sweattrails`. The uninstaller
    removes the program, the shortcut and the entry, and leaves
    `%APPDATA%\sweattrails` and `%LOCALAPPDATA%\sweattrails`. The
    installer's own pages haven't been seen.
- Found by building as Release, which nothing did before:
  `App::measure_text()` dereferenced a missing font, and
  `input_box_test` and `query_box_test` crashed. Fixed in its own commit.

## Review notes

*Filled in by the Reviewer.*
