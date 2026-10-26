# Video Overlay: Rendering to Video

Status: Ready

The second slice of the video overlay (see the
[roadmap](overlay-layout.md#roadmap)). The first,
[overlay-preview.md](overlay-preview.md), draws a layout in the app; this
one renders it to a video file. The layout format is in
[overlay-layout.md](overlay-layout.md).

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
[Sampling and smoothing](overlay-layout.md#sampling-and-smoothing).
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
  [overlay-layout.md](overlay-layout.md#two-files-layout-and-render-job)).
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
[overlay-layout.md](overlay-layout.md#roadmap)).

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
- **`stop()`** kills ffmpeg and removes the file; **`finish()`** closes
  its stdin and waits for it. Tests use `/bin/sh` as a stand-in for ffmpeg.

### In the app

- **`OverlayRender`**, owned by `SweatTrails` while a render runs: a
  `RenderTexture2D` at the output size, the renderer, the encoder, and the
  frame it's at. `SweatTrails::process_input()` gives it a step each UI
  frame: it renders frames, with the separate alpha blend mode, and reads
  each back with `glReadPixels` into one buffer per queue slot, until about
  10 ms have gone, or the queue is full. The status line shows the percentage.
- **Ctrl+E** (`st-overlay-render`): the layout as the preview would show
  it, the settings, `input_box()` for the range, a `query_box()` for a
  long one, then the render. During a render it asks whether to stop.
  `cmd_quit` asks first, too, while one runs.
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

*Filled in by the Reviewer.*
