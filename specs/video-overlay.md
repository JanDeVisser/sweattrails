# Video Overlay: Transparent Activity Data Overlay

Goal: render an activity's data (speed, heart rate, the map, etc.) as a video
with a transparent background. The user imports it into Final Cut Pro (or
DaVinci Resolve, Premiere, ...) together with footage recorded during the ride
or run, stacks the overlay on top, lines the two up by hand, and does the
trimming, audio and color work there.

SweatTrails never touches the footage. The input is just an activity, a layout
and output settings: "render this activity with this layout at 1080p, 29.97
fps". What the overlay contains (fields, positions, fonts, map) and the render
job format are defined in [overlay-layout.md](overlay-layout.md).

## Rendering the overlay

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
[overlay-layout.md](overlay-layout.md#version-1-scope)).

## Output settings

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

## Codec

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

## Alpha

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

## Timecode

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

## Lining up the overlay in Final Cut

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
