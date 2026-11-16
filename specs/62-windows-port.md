# Windows port

Status: In progress, on branch feature/62-windows-port

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

SweatTrails builds and runs on Windows. This spec covers only the code:
building the Windows binary from macOS and packaging it into an installer
is in `63-packaging.md`.

Today the app only builds on POSIX systems (macOS, Linux). Its
dependencies, raylib, curl, freetype, zlib and OpenSSL, all run on
Windows. Our own code that calls the operating system directly does not.

## Feature

Nothing changes for the user on macOS or Linux. On Windows the app works
the same, with these platform differences:

- `user_config_dir()` (settings, login tokens, the mkcert certificate) is
  `%APPDATA%\SweatTrails`: it's small, and roams with the user's profile.
- `user_data_dir()` (the activity store) and `user_state_dir()` (window
  and overlay state) are `%LOCALAPPDATA%\SweatTrails`: the store can be
  gigabytes, which shouldn't roam, and window state is per machine.
- Opening URLs (OAuth logins) and files goes through the Windows shell.
- The overlay looks for `ffmpeg.exe` on `PATH` (`;`-separated) and in
  `%LOCALAPPDATA%\Microsoft\WinGet\Links`. If it isn't found, the error
  says `winget install ffmpeg`, as it says `brew install ffmpeg` on macOS.
  ffmpeg isn't bundled: it's about 100 MB, and a GPL build comes with
  licence obligations.
- The Garmin API key file is `%USERPROFILE%\.config\garmin\connect-personal-api-key`:
  the same path, relative to the home directory, as on macOS and Linux.
  That's the path Garmin's sample app uses, so a key set up for it just
  works. `Garmin.cpp` uses `home_dir()` instead of `getenv("HOME")`, which
  isn't set on Windows. The environment variable keeps working as well.
- Wahoo logins need the mkcert certificate on Windows too. How users
  install mkcert there (`winget install mkcert`, `mkcert -install`) and
  where that's documented is part of `63-packaging.md`, not this spec.
- Paths with non-ASCII characters (`C:\Users\José`) work. The executable
  embeds an application manifest with `activeCodePage` UTF-8, so
  `path.string()`, `fopen` and the other narrow APIs use UTF-8, as on
  macOS. The code calls `.string()` on paths about 190 times, and none of
  those calls has to change. This needs Windows 10 1903 or later.

### What there is now

The POSIX-specific code, by file. Some of it isn't used:

- *Unused:* `src/Lib/Base/Process.h`, `Pipe.h` (`fork()`/`execvp()` and
  pipes). `src/App/Map.cpp` includes `Process.h` but uses nothing from it.
- *Unused:* `src/Lib/Base/Resolve.cpp` (`dlfcn.h`, looks up functions by
  name). Nothing outside it refers to `Resolver`.
- *Unused:* the `Socket` class in `src/Lib/Base/IO.cpp`, with its Unix
  domain sockets, `connect()` and `tcpip_address_resolve()`. Nothing in
  `src/` or `test/` uses it.
- `src/App/overlay/Encoder.cpp`: `posix_spawnp()` with a pipe to ffmpeg's
  stdin and its stderr to a log file; `pid_t`, `waitpid()`, `kill()`.
- `src/App/Desktop.cpp`: `posix_spawnp()`/`waitpid()` of `open` (macOS)
  or `xdg-open` to open URLs and files.
- `src/App/OAuth.cpp`: the login's loopback HTTPS server: BSD sockets,
  `poll()`, `fcntl()` for non-blocking, SIGPIPE, and OpenSSL with a
  certificate the user makes with mkcert (Wahoo only redirects to
  `https://`).
- `src/Lib/Base/IO.cpp`: `fcntl`, `stat`, and `getenv("HOME")`/`getpwuid`
  for the home directory. It already has `user_config_dir()`,
  `user_data_dir()` and `user_state_dir()`, with a macOS and an XDG
  implementation.
- `src/Lib/Base/FileBuffer.cpp`, `StringBuffer.h`: `unistd.h` file
  descriptors and `stat`.
- `src/Lib/Base/Error.cpp`: errno tables for macOS and Linux.
- `src/Lib/Base/Logging.h`: `sys/signal.h`.
- `src/App/overlay/Render.cpp`: `unistd.h`.
- `src/App/storage/Garmin.cpp:123`: `getenv("HOME")` for
  `~/.config/garmin/connect-personal-api-key`.
- `src/App/SweatTrails.cpp:1765`: `find_ffmpeg()` searches `PATH` plus the
  Homebrew directories, and the error says `brew install ffmpeg`.

