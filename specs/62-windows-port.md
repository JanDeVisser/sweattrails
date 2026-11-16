# Windows port

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
