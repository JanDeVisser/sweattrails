# Video Overlay: Reading MOV Files and Superimposing Activity Data

Goal: take a video (e.g. an iPhone or GoPro `.mov` recorded during a ride or
run) and superimpose a layer with activity data such as speed, heart rate, or
the map.

How hard this is depends on whether we export a new video or play the video
live inside the app. Both options assume FFmpeg does the heavy lifting.

## Option 1: Export a video with the overlay burned in (easy)

Let FFmpeg handle the video. SweatTrails only draws the overlay.

- Draw the overlay with raylib into a `RenderTexture2D` with a transparent
  background.
- Send each frame to an `ffmpeg` child process, either by writing PNGs or by
  piping raw RGBA to its stdin:

  ```sh
  ffmpeg -i ride.mov \
         -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30 -i - \
         -filter_complex "[0:v][1:v]overlay" -c:a copy out.mp4
  ```

- Estimated size: about 100–200 lines of code. No codec handling, and audio is
  copied through unchanged.

## Option 2: Play the MOV in the app with a live overlay (moderate)

Decode the video ourselves and draw it as a raylib texture under the UI.

- **Decode with libavformat/libavcodec:** open the MOV, find the video stream,
  decode the frames, and convert them to RGBA with `libswscale`. Call
  `UpdateTexture()` each frame and draw the overlay on top with ordinary raylib
  calls.
- **macOS-only alternative:** `AVAssetReader` from AVFoundation in a small
  Objective-C++ file. Hardware decoding comes for free, but it doesn't work on
  other platforms.

Estimated size: about 300–500 lines for a basic version. The decoding loop is
boilerplate. The real work is in these details:

- **A/V sync and frame timing:** use the frame PTS, not the render loop's frame
  rate.
- **Audio:** feed decoded samples into a raylib `AudioStream`, or skip audio.
- **iPhone quirks:** HEVC, 10-bit/HDR, and rotation metadata. A portrait video
  shows up sideways unless the display matrix is applied.
- **Performance:** decoding 4K HEVC in software is slow. Use VideoToolbox
  hardware decoding through `av_hwdevice_ctx_create`.
- **Seeking:** seek to a keyframe, then decode forward to the target frame.

## Synchronizing video and activity data

The video's timeline has to be aligned with the activity's timestamps.

- iPhone MOVs include `com.apple.quicktime.creationdate`, which FFmpeg exposes
  in the format metadata. That gives an offset from the start of the activity.
  A manual nudge to correct the offset is probably still needed.
- GoPro files carry GPS and sensor telemetry (GPMF) in a separate stream, so
  that data is already in sync with the video.

## Recommendation

If the goal is to share rides as videos, start with option 1; it's an
afternoon's work. Option 2 is only worth it for scrubbing through the video
inside the activity screen. The overlay drawing code from option 1 carries over
to option 2.