### Version 1 scope

- Delete the unused code first, in a commit of its own: `Process.h`,
  `Pipe.h`, `Resolve.*`, and `IO.cpp`'s `Socket` with
  `tcpip_address_resolve()`. Porting it would be work for code nobody
  calls.
- Linux keeps its code (XDG directories, `xdg-open`, `pipe2`), so it's no
  more broken than now, but isn't built or tested in this spec. The other
  implementation next to Win32 is therefore POSIX, not macOS.
- Keep the interfaces that are already there, and give each a POSIX and a
  Win32 implementation: `Desktop` (open a URL or file), `Encoder` (start
  ffmpeg with a pipe to its stdin), and `user_config_dir()`,
  `user_data_dir()`, `user_state_dir()` and `home_dir()` in `IO.cpp`. The
  one new piece is a thin socket layer under the login's loopback server
  in `OAuth.cpp` (Winsock startup, `closesocket`, non-blocking sockets,
  `WSAPoll`). The Developer works out the details. Use the
  C++ stdlib where it covers the job (`std::filesystem` for `stat` and
  paths, for example) so less needs two implementations.
- The app builds with the llvm-mingw toolchain (clang + libc++), the same
  compiler and standard library as on macOS.
- The tests that don't need a window pass on Windows (under Wine is
  enough for this spec).
- *(deferred)* An MSVC build.
- *(deferred)* Windows on ARM.

## Open questions

None.

## Architecture

### Found while designing

Two things the spec's list doesn't have:

- **`wchar_t` is 16 bits on Windows.** The widgets (`TextLine`,
  `TextArea`, `FontCache`, `Modal`), `StringUtil`, `Devices` and the
  overlay's `Layout` keep text as `std::wstring`, one code point per
  `wchar_t`. On Windows that's UTF-16, so an emoji becomes two surrogate
  halves, and cursor movement and font lookups break. They move to
  `std::u32string` / `char32_t`, which is 32 bits everywhere. That change
  is mechanical, at about 33 places.
- **iconv.** `Utf8.cpp` converts with iconv, which mingw doesn't have.
  UTF-8 ↔ UTF-32 is about 40 lines, so `Utf8.cpp` gets its own codec, and
  the `ICONV` link dependency goes on every platform.

More dead code, which isn't compiled and isn't included anywhere:
`FileBuffer.*`, `StringBuffer.*`, `Integer.*`, `Scanner.h`,
`StringScanner.h` and `Checked.h`. `Lexer.cpp` isn't compiled either, but
`JSON.cpp` includes `Lexer.h`. These go in the same first commit as the
spec's dead code.

### Toolchain and dependencies

