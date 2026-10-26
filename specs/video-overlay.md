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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
