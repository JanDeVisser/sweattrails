# Windows port

Status: In review, on branch feature/62-windows-port

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

### Found while building

- **`IO.h` clashed with `<io.h>`.** mingw's `sys/stat.h` includes
  `<io.h>`, which on a file system that ignores case, as macOS's and
  Windows's do, finds `src/Lib/Base/IO.h`. It's `FileIO.h` (and
  `FileIO.cpp`) now. No other header of ours has the name of one that
  mingw's headers include.
- **`windows.h` clashes with raylib** (`Rectangle`, `CloseWindow`,
  `ShowCursor`, `DrawText`, ...), and `curl.h` and `GL/gl.h` include it.
  The Windows build defines `NOGDI` and `NOUSER`, which leave those
  names out. `src/Lib/Base/Win32.h` undefines them again and includes
  `winsock2.h`, `windows.h`, `shellapi.h` and `shlobj.h`, in that order,
  for the Win32 code only, which never includes raylib. clang-format sorts
  includes, so the order is fixed inside `Win32.h`, with clang-format
  turned off around it. `Win32.h` also has `to_wide()`, `from_wide()` and
  `win32_error()`.
- **Text-mode files.** `read_file_by_name()` and `write_file_by_name()`
  opened their streams in text mode, which on Windows stops reading at a
  ^Z byte and writes `\r\n` for `\n`, and corrupts FIT files. All the
  app's streams are binary now.
- **`localtime_r()`, `gmtime_r()`, `timegm()` and `tm_gmtoff`** aren't on
  Windows. `Date.h` has `utc_tm()`, `local_tm()`, `utc_offset()` and
  `utc_time()` instead, which the app and tests use.
- **`path.c_str()` is `wchar_t` on Windows.** Where a path goes to a C
  API (OpenSSL's file functions, raylib's `LoadImage()`, `ExportImage()`)
  it goes as `path.string().c_str()`, which the manifest's code page
  makes UTF-8.
- **Time zones in tests.** The tests set `TZ` to IANA names. Windows's C
  runtime only knows values like `EST5EDT`, with US summer time rules.
  `test/Environment.h` has `set_time_zone()`, which maps UTC and
  America/Toronto, and returns false for other zones on Windows, and
  `set_environment()`/`unset_environment()` for `setenv()`/`unsetenv()`.
  The two checks that need Europe/Amsterdam (`FitnessTest`) and
  Pacific/Auckland (`DownloadTest`) don't run on Windows.
  `StravaTitlesTest` moved its second zone from Amsterdam to UTC, which
  crosses the month just the same.
- The app's ID is `sweattrails`, so the folders are
  `%APPDATA%\sweattrails` and `%LOCALAPPDATA%\sweattrails`, as on macOS
  (`~/Library/Application Support/sweattrails`). Windows ignores case.

### Toolchain and dependencies

- **llvm-mingw** 20261104 (x86_64 UCRT, clang 23), from its GitHub
  release, in `~/opt/llvm-mingw` (no sudo needed). `cmake/llvm-mingw.cmake`
  is the CMake toolchain file. It finds the toolchain through
  `LLVM_MINGW_ROOT` and the libraries through `SWEATTRAILS_WINDOWS_DEPS`
  (default `build-windows/deps`), keeps CMake and pkg-config away from
  Homebrew's libraries, and runs programs through `scripts/wine-run.sh`.
- **Wine**: Homebrew disabled its Wine casks on 2026-09-01 because the
  builds aren't signed. The casks installed Gcenx's builds
  (github.com/Gcenx/macOS_Wine_builds), so that's what's used: Wine Devel
  11.21, unpacked in `~/opt/wine-devel-11.21`, with `~/opt/wine-bin`
  linked to its `wine`. It runs x86_64 Windows programs under Rosetta.
  `scripts/wine-run.sh` finds Wine (`WINE`, then `~/opt/wine-bin`, then
  `PATH`), keeps its prefix in `build-windows/wineprefix` so tests don't
  touch `~/.wine`, and silences Wine's own messages. Wine honours the
  manifest: a program without it gets code page 1252, with it 65001.
- **The dependencies** are built by `scripts/windows-deps.sh`, from
  pinned source releases checked by SHA-256, as static libraries, into
  `build-windows/deps`: zlib 1.3.1, freetype 2.15.0 (zlib only), raylib
  6.2, OpenSSL 4.1.0 (for the login's loopback server only) and curl
  8.23.0 with Schannel (Windows's own TLS and certificate store, so no CA
  bundle). The versions are macOS's, except zlib, where 1.4 was too new.
  The hashes were checked against OpenSSL's published one, and against a
  second download site for curl and freetype. A library that's built is
  skipped the next time.
- The build:

  ```sh
  scripts/windows-deps.sh
  cmake -B build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake
  ninja -C build-windows
  ctest --test-dir build-windows
  ninja -C build-windows install   # share/, for running the app
  ```

- `CMakeLists.txt` on Windows: `-static` instead of `-lc++abi`, the
  defines above and `_WIN32_WINNT` for Windows 10, `CURL_STATICLIB`, the
  Win32 sources instead of the POSIX ones, Windows's libraries, and the
  app as a GUI program (`WIN32_EXECUTABLE`), without a console.
  `src/App/windows/sweattrails.rc` embeds `sweattrails.manifest`
  (`activeCodePage` UTF-8, Windows 10 and 11) in every program, tests
  too, since they need UTF-8 paths and arguments as much as the app.
  `sweattrails.exe` only needs Windows's own DLLs (the UCRT is part of
  Windows 10).

