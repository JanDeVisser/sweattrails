# Video Overlay: Transparent Activity Data Overlay for MOV Files

Goal: given a video recorded during a ride or run (e.g. an iPhone or GoPro
`.mov`), produce a second video containing only the activity data (speed,
heart rate, the map, etc.) on a transparent background. The user imports both
into Final Cut Pro (or DaVinci Resolve, Premiere, ...), stacks the overlay on
top of the footage, and does the compositing, trimming, audio and color work
there.

SweatTrails never decodes or re-encodes the footage itself. It only reads the
source video's metadata and renders the overlay.

What the overlay contains (fields, positions, fonts, map) is defined in
[overlay-layout.md](overlay-layout.md). This document covers producing the
`.mov` file.

## Rendering the overlay

- Read the source video's metadata with `ffprobe` (see
  [Matching the source](#matching-the-source)).
- For each output frame, compute the activity time for that frame (see
  [Synchronizing video and activity data](#synchronizing-video-and-activity-data)),
  then draw the layout with raylib into a `RenderTexture2D` cleared to `BLANK`
  (fully transparent).
- Read the frame back with `LoadImageFromTexture()` and pipe the raw RGBA to
  an `ffmpeg` child process, which encodes it as ProRes 4444 with alpha:

  ```sh
  ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30 -i - \
         -vf vflip \
         -c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le \
         overlay.mov
  ```

  `vflip` is needed because raylib render textures are stored upside down.

Rendering runs off-screen and doesn't have to keep up with real time, so the
render loop simply produces frames as fast as it can.

Estimated size: about 100–200 lines of code, not counting the layout renderer.
There's no decoding and no audio handling.

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

## Matching the source

The overlay must line up with the footage frame for frame and must not be
scaled in the editor. So the overlay copies these from the source:

- **Resolution:** use the *displayed* dimensions. iPhone portrait videos are
  stored landscape with a rotation in the display matrix, so swap width and
  height when the rotation is ±90°.
- **Frame rate:** use the exact rational rate (e.g. `30000/1001`, not `29.97`),
  otherwise the overlay drifts on long clips.
- **Duration:** the overlay covers the whole clip by default. The render job's
  `range` can limit it.
- **Timecode:** copy the source's start timecode (`-timecode HH:MM:SS:FF`), if
  it has one, so the editor can line the two clips up automatically.

`canvas: "source"` in the layout refers to these values.

## Alpha

raylib produces straight (unpremultiplied) alpha. If edges look dark or
fringed in the editor, set the clip's alpha handling to "Straight" (in Final
Cut: Info inspector → Alpha Handling).

## Synchronizing video and activity data

The video's timeline has to be aligned with the activity's timestamps. The
render job's `offset` is the activity time, in seconds, at video frame 0.

- iPhone MOVs include `com.apple.quicktime.creationdate`, which FFmpeg exposes
  in the format metadata. That gives the initial offset from the start of the
  activity. A manual nudge to correct the offset is probably still needed.
- GoPro files carry GPS and sensor telemetry (GPMF) in a separate stream,
  already in sync with the video. Matching that GPS track against the
  activity's track could compute the offset automatically. (Possible later
  addition.)

To make the manual nudge practical, the app should preview the overlay over a
still frame from the video (extracted with
`ffmpeg -ss <t> -frames:v 1`) while the user adjusts the offset.
