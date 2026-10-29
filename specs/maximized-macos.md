# Starting maximized on macOS

Status: In review

## Goal

On macOS, the app starts in a normal window that fills the screen, with
its title bar and buttons, as other apps' windows do: it doesn't cover
the other apps, and it can be found in Mission Control and moved,
resized or minimized like any other window.

## Feature

### What there is now

The app starts with the raylib flag `FLAG_WINDOW_MAXIMIZED`. On macOS,
this gives a window without decorations which appears to lie over top of
all other apps on the same monitor/desktop, making these apps
inaccessible. Pressing Ctrl+Up (Mission Control) shows all apps, but
SweatTrails appears to be missing from it. It's all very confusing and
poor UX.

What's wanted is what's known in Linux and Windows lingo as maximized:
the window covers the entire desktop, but has window decorations (close
buttons, borders, etc.).

The likely cause, from `App::create()` in `src/Lib/Widget/App.h`: the
window is created with `InitWindow(0, 0, ...)`, as the viewport starts
out empty, and raylib makes a window of size 0×0 as large as the whole
monitor. On macOS, that window lies over the menu bar and the Dock.
`SetWindowMonitor()`, `FLAG_WINDOW_MAXIMIZED` and `MaximizeWindow()`
come after that, on a window that's already as large as it can be.
raylib has no way to ask for a monitor's usable area (the screen less
the menu bar and the Dock); `MaximizeWindow()` asks the window system to
work that out, which on macOS is the window's zoom.

### Starting

- The app starts in a normal window, with its title bar and its close,
  minimize and zoom buttons, maximized: on macOS, zoomed, filling the
  screen below the menu bar and beside the Dock, as double-clicking a
  title bar does. It doesn't cover other apps, shows in Mission Control
  and the Dock's window list, and can be moved, resized and minimized.
- It isn't macOS's own full screen, in a Space of its own: that's
  further from what's wanted, and raylib's full screen isn't the native
  kind.
- Un-maximizing it (double-clicking the title bar, or the zoom button)
  gives a window two thirds of the monitor's width and height, centred on
  it: a sensible size on a laptop and on a large screen alike. That's
  the size the window is created at, before it's maximized.
- It starts on the monitor it was last on, as now. If that monitor isn't
  connected any more, it starts on the main monitor, which is remembered
  from then on.
- The rest of the app follows the window's size as it does now when the
  window is resized.

### Other platforms

Linux and Windows start the same way, with the same code: a window of
two thirds of the monitor, then maximized. That's what maximized means
there too, so they should stay as they are, or get better. Only macOS
can be tried by hand for now; the README says that starting maximized
hasn't been checked on Linux and Windows since this change.

### Version 1 scope

In: starting maximized, with the title bar, on the monitor the app was
last on or else the main one; two thirds of the monitor when
un-maximized; the same code on every platform.

*(Deferred)*:

- Keeping the window's size and position, and whether it was maximized,
  between runs. Restoring them has edge cases, such as a monitor that's
  gone or a window off-screen.
- A key to switch to full screen and back.

## Open questions

None.

## Architecture

The change is in `App::create()` (`src/Lib/Widget/App.h`), with the
arithmetic in a new `src/Lib/Widget/Window.h` and `Window.cpp`, where a
test can reach it without a window.

### Where and how large (`Window.h`)

```cpp
// The monitor to start on: the one saved, if it's still connected, else
// the main one, 0.
int start_monitor(int saved, int monitor_count);

// The window before it's maximized, and after it's un-maximized: two
// thirds of the monitor's width and height, centred on it. Nothing if
// that leaves no window, for a monitor raylib gives no size for: the
// window then stays at the size it was made at.
std::optional<Rectangle> restored_window(Vector2 monitor_position, Vector2 monitor_size);
```

### Starting (`App::create()`)

1. `SetConfigFlags()` asks for `FLAG_WINDOW_RESIZABLE` and
   `FLAG_WINDOW_HIDDEN` besides the multisampling, before the window is
   made. raylib's `MaximizeWindow()` does nothing to a window that isn't
   resizable, and a hidden window doesn't flash at the wrong size and
   place.
2. `InitWindow()` with a small size, such as 800×600, rather than 0×0,
   which raylib makes as large as the whole monitor: that's the window
   that covers the menu bar and the Dock. The monitors can only be asked
   about once there's a window.
3. `start_monitor(app->monitor, GetMonitorCount())` becomes
   `app->monitor`, so the main loop saves it if it changed.
4. `SetWindowSize()` and `SetWindowPosition()` with
   `restored_window()` for that monitor, which is also where the window
   goes when it's un-maximized: macOS remembers the size a window had
   before it was zoomed.
5. `ClearWindowState(FLAG_WINDOW_HIDDEN)`, then `MaximizeWindow()`,
   which is glfw's maximize: on macOS, the zoom, which fills the screen
   less the menu bar and the Dock.
6. The viewport takes the window's size after that, as now. If
   maximizing finishes later, `IsWindowResized()` in
   `App::start_frame()` picks the new size up. That was
   `on_process_input()`, which a modal skipped: it now runs at the top of
   `App::process_input()` in every frame, and leaves the window alone
   when there isn't one, as in tests (review note 1).