- **llvm-mingw** (a release tarball from GitHub, x86_64 UCRT), installed
  outside the repo. `cmake/llvm-mingw.cmake` is the CMake toolchain file.
  It finds the toolchain through `LLVM_MINGW_ROOT` (default
  `~/opt/llvm-mingw`, which doesn't need sudo), and sets `CMAKE_CROSSCOMPILING_EMULATOR` to `wine`,
  so `ctest` runs the tests under Wine.
- **Wine**: Homebrew disabled its Wine casks on 2026-09-01 because the
  builds aren't signed. The casks installed Gcenx's builds
  (github.com/Gcenx/macOS_Wine_builds), so that's what's used, unpacked in
  `~/opt/wine-devel-<version>`, with `~/opt/wine-bin` linked to its
  `wine`. It runs x86_64 Windows programs under Rosetta. The emulator is
  `scripts/wine-run.sh`, which finds Wine (`WINE`, then `~/opt/wine-bin`,
  then `PATH`), keeps a Wine prefix in the build directory so tests don't
  touch `~/.wine`, and silences Wine's own debug output.
- **The dependencies** are built from pinned source releases, as static
  libraries, by `scripts/windows-deps.sh` into `build-windows/deps`. They
  are raylib, zlib, freetype, curl (with Schannel: Windows's own TLS and
  certificate store, so there's no CA bundle to ship) and OpenSSL (only for
  the login's loopback server). The script is plain and readable, it runs
  once, and it leaves the main `CMakeLists.txt` with `find_package` as now.
  Static libraries mean no DLLs, which `63-packaging.md` benefits from.
- The build: `scripts/windows-deps.sh`, then
  `cmake -B build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake`,
  `ninja -C build-windows`, `ctest --test-dir build-windows`.
- `CMakeLists.txt` stops hard-coding macOS things where Windows trips over
  them: `-lc++abi` and `m` only off Windows, `MACOSX_BUNDLE` only on Apple,
  and `ws2_32`, `shell32` and the like on Windows. A `sweattrails.rc`
  embeds the manifest (`activeCodePage` UTF-8, and Windows 10 as the
  supported OS).

### Platform code

Where a whole implementation differs, it gets its own file, chosen in
CMake: `Foo.cpp` keeps what's shared, and `FooPosix.cpp` and
`FooWin32.cpp` hold the rest. Small differences, such as a path, stay as
`#ifdef _WIN32` in place.

- **`src/Lib/Base/Child.h`** (new): a child process: start a program with
  arguments, optionally with a pipe to its stdin and its stderr to a file,
  without inheriting the app's other handles; write to its stdin, close it,
  wait for its exit code, kill it. POSIX: `posix_spawnp` (the code that's
  in `Encoder.cpp` now). Win32: `CreateProcessW` with
  `STARTUPINFOEX`/`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, so only the pipe and
  the log are inherited, and the command line quoted by Windows's rules.
  `Encoder` keeps its queue and threads and uses a `Child` instead of
  `pid_t`/`m_stdin`. `Desktop`'s `run_and_wait()` uses it too. It replaces
  the deleted `Process.h`.
- **`Desktop`**: on Windows `open_browser()` is `ShellExecuteW`, and
  `reveal_command()` is `explorer.exe /select,<file>`, which does select
  the file.
- **`src/Lib/Base/Socket.h`** (new, small): `socket_t` (`int` / `SOCKET`),
  `InvalidSocket`, `close_socket()`, `make_nonblocking()`,
  `poll_sockets()` (`poll` / `WSAPoll`), `socket_error()` (`errno` /
  `WSAGetLastError()`), and a `SocketsInit` that calls `WSAStartup` once.
  `OAuth.cpp` and `OAuthTest.cpp` use it instead of the BSD calls.
  `ignore_sigpipe()` does nothing on Windows, which has no SIGPIPE. OpenSSL
  takes a `SOCKET` through `SSL_set_fd`, as it does there.
- **`IO.cpp`**: `home_dir()` is `USERPROFILE` (via `SHGetKnownFolderPath`)
  on Windows. `user_config_dir()` is `FOLDERID_RoamingAppData`, and
  `user_data_dir()` and `user_state_dir()` are `FOLDERID_LocalAppData`.
  `write_file_atomically()`'s rename and permissions go through
  `std::filesystem`.
- **`find_program()`** (`Render.cpp`): `;` between the `PATH` entries on
  Windows, and the program is `ffmpeg.exe`. "Executable" means it exists,
  because Windows has no `X_OK`. `find_ffmpeg()` adds the winget `Links`
  folder instead of the Homebrew ones, and the error says
  `winget install ffmpeg`.
- **`Garmin.cpp`**: `home_dir()` instead of `getenv("HOME")`.
- **`Error.cpp`**: an errno table for Windows (mingw's errno values, which
  are the MSVC CRT's).
- `Logging.h` uses `<csignal>`, and the stray `unistd.h` includes go.

## Test plan

- The macOS build and all its tests pass at every step, as now.
- The Windows build's tests, other than the ones that need a window, pass
  under Wine with `ctest --test-dir build-windows`. Tests that need a
  window or a GPU (anything that opens one) are left out of the Windows
  test list. The list is in the Windows section of `CMakeLists.txt`.
- Tests that use POSIX now become portable:
  - `EncoderTest` and `DesktopTest` use `/bin/sh` scripts as stand-ins for
    ffmpeg. They get a small test program of our own instead,
    `test/FakeProgram.cpp`: copy stdin to a file, write lines to stderr,
    exit with a code, sleep. It's built for both platforms.
  - `OverlayRenderStartTest`'s `fork()` and `TempDir.h`'s and
    `ActivitySelectorTest`'s `getpid()` go through portable code
    (`Child`, and a unique name from `<random>`).
  - `OAuthTest` uses `Socket.h`.
- New tests: `Utf8Test` gets surrogate-range, 4-byte (emoji) and invalid
  sequences for the new codec. `ChildTest` covers exit codes, stdin to
  file, stderr to log, kill, a missing program, arguments with spaces and
  quotes (the Win32 quoting), and that other open files aren't inherited.
  `IOTest` covers `user_*_dir()` with the environment variables set by
  hand where the platform reads them.
- Not tested automatically: opening a browser or Explorer (the tests check
  the command, as now), and the app's window under Wine. That's tried by
  hand: the app starts under Wine and shows activities.

## Review notes

*Filled in by the Reviewer.*
