# Overlay map: the tiles

Status: Ready

## Goal

The overlay's map can show OpenStreetMap under the track, so that viewers
see the roads, the towns and the coast the ride went past, not only its
shape. On a ride video, a map with streets says where it is in a way a
line on a dark panel can't.

This is slice 6b of the [overlay roadmap](overlay-layout.md#roadmap),
after [overlay-map.md](overlay-map.md), which drew the track, `fit` and
`follow`, the clip and the marker, and took `tiles` and `attribution`
without drawing them. The format is in the layout reference, under
[`map`](overlay-layout.md#map-gps-track-position-optional-osm-underlay).
This spec says what of it is built now, and where it departs from the
reference.

## Feature

```json
{ "type": "map",
  "view": "follow",
  "zoom": 15,
  "clip": "circle",
  "tiles": { "source": "osm", "opacity": 0.8, "filter": "grayscale" },
  "attribution": { "style": "label", "font_size": 14 } }
```

- **`tiles`**: leave it out, or `null`, for the track on its panel, as in
  6a.
  - `source`: `"osm"`, OpenStreetMap's standard tiles, and the default.
    It's the only source: other sources and URL templates are deferred.
  - `opacity`: from 0 to 1, default 1. The panel's background shows
    through.
  - `filter`: `"none"` (the default), `"grayscale"`, `"dark"` (grayscale,
    inverted, so the map is dark with light roads), or a color, which
    tints the grayscale map. OSM's bright map competes with the footage;
    a filter keeps it in the background. The tiles are already drawn
    through the map's clip shader, so a filter is a few lines more in it.
- The track, the marker and the border are drawn over the tiles, as over
  the panel in 6a.
- **`attribution`**: "© OpenStreetMap contributors", which OSM's licence
  requires wherever its tiles are shown. It's drawn whenever the map has
  tiles, and can't be turned off: `attribution` is a text style (`style`,
  font, size, color, shadow, as for a `text` element), which only styles
  it, and is the `label` style's look by default.
  - It goes just below the map's box, right-aligned with it for `rect`
    and `rounded`, and centred under a `circle`. Inside the map it would
    be cut off by a circle, which is only about 150 px wide near the
    bottom of a 300 px map, while the text at 14 px is about 200 px. Next
    to the map it reads as the map's, and covers none of it.
  - Where there's no room below the map in the frame, such as for a map
    at the bottom, it goes just above it instead, as the policy says the
    attribution mustn't be off-screen.
- **The tiles' zoom**: the OSM level nearest the map's scale, in world
  pixels per screen pixel, `round(log2(scale))`, with the tiles drawn
  bigger or smaller to fit. One rule for both views:
  - `follow` at zoom 15 is level 15 at 1080p, 16 at 4K (the layout's
    scale is 2), at full sharpness, as the reference has it; and 15 at
    1440p (a scale of 1.33), drawn 1.33 times as big.
  - `fit`'s scale is anywhere between two levels, so its tiles are drawn
    from 0.7 to 1.4 times as big. 6a said the level just above, drawn
    smaller, which is sharp, but at down to half the size the tiles'
    street names can't be read, and it needs up to four times the tiles.
  - At most level 18, `fit`'s and `zoom`'s limit too.

### Errors

`tiles` is an object or `null`; `source` is `"osm"`; `opacity` a number
from 0 to 1; `filter` `"none"`, `"grayscale"`, `"dark"` or a color.
`attribution` is an object, decoded as a text style. Each with an error
saying which, as for other properties.

### OpenStreetMap's tile policy

OSM's [tile usage policy](https://operations.osmfoundation.org/policies/tiles/),
as read on 2026-10-28, shapes this slice more than the drawing does:

- "Bulk downloading is any pre-emptive fetching of tiles other than those
  a user is actively viewing", and it isn't allowed. So the reference's
  plan, fetching every tile along the track before a render starts, is
  out: a render fetches a tile when a frame needs it (see
  [Rendering](#rendering)). That's the way OSM is used as it's meant to
  be; a commercial tile source with an API key, which would allow more,
  is deferred with custom sources.
- Attribution is shown "clearly on the map (typically bottom-right)",
  and not hidden or off-screen. The policy doesn't mention videos or
  still images.
- A User-Agent that names the app, and may give a contact URL or email.
  Library defaults are blocked. The app's `sweattrails/1.0` names it;
  this slice makes it `SweatTrails/1.0
  (+https://github.com/JanDeVisser/sweattrails)`, for tiles and for the
  other web services in `Http.cpp` alike.
- Honour the server's caching headers, or else cache each tile for at
  least 7 days. The cache keeps tiles forever, which meets the minimum.
  Refreshing old tiles is deferred.

### Rendering

A render isn't real time: ffmpeg takes frames as fast as they're drawn.
So a frame can wait for its tiles:

- Before drawing a frame, the render works out the tiles in the map's
  view. Those that are cached are loaded. If any aren't, they're
  downloaded on the job thread, one at a time, as the app's map does,
  and the render waits, with the app responsive and the progress shown
  as "Downloading map tiles", until they're there or have failed.
- So the render only fetches what the video shows, when it shows it, as
  someone scrubbing through the ride in the preview would. A second
  render of the same ride reads them all from the cache.
- A tile that fails to download is left out: the panel shows through. The
  render goes on, and at its end the status line says "Couldn't download
  3 map tiles", as the app's map does.
- How many: in `follow` at zoom 15 in a 300 px box at 1080p, a new column
  of tiles is needed every minute and a half or so at 30 km/h. That's
  about 25 tiles for a 10-minute clip, and 300 to 400 for a whole 3-hour
  ride; about twice that at 4K. In `fit`, the few tiles of the whole
  ride.

### Built-in layout

The `cycling-dashboard`'s map gets tiles: `"filter": "dark"` at an
opacity of about 0.6, so that the map stays behind the footage and the
amber track stands out. It's what the slice is for. It does mean every
render of the dashboard uses OSM's servers, which is why the User-Agent
gets a contact.

### Preview

The preview draws the tiles as the app's map does: cached tiles at once,
the others downloaded in the background and drawn when they arrive.

### Version 1 scope

In:

- `tiles` with `source: "osm"`, `opacity` and `filter`, under the track,
  in the preview and in a render.
- The attribution, below or above the map.
- A render waiting for the tiles each frame needs, downloaded one at a
  time on the job thread, and reporting the tiles that failed.
- Tiles in the built-in `cycling-dashboard`.
- Of the reference's tile code prerequisites: the User-Agent with a
  contact, and curl's verbose logging off in `download()`, which logs
  every tile's request now.

*(Deferred)*:

- Other tile sources and URL templates, and with them a tile cache keyed
  by source: with one source they'd change nothing, and keying the cache
  by source means moving the tiles already cached.
- Refreshing cached tiles by the server's caching headers.
- Fetching tiles ahead of the render, which OSM doesn't allow; it could
  come with a source that does.
- `rotate: "heading"`, as in 6a.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
