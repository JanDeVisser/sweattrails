# Video Overlay: Rendering to Video

Status: Done

The second slice of the video overlay (see the
[roadmap](02-overlay-layout.md#roadmap)). The first,
[12-overlay-preview.md](12-overlay-preview.md), draws a layout in the app; this
one renders it to a video file. The layout format is in
[02-overlay-layout.md](02-overlay-layout.md).

## Goal

The user renders an activity's data as a video with a transparent
background, to put over footage of the ride in Final Cut Pro (or DaVinci
Resolve, Premiere, ...): they stack the overlay on the footage, line the
two up by hand, and do the trimming, audio and color work there.

SweatTrails never touches the footage. The input is an activity, a layout,
a part of the activity, and output settings. Even with only the preview's
text and values, a video with speed and power over the footage is useful,
and it tries the parts that could go wrong (alpha, ffmpeg, the timecode)
before most of the elements exist.

## Feature

### Rendering

- **In the app**, not in a process of its own: raylib draws only on the
  UI thread, which has the OpenGL context, so the app renders a few
  frames in each of its own frames, and stays usable. A separate
  `sweattrails render <job>`, which would give command-line and batch
  rendering, needs the render job file, a second way of starting the
  app, and progress reported between processes: that's deferred.
- **Ctrl+E**, the task `st-overlay-render`, renders the activity on
  display with the layout the preview shows, or would show: the one picked
  last, or `cycling-basic`. A layout with an error isn't rendered: the
  status line says to look at the preview. Like the other keys, Ctrl+E does
  nothing under a modal, or without an activity. It opens a box, "Range", with
  the part to render, which the user can change:
  - the range selected in the graph, or the selected lap, or else the
    whole activity, as elapsed time: `1:12:00-1:20:30`;
  - or typed as times of day, as read off the camera's file info:
    `@09:44:00-@09:52:30`.
  A range that doesn't parse, ends before it starts, or isn't in the
  activity isn't rendered, and the status line says why. A range that only
  partly is, is cut to the activity.
- **Resolution, frame rate and folder** come from `settings.json`, under
  `"overlay"`: `{ "resolution": "1080p", "fps": "30000/1001", "folder":
  "~/Movies" }`, which are also the defaults. They change once per Final
  Cut project, not per render, so the box asks only for the range: there
  are no form widgets, and asking for each would be four boxes on every
  render.
- **The file** is `<folder>/<activity's date>-<range's start>.overlay.mov`,
  such as `2025-06-14-0944.overlay.mov`. An existing file isn't overwritten:
  a number is added.
- **While it renders**, the app stays usable, and the status line says how
  far it is: `Rendering 2025-06-14-0944.overlay.mov: 34%`. At the end it
  says where the file is. One render at a time.
- **Stopping**: Ctrl+E during a render asks whether to stop it, rather
  than starting another, and stopping removes the half-written file.
  Quitting during a render asks too. (Esc is already "whole activity".)
- **ffmpeg failing**, such as with a full disk, stops the render, removes
  the file, and the status line shows ffmpeg's last line of output.
- **A long range** (more than 10 minutes) asks first, with the size it
  will be: ProRes 4444 is about 2.5 GB a minute at 1080p.
- **ffmpeg** does the encoding. It's looked for on the `PATH` and in
  Homebrew's `/opt/homebrew/bin` and `/usr/local/bin`, since an app opened
  from the Finder gets a short `PATH`. If it isn't there, the status line
  says so, and how to install it (`brew install ffmpeg`). The README lists
  it. Not libavcodec, linked in: a much bigger dependency, with a much
  harder API, for the same result.

### Interpolation

Records usually come once a second, and the video has 30 or 60 frames a
second, so values are **interpolated** linearly between the two records
around a frame's time, as in
[Sampling and smoothing](02-overlay-layout.md#sampling-and-smoothing).
`elapsed` runs on with the frame's time. Two records more than 5 seconds
apart (a pause, or a lost signal) aren't interpolated between: the frame
shows the earlier record. The full pause rules, field by field, come with
the other fields in slice 3. The preview stays on records: it shows the
record at the mark.

### Version 1 scope

In: the above, with ProRes 4444, the alpha blend, and the timecode, as in
the technical notes below.

Deferred:

- The render job file, and rendering from the command line or in batches
  *(deferred)*: the app builds the job itself.
- HEVC with alpha *(deferred)*.
- Choosing the resolution and frame rate in the app, rather than in
  `settings.json` *(deferred)*.

## Open questions

None.

## Technical notes

From the first version of this spec. They hold for the Developer, except
where the Feature section above says otherwise: the range and the output
settings come from the app, not a render job file.

### Rendering the overlay

- raylib needs an OpenGL context even for off-screen rendering. In the app
  there already is one. For command-line rendering, call
  `SetConfigFlags(FLAG_WINDOW_HIDDEN)` before `InitWindow`.
- For frame `n`, the activity time is `range.from + n / fps`, computed with
  integer math on the rational frame rate (see
  [02-overlay-layout.md](02-overlay-layout.md#two-files-layout-and-render-job)).
- Draw the layout into a `RenderTexture2D` cleared to `BLANK` (fully
  transparent), with the blend mode described under [Alpha](#alpha).
- Read the pixels back into one buffer that's reused for every frame (with
  `rlReadTexturePixels` or `glReadPixels` into a persistent buffer).
  `LoadImageFromTexture()` allocates a new image every frame, 33 MB at 4K.
- Write the raw RGBA to the stdin of an `ffmpeg` child process, which encodes
  it as ProRes 4444 with alpha:

  ```sh
  ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30000/1001 -i - \
         -vf vflip \
         -c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le \
         -timecode '09:44:00;02' \
         overlay.mov
  ```

  - `vflip` is needed because raylib render textures are stored upside down.
  - The timecode must be quoted, because `;` ends a shell command. When the
    command is started through `Process` with an argument list, no quoting is
    needed. See [Timecode](#timecode) for how it's computed.
- The child process:
  - Read and discard (or log) ffmpeg's stderr while rendering, with
    `on_stderr_read` from `Process.h`. ffmpeg writes progress to stderr all the
    time; if nobody reads it, the pipe fills up, ffmpeg blocks, and the render
    deadlocks.
  - Handle ffmpeg exiting early (bad arguments, disk full): writing to its
    stdin then fails with `EPIPE`, or kills the app with `SIGPIPE` unless
    that's ignored. Report ffmpeg's last stderr lines to the user.
  - Close stdin after the last frame and wait for ffmpeg to exit before
    reporting success, because it's still writing the end of the file.

Rendering runs off-screen and doesn't have to keep up with real time, so the
render loop simply produces frames as fast as it can.

Estimated size: a few hundred lines for the encoding loop and the child
process handling. The layout renderer is much bigger (see
[02-overlay-layout.md](02-overlay-layout.md#roadmap)).

### Output settings

- **Resolution:** pick the resolution of the Final Cut project, not
  necessarily that of the footage. A preset (`1080p`, `4k`, ...) or explicit
  width and height, with portrait variants for vertical video.
- **Frame rate:** should match the project. If it doesn't, Final Cut drops or
  repeats overlay frames to fit the project's rate, which makes the numbers
  and the map marker judder. Use the exact rational rate (e.g. `30000/1001`
  for 29.97).
- **Range:** a whole ride can be several hours. At 30 fps, three hours is
  324,000 frames, and as ProRes 4444 hundreds of GB. Usually only a few
  stretches of the ride have footage, so the render job's `range` limits the
  render to one stretch, e.g. `1:12:00` to `1:20:30`, or `@09:44:00` to
  `@09:52:30` in time of day. With no range the whole activity is rendered,
  after a warning about the expected size.

### Codec

- **ProRes 4444** (default) is the standard intermediate codec with alpha,
  and every NLE imports it. Files are big: about 2.5 GB per minute at 1080p
  29.97, in line with Apple's target data rates.
- *(Deferred)* **HEVC with alpha** (macOS only) gives much smaller files and
  Final Cut supports it:

  ```sh
  ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30000/1001 -i - \
         -vf vflip,format=bgra \
         -c:v hevc_videotoolbox -alpha_quality 0.75 -q:v 65 -tag:v hvc1 \
         overlay.mov
  ```

  The encoder only keeps the alpha channel when it gets `bgra` input, so the
  conversion is explicit. Without `-q:v` (or `-b:v`) the default bitrate is
  low. These flags haven't been tested
  yet.

The render job's `codec` field (`prores4444` or `hevc-alpha`) picks one.

### Alpha

raylib's default blend mode (`BLEND_ALPHA`) applies
`src × alpha + dst × (1 − alpha)` to the alpha channel as well as the colors.
Drawing onto a transparent texture with it gives the wrong alpha: a 50%
transparent shadow ends up about 25% opaque, and the colors come out
premultiplied. That's fine on screen, where the window has no alpha, but not
for an overlay.

So the overlay is drawn with a custom blend mode that blends colors the usual
way but adds up alpha correctly:

```cpp
rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,  // color
                          RL_ONE, RL_ONE_MINUS_SRC_ALPHA,        // alpha
                          RL_FUNC_ADD, RL_FUNC_ADD);
BeginBlendMode(BLEND_CUSTOM_SEPARATE);
```

The colors in the texture are then premultiplied by alpha. Before writing a
frame, unpremultiply it (divide each color by alpha, for pixels with alpha >
0) so the file contains straight alpha, which is what ProRes 4444 normally
holds and what editors assume. If edges still look dark or fringed in Final
Cut, check the clip's alpha handling setting (in the Info inspector, as far as
we know; not verified) and set it to "Straight".

### Timecode

The overlay's start timecode is the local time of day of its first frame:
the activity start time plus `range.from`, converted with
`Activity.utc_offset`. So the example render job (activity started at 08:32,
range from `1:12:00`) gets a timecode starting at 09:44:00.

- **29.97 and 59.94 fps** use drop-frame timecode, which skips frame numbers
  `;00` and `;01` at the start of every minute except every tenth minute, so
  that the timecode keeps up with the clock. Those labels don't exist:
  `09:44:00;00` is invalid and the first valid label of that minute is
  `09:44:00;02`. Compute the frame count since midnight as
  `round(seconds_since_midnight × 30000 / 1001)` and convert it to a
  drop-frame label with the standard algorithm (as in ffmpeg's
  `av_timecode_make_string`).
- **23.976 fps** has no drop-frame timecode. A time-of-day timecode there
  drifts from the real clock by about 3.6 seconds per hour. That's acceptable,
  since the timecode is only a guide for lining things up by hand.
- **Integer frame rates** (24, 25, 30, 60) simply use `HH:MM:SS:FF`.

### Lining up the overlay in Final Cut

The overlay is just a clip on the timeline. The user slides it until the data
matches the footage, for example by finding a moment that's easy to spot in
both: setting off, stopping at a junction, cresting a climb. Two things make
this easier at no extra cost:

- **Time-of-day timecode:** Final Cut shows clip timecode, so the user can read
  off which overlay frame corresponds to, say, 09:45:10. This also makes
  renders of different ranges of the same activity easy to tell apart.
- **Ranges with a margin:** render a bit more than the stretch the footage
  covers, so there's room to slide the overlay before trimming it.

Lining up automatically (from the footage's creation date or a GoPro's GPS
telemetry) would need the video as input again. That's deliberately left out
for now. It can be added later as an optional convenience if lining up by hand
turns out to be tedious.

## Architecture

The code goes in `src/App/overlay/`, next to the preview's. As there, the
logic is plain functions the tests reach; what needs raylib or a child
process is kept thin.

### Drawing: one renderer for the preview and the video

- **`OverlayRenderer`** in `overlay/Renderer.h`: the preview's drawing
  (fonts by `layout_fonts()`, a `FontCache`, measuring, `place()`,
  `place_in_box()`, shadows) moves out of `OverlayPreview` into it, and
  draws a layout's elements for a frame's field values into a rectangle,
  on screen or in a render texture. The preview and the render each have
  one, since their fonts are at different sizes.
- **`FieldValues`** in `Fields.h`: the values of all the fields for one
  frame, with the activity's sport. `values_at_record()` is what the
  preview shows; `values_at_time(activity, seconds)` interpolates for the
  video. `fill_template()` and the renderer take a `FieldValues` instead
  of an activity and a record.
- **Interpolation**: the records around the time, found by their
  timestamps; linear between them where both have a value, else the
  earlier record's; the earlier record's alone when they're more than 5 s
  apart. `elapsed` is the time itself.

### The render: `overlay/Render.h`

- **`RenderSettings`**, decoded from `settings.json`'s `"overlay"`:
  resolution (the presets and their `-portrait` variants, or a width and
  height), frame rate as a rational (`30000/1001`, or an integer), and the
  folder, `~` expanded. Errors say which setting is wrong.
- **`RenderRange`**, seconds since the activity's start: `parse_range()`
  reads `1:12:00-1:20:30` and `@09:44:00-@09:52:30`, and cuts it to the
  activity; `format_range()` writes the one the box starts with, from the
  graph's selection, the lap, or the whole activity. Time of day is local
  to where the activity was, by `utc_offset`, or the system's time zone.
  `@9:44` is `@09:44:00`. A time of day is on the day closest to the
  activity, for a ride through midnight or one starting just after it,
  and an end that's a time of day comes after the start, within a day. A
  little before the start is cut to the start.
- **Frames**: frame `n`'s time is `from + n × den / num`, from integers,
  not added up. The count is `floor((to − from) × num / den)`.
- **`timecode(seconds since midnight, fps)`**: `HH:MM:SS:FF`, or drop
  frame, `HH:MM:SS;FF`, for 29.97 and 59.94, by the standard algorithm.
- **`output_path(folder, date, start, exists)`**: the file's name, with a
  number added while `exists()` says it's taken.
- **`find_program(name, path, extra_dirs)`**: ffmpeg on the `PATH`, or in
  `/opt/homebrew/bin` and `/usr/local/bin`.
- **`unpremultiply()`**: straight alpha for a frame's pixels, as the
  technical notes say.
- **`records_range(activity, first, last)`**: the range the graph's
  selection or the lap covers, for the box to start with.
- **`ffmpeg_arguments()`**: as in the technical notes, plus
  `-hide_banner -loglevel error`, so that the log's last line is the
  error, and `-n`: ffmpeg would ask about overwriting a file on its
  stdin, which has the frames.

### ffmpeg: `overlay/Encoder.h`

- **`Encoder`** starts ffmpeg with `posix_spawnp`, writes frames to its
  stdin, and sends its stderr to a log file in the temporary directory,
  whose last line is the error when it fails. **Not `Process.h`**: it
  writes through `std::format`, which would mangle pixels with a `{` in
  them; it can't close the child's stdin before waiting, and ffmpeg only
  finishes when its stdin closes; and its reading threads are detached
  with a pointer to the process, so they can outlive it. Reading stderr
  into a file instead of a pipe also means ffmpeg can never block on it.
- Frames are written on a **thread of the encoder's own**, from a queue
  of two frames, so that the UI thread doesn't wait for ffmpeg; the queue
  being full is what slows the rendering down to ffmpeg's speed. That
  thread also unpremultiplies.
- `SIGPIPE` is ignored, as `OAuth.cpp` does already, so that ffmpeg
  exiting early is an `EPIPE` from `write()`, not the end of the app.
- **`stop()`** kills ffmpeg (`SIGKILL`: the file goes anyway) and removes
  the file; **`close()`** says there are no more frames, after which the
  writer thread closes ffmpeg's stdin and waits for it, so that the UI
  doesn't, and `result()` says how it went; **`finish()`** closes and
  waits, for the tests. ffmpeg is reaped only once `stop()` can see it
  has exited, so that it never kills a process that got its pid. The log is
  removed either way, since the error has its last line. Tests use
  `/bin/sh` as a stand-in for ffmpeg.

### In the app

- **`OverlayRender`** (`overlay/OverlayRender.h`), owned by
  `SweatTrails` while a render runs: a
  `RenderTexture2D` at the output size, the renderer, the encoder, and the
  frame it's at. `SweatTrails::process_input()` gives it a step each UI
  frame: it renders frames, with the separate alpha blend mode, and reads
  each back with `glReadPixels` into one buffer per queue slot, until about
  10 ms have gone, or the queue is full. The status line shows the percentage.
  The app now links OpenGL itself, for `glReadPixels`.
- **Ctrl+E** (`st-overlay-render`): the layout as the preview would show
  it, the settings, `input_box()` for the range, a `query_box()` for a
  long one, then the render. During a render it asks whether to stop.
  `cmd_quit` asks first, too, while one runs.
- **Modals** (Widget library): a box removes itself when it closes,
  rather than the top modal, so that the range box's submit function can
  open the question about a long range.
- README: ffmpeg in the requirements, and rendering in the overlay
  section.

## Test plan

- `OverlayFieldsTest`: `values_at_time()` between records, at and past the
  ends, over a gap of more than 5 s, with a field one record lacks;
  `values_at_record()`; templates on `FieldValues`.
- `OverlayRenderTest` (new): `RenderSettings` (presets, portrait, explicit
  sizes, rational and integer rates, errors); `parse_range()` and
  `format_range()` (elapsed, time of day with and without `utc_offset`,
  cut to the activity, errors); frame times and counts over a long range,
  without drift; `timecode()` for 25, 30, 29.97 (including the skipped
  labels at minutes that aren't tens) and 59.94; `output_path()` with
  taken names; `find_program()` in temporary directories;
  `unpremultiply()`.
- `EncoderTest` (new): with `/bin/sh -c 'cat > file'`, the frames arrive
  whole and in order; with `/bin/sh -c 'exit 3'` writing fails, and the
  error has the log's last line; `stop()` removes the file.
- By hand, with ffmpeg installed: a render of a few minutes at 1080p and
  29.97 imported in Final Cut, its alpha and timecode, values gliding
  between records, Ctrl+E to stop, quitting during a render, a long range's
  question, and ffmpeg missing.

## Review notes

1. **Bug: a render that fails to start hangs the app.** If
   `OverlayRender::start()` returns early, before it has an encoder
   (`OverlayRender.cpp:36-45`: the layout's font isn't found, or ffmpeg
   can't be started), the `unique_ptr` destroys the render.
   `~OverlayRender()` (line 57) sees `!over()`, calls `stop()`, and that
   calls `m_encoder->stop()` (line 112) on a null `m_encoder`. In a test
   that's an endless loop in `Encoder::stop()`. In the app: a layout with
   a font that isn't installed (the preview shows the error, but Ctrl+E
   only checks `file->layout`), then Ctrl+E, Enter, and the app hangs or
   crashes instead of showing "Couldn't render the overlay: ...". Fix:
   `stop()` and the destructor check `m_encoder`, or `start()` sets
   `m_result` before it returns an error. `overlay_render_start_test`
   shows it, for both causes.
2. **Bug: a time of day just before a ride that starts after midnight
   isn't cut to the start.** `range_point()` (`Render.cpp:150-153`) only
   moves a time of day that's more than 12 hours *before* the start to the
   next day, never one more than 12 hours *after* it to the day before. A
   ride started at 00:10 with `@23:55-@00:20` reads 23:55 as 23:45 after
   the start, and says "ends before it starts", where the spec says a
   little before the start is cut to the start. Fix: take the end relative
   to the start of the range (a day later if it's earlier), and the start
   as whichever of `t - day`, `t`, `t + day` is closest to the activity.
   `test_ranges_at_midnight` in `OverlayRenderTest` fails on it.
3. **Bug: a font that can't be loaded renders a blank movie, reported as
   done.** `OverlayRenderer::font()` sets `m_error` on the first frame if
   raylib can't load a font that was found (a broken file), and from then
   on `draw()` (`Renderer.cpp:54`) draws nothing. `OverlayRender::step()`
   never looks at `m_renderer.error()`, so every frame is transparent and
   the status line says "Rendered the overlay to ...". Fix: after
   `render_frame()`, stop the render with the renderer's error.
4. **Suggestion: ffmpeg inherits the app's open file descriptors.**
   `Encoder::start()` (`Encoder.cpp:83-104`) marks only its own pipe's
   write end close-on-exec, and only after `pipe()`. ffmpeg inherits
   every descriptor the app has without `FD_CLOEXEC`, such as the Wahoo
   login's listening socket (`OAuth.cpp` makes it without), which then
   stays bound for as long as the render runs; and a `Process` forked on
   a job thread between `pipe()` and `fcntl()`/`close()` (Strava's curl)
   inherits the encoder's pipe, so ffmpeg wouldn't see the end of its
   input until curl exits. Fix: `posix_spawnattr_setflags(...,
   POSIX_SPAWN_CLOEXEC_DEFAULT)` on macOS, which leaves the child only
   the descriptors the file actions give it, and `FD_CLOEXEC` on both
   ends.
5. **Suggestion: the end of a render blocks the UI.** `step()` calls
   `finish()` on the UI thread (`OverlayRender.cpp:80-82`), which joins
   the writer while it writes up to two frames (8 MB each at 1080p, 33 MB
   at 4K) to ffmpeg, and then waits for ffmpeg to encode them and write
   the file's index. That's a visible stall at the end. It could be done
   on the writer thread, with `step()` polling for it.
6. **Suggestion: the range box can start with an empty range.** A graph
   selection of one record (`segment->min == segment->max`) gives
   `records_range()` a range of length 0 (`SweatTrails.cpp`, in
   `cmd_overlay_render`), so the box starts with e.g. `1:00:00-1:00:00`,
   which, submitted as it is, says "ends before it starts". Falling back
   to the whole activity for an empty selection would avoid that.
7. **Suggestion: fit.** `Encoder` and `OverlayRender` are `class` with
   `public:`/`private:`, where `STYLE.md` says `struct`. In
   `SweatTrails.cpp:1060`, set_status's comment, "Must be called on the
   UI thread", now sits above `RenderBudget`, away from `set_status()`.
8. **Missing tests (added by the Reviewer).** `EncoderTest`: no file
   descriptors left open and no child left after finishing, failing and
   stopping; `stop()` while the writer is stuck on a program that doesn't
   read; a program that exits with 0 before it has all the frames. All
   pass. `OverlayRenderTest`: every drop-frame label for two hours at
   29.97 and 59.94 against labels counted one by one, timecodes at
   midnight, `activity_clock()` without a `utc_offset` (the Test plan's
   "without `utc_offset`" wasn't tested), and ranges around midnight (one
   fails: item 2). `OverlayRenderStartTest` (new): starting without
   ffmpeg, and with a font that isn't there (both fail: item 1).

### Resolution

Jan agreed to all of them; each fix is its own commit.

1. Fixed: `OverlayRender::stop()` checks for an encoder.
   `OverlayRenderStartTest` passes.
2. Fixed: a time of day is on the day closest to the activity, and an
   end that's a time of day comes after the start, within a day.
   `test_ranges_at_midnight` passes.
3. Fixed: `step()` stops the render with the renderer's error. No test:
   loading a font needs raylib.
4. Fixed: `POSIX_SPAWN_CLOEXEC_DEFAULT` where there is one, and both ends
   of the pipe close-on-exec (`pipe2` on Linux). `test_no_files_inherited`
   fails without it.
5. Fixed: the writer thread closes ffmpeg's stdin and waits for it after
   the last frame; `step()` polls `Encoder::result()`. ffmpeg is reaped
   only once `stop()` can see it has exited. `test_close_doesnt_wait`.
6. Fixed: a selection of one record starts the box with the whole
   activity.
7. Fixed: both are structs, and the comment is back above `set_status()`.
8. Kept, all passing.