### Platform code

Where a whole implementation differs, it gets its own file, chosen in
CMake: `FooPosix.cpp` and `FooWin32.cpp`. Small differences stay as
`#ifdef _WIN32` in place.

- **`src/Lib/Base/Child.h`** (new): a program the app runs. `start()`
  takes the command and `Options` (a pipe to its stdin, stdout thrown
  away, stderr to a file); `write()`, `close_stdin()`, `wait()` for the
  exit code, and `kill()`, which another thread may call while one waits.
  Also `process_id()`. `ChildPosix.cpp` is the `posix_spawnp()` code that
  was in `Encoder.cpp`, with its care not to kill a reaped pid.
  `ChildWin32.cpp` uses `CreateProcessW` with
  `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, so only the program's own three
  handles are inherited, quotes the command line by the C runtime's
  rules, and gives console programs such as ffmpeg no console window.
  A killed program ends with exit code 1 there. `Encoder` and `Desktop`'s
  `run_and_wait()` use it. It replaces the deleted `Process.h`.
- **`Desktop`**: on Windows `open_browser()` is `ShellExecuteW`, and
  `reveal_file()` is `SHOpenFolderAndSelectItems()`, which selects the
  file in Explorer. `explorer /select,` would do that too, but it quotes
  by rules of its own and always exits with 1. `reveal_command()` is
  POSIX only.
- **`src/Lib/Base/Socket.h`** (new): `socket_t` (`int` / `SOCKET`),
  `InvalidSocket`, `start_sockets()` (`WSAStartup()` once),
  `socket_error()` (Winsock's errors that have an errno value get it),
  `close_socket()`, `make_nonblocking()` (also not inherited),
  `ignore_sigpipe()` (nothing on Windows) and `poll_sockets()` (`poll` /
  `WSAPoll`). It includes no system headers, because of `windows.h`; the
  `.cpp` files that make socket calls include them. `OAuth.cpp` and
  `OAuthTest.cpp` use it. `SO_REUSEADDR` is only set off Windows, where it
  would let another program take the login's port.
- **`FileIO.cpp`**: on Windows `home_dir()` is `FOLDERID_Profile`,
  `user_config_dir()` `FOLDERID_RoamingAppData`, and `user_data_dir()`
  and `user_state_dir()` `FOLDERID_LocalAppData`, from
  `SHGetKnownFolderPath()`.
- **`find_program()`** (`Render.cpp`): on Windows, `name.exe`, in a
  `PATH` separated by `;`, and any such file counts, as Windows has no
  `X_OK`. `find_ffmpeg()` adds `%LOCALAPPDATA%\Microsoft\WinGet\Links`
  instead of the Homebrew folders, and the error says
  `winget install ffmpeg`.
- **`Garmin.cpp`**: `home_dir()` instead of `getenv("HOME")`.
- **`Error.cpp`**: an errno table for Windows: the UCRT's values, and the
  POSIX ones mingw adds from 100 up.
- `Logging.h` includes `<cstdlib>` instead of `sys/signal.h`, and the
  stray `unistd.h` includes are gone.

## Test plan

- The macOS build and all its tests pass at every step, as before.
- All 84 tests are built for Windows and run under Wine with
  `ctest --test-dir build-windows`; none needed leaving out. They pass,
  on macOS and under Wine, except `test_many_connections` in
  `oauth_test`, which fails now and then on both, as it did before this
  spec.
- Tests that used POSIX are portable now:
  - `EncoderTest`, `DesktopTest` and `OverlayRenderStartTest` used
    `/bin/sh` as a stand-in for ffmpeg. `test/FakeProgram.cpp` does the
    same few things on both platforms: copy stdin to a file, write to
    stderr, write a file, write its arguments, sleep, check a handle isn't
    inherited, exit with a code. Tests get its path as `FAKE_PROGRAM`.
  - `EncoderTest`'s check for leaked file descriptors counts handles on
    Windows, and its check that the program doesn't inherit the app's
    files opens an inheritable handle there.
  - `OverlayRenderStartTest` only forks off Windows; there a crash fails
    the test anyway, and ctest's timeout catches a hang.
  - `TempDir.h` and `ActivitySelectorTest` get the process ID from
    `Child.h`.
  - `OAuthTest` uses `Socket.h`, and `send()` instead of `write()`.
  - Tests compare paths in their generic form, or expect the platform's
    separators where the app shows a path to the user. The checks of
    POSIX permissions are POSIX only.
- New tests: `Utf8Test` covers each UTF-8 length, invalid UTF-8 (cut
  off, overlong, surrogates, past U+10FFFF), invalid UTF-32, and
  `is_space()` and `is_control()`. `ChildTest` covers exit codes, a
  missing program, arguments with spaces, quotes, backslashes and UTF-8
  (the Win32 quoting), stdin and stderr with bytes that text mode would
  change, writing to a program that has gone, and `kill()`. `IOTest`
  covers the user directories on each platform, the home directory, and
  a file name that isn't ASCII.
- Not tested automatically: opening a browser or Explorer, and the app's
  window. By hand, on 2026-11-16, under Wine: the app opens its window,
  and with ten FIT files in its inbox it files them, downloads map tiles
  (curl with Schannel), and shows the activities, their maps, graphs and
  summaries as on macOS.

## Review notes

No correctness bug against the spec's Version 1 scope found. Both builds
pass (macOS, and Windows under Wine) with the added tests, apart from the
known flaky `test_many_connections`. Findings, most severe first:

1. **Suggestion** (`src/Lib/Base/ChildWin32.cpp`, `start()`): the results of
   `InitializeProcThreadAttributeList()` and `UpdateProcThreadAttribute()`
   aren't checked. If `Update` fails, `CreateProcessW` still runs with
   `bInheritHandles = TRUE` and without the handle list, so the program
   silently inherits every inheritable handle in the app, which is what the
   list is there to prevent. If `Initialize` fails, `DeleteProcThreadAttributeList()`
   runs on a list that was never initialised. Return the `win32_error()`
   instead (and delete the list only after a successful initialise).
2. **Suggestion** (`ChildWin32.cpp`): the pipe's app end and the stderr file
   are inheritable for a moment (`CreatePipe` with `inheritable`, then
   `SetHandleInformation`; `open_inheritable()` for the file). Another thread
   that starts a program with `bInheritHandles` (`ShellExecuteW` from
   `open_browser()` does) in that window gets them. Creating the handles
   non-inheritable and making only the program's copies inheritable
   (`DuplicateHandle(..., TRUE, ...)`), or the handle list alone, closes it.
   Rare, hence only a suggestion.
3. **Suggestion** (`ChildWin32.cpp`): `Child::State::waited` and the mutex do
   nothing on Windows. The process handle stays open until the destructor,
   so `TerminateProcess()` on a finished process is harmless. They can go,
   or `kill()` can say so. A killed program and one that exits with 1 are
   indistinguishable (the spec says so).
4. **Suggestion** (`src/Lib/Base/Date.cpp`): `localtime_s()`/`gmtime_s()`
   fail for times before 1970 on Windows and leave `tm` zeroed (year 1900),
   where POSIX gives the right date; `_mkgmtime()` returns -1 there. The
   app's times are `u32` and after 1990, so nothing reaches it now.
   `utc_offset()` would give nonsense for such a time.
5. **Suggestion** (`src/Lib/Widget/TextArea.cpp` `put()`, `TextLine.cpp`
   `insert()`): `ch >= 0` is always true for `char32_t` now (it guarded
   against a negative `wchar_t`). Harmless, but the check can go and may
   warn with `-Wtype-limits`.
6. **Missing test**, added: `utc_tm()`, `utc_time()` (leap day, week and
   year day, carrying overflowing fields) and `utc_offset()` (summer, winter,
   UTC) had no direct tests, only through other features; `DateTest` has them
   now (`test_utc_tm_and_utc_time`, `test_utc_offset`,
   `test_parse_iso8601_calendar`). They pass on macOS and under Wine.
7. **Missing test**, added: `ChildTest::test_kill_while_waiting`, the
   spec's "another thread may call `kill()` while one waits". Passes on both.

Checked and fine: handle inheritance (the three handles are distinct, so the
list has no duplicates; error paths close the pipe end, and `StdHandles`
closes the program's ends); command-line quoting (backslashes before a quote
and at the end, empty argument); the kill/wait race (POSIX reaps under the
lock; on Windows the handle can't be reused); the UTF-8 codec (overlong,
surrogates, past U+10FFFF, cut-off, 0xF5 and up, 0xC0/0xC1 all rejected);
`char32_t` replacements, with no `wchar_t`, `isw*` or `L'` left in `src/`;
Winsock use (`WSAStartup` before the first socket, `WSAPoll` never called
with an empty list, `EINTR` mapped, accepted sockets made non-blocking and
not inheritable, no `SO_REUSEADDR`); no text-mode stream or `fopen` left (all
streams are `std::ios::binary`); `.c_str()` of a path only goes to C APIs as
`.string()`; `mktime()` in `Download.cpp` only sees years since 1990.