`SetWindowMonitor()` goes, as does `FLAG_WINDOW_MAXIMIZED` in
`SetWindowState()`, which only calls `MaximizeWindow()` again:
`SetWindowPosition()` puts the window on its monitor. Monitor positions
and sizes are in screen coordinates on every platform, as glfw gives
them, so on a Retina screen they're points, not pixels.

`FLAG_VSYNC_HINT` stays in `SetWindowState()`.

## Test plan

- `WindowTest` (`window_test`):
  - `start_monitor()` keeps a monitor that's connected, the first and
    the last, and falls back to 0 for one past the last, a negative one,
    and with no monitors at all.
  - `restored_window()` on a monitor at the origin, on one to the right
    of it and on one with a negative position (left of the main one),
    and on an odd size, where two thirds isn't whole.
- By hand, on macOS: the window starts zoomed with its title bar, below
  the menu bar and beside the Dock; other apps can be reached; it shows
  in Mission Control; un-maximizing gives two thirds of the screen,
  centred; with two monitors, it starts on the last one, and on the main
  one when that's unplugged. What the window looks like can't be tested
  without a window system.
- Linux isn't tried; the README says so. Windows isn't supported at all
  (see the README), so there's nothing to try there.

## Review notes

1. **Bug** (not introduced here, but step 6 of Starting relies on it):
   `App::on_process_input()` (`src/Lib/Widget/App.cpp:121`) only runs
   from `Layout::process_input()`, and `App::process_input()`
   (`src/Lib/Widget/App.cpp:172-182`) skips that while a modal is open.
   `IsWindowResized()` is only true for one frame, so a resize that lands
   while a modal is up is lost: click the close button (the quit prompt
   opens), then double-click the title bar to un-maximize. The window
   shrinks, but the viewport keeps the maximized size until the next
   resize with no modal open, so the layout runs off the window. On
   Linux, where the window manager maximizes later, this also happens if
   a modal opens in the first frames. `monitor` isn't followed under a
   modal either. Fix: do the resize and monitor check in
   `App::process_input()` before the modal branch.
2. **Suggestion**: `src/Lib/Widget/App.h:152-155` trusts
   `GetMonitorWidth()`/`GetMonitorHeight()`, which return 0 (with a
   warning) when glfw has no video mode for the monitor.
   `restored_window()` then gives 0x0 (see
   `test_restored_window_no_size`), and `SetWindowSize(0, 0)` makes the
   restored window empty, so un-maximizing gives nothing to grab. Keeping
   the 800x600 window when the size comes back as 0 would be safer.
3. **Suggestion**: on Linux under Wayland, glfw can't set a window's
   position, so `SetWindowPosition()` (`src/Lib/Widget/App.h:155`) logs a
   GLFW error at warning level on every start, and `GetCurrentMonitor()`
   can't tell where the window is, so the saved monitor isn't
   meaningful. Linux is untried, as the README says, but the README
   could mention X11 vs Wayland when someone tries it.
4. **Missing test** (added to `test/WindowTest.cpp`): `restored_window()`
   where what's left over is odd, so the offset is rounded down
   (1003x1004, at the origin and at a negative position), on a Retina
   MacBook's size in points (1512x982), on a monitor of 0x0, and a sweep
   of sizes 1 to 8000 at three positions. The sweep checks that the
   window is whole, exactly two thirds rounded down, on the monitor, and
   centred to within one. All of them pass.

Checked, and found right: the raylib calls are in the order the
Architecture gives. raylib 6.2 hides the window for `FLAG_WINDOW_HIDDEN`,
shows it in `ClearWindowState()`, and only maximizes a resizable one. The
viewport is read after `MaximizeWindow()`, and a maximize that finishes
later comes in through `IsWindowResized()` on the next frame (apart from
note 1). `SetWindowSize()`/`SetWindowPosition()` and
`GetMonitorWidth()`/`GetMonitorPosition()` are all glfw screen
coordinates, which are points on macOS, so HiDPI doesn't skew the
arithmetic. Falling back to monitor 0 is saved: `create()` sets
`monitor`, `SweatTrails::on_start()` replaces it with
`GetCurrentMonitor()` (the same monitor, as the window is on it now), and
the first `Main::process_input()` writes it if it differs from the state
file. The float arithmetic is exact for integer sizes, so whole
multiples of three don't round down by one.

### Resolution

1. Fixed: `App::on_process_input()` became `App::start_frame()`, at the
   top of `App::process_input()`, so the time, the frame count, resizes
   and the current monitor are followed under a modal too. It doesn't
   ask raylib about the window when there isn't one, as in tests.
   `test_frames_start_under_a_modal` in `window_test` fails on the old
   code and passes.
2. Fixed: `restored_window()` returns nothing when two thirds of the
   monitor leaves no window, and `App::create()` then keeps the 800x600
   window. `test_restored_window_no_size` checks 0x0, no height, and a
   pixel wide.
3. The README says that Wayland doesn't let the app place its window,
   so it may not start on the last monitor, and that glfw warns about
   it. The code stays as it is until Linux is tried.
4. The review's tests are committed. The sweep starts at 2, as a monitor
   one wide now gives no window.
