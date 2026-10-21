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

- For each output frame, compute the activity time for that frame: the start
  of the render job's `range` plus `frame / fps`. Then draw the layout with
  raylib into a `RenderTexture2D` cleared to `BLANK` (fully transparent).
- Read the frame back with `LoadImageFromTexture()` and pipe the raw RGBA to
  an `ffmpeg` child process, which encodes it as ProRes 4444 with alpha:

  ```sh
  ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30000/1001 -i - \
         -vf vflip \
         -c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le \
         -timecode 08:32:00;00 \
         overlay.mov
  ```

  `vflip` is needed because raylib render textures are stored upside down.

Rendering runs off-screen and doesn't have to keep up with real time, so the
render loop simply produces frames as fast as it can.

Estimated size: about 100 lines of code, not counting the layout renderer.
There's no video decoding, no audio, and no metadata to read.

## Output settings

- **Resolution:** pick the resolution of the Final Cut project, not
  necessarily that of the footage. A preset (`1080p`, `4k`, ...) or explicit
  width and height, with portrait variants for vertical video.
- **Frame rate:** should match the project. Use the exact rational rate (e.g.
  `30000/1001` for 29.97), otherwise Final Cut conforms the clip and the data
  drifts slightly against the timeline over long clips.
- **Range:** a whole ride can be several hours. At 30 fps, three hours is
  324,000 frames, and as ProRes 4444 hundreds of GB. Usually only a few
  stretches of the ride have footage, so the render job's `range` limits the
  render to one stretch of activity time, e.g. `1:12:00` to `1:20:30`. A
  sensible default when no range is given is the whole activity, with a warning
  about the expected size.

## Codec

- **ProRes 4444** (default) is the standard intermediate codec with alpha,
  and every NLE imports it. Files are big, up to a couple of GB per minute at
  1080p.
- **HEVC with alpha** (macOS only) gives much smaller files and Final Cut
  supports it:

  ```sh
  -c:v hevc_videotoolbox -alpha_quality 0.75 -tag:v hvc1
  ```

The render job's `codec` field (`prores4444` or `hevc-alpha`) picks one.

## Alpha

raylib produces straight (unpremultiplied) alpha. If edges look dark or
fringed in the editor, set the clip's alpha handling to "Straight" (in Final
Cut: Info inspector → Alpha Handling).

## Lining up the overlay in Final Cut

The overlay is just a clip on the timeline. The user slides it until the data
matches the footage, for example by finding a moment that's easy to spot in
both: setting off, stopping at a junction, cresting a climb. Two things make
this easier at no extra cost:

- **Time-of-day timecode:** the overlay's start timecode is set to the local
  time of day of the first rendered frame (`-timecode`). Final Cut shows clip
  timecode, so the user can read off which overlay frame corresponds to, say,
  08:45:10. This also makes renders of different ranges of the same activity
  easy to tell apart.
- **Ranges with a margin:** render a bit more than the stretch the footage
  covers, so there's room to slide the overlay before trimming it.

Lining up automatically (from the footage's creation date or a GoPro's GPS
telemetry) would need the video as input again. That's deliberately left out
for now. It can be added later as an optional convenience if lining up by hand
turns out to be tedious.
