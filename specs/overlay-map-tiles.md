# Overlay map: the tiles

Status: In progress

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

The tiles are decoded in `overlay/Layout`, which tiles a map needs and
where the attribution goes are worked out in `overlay/TrackMap`, a new
`overlay/OverlayTiles` keeps a renderer's tile textures and downloads,
and `overlay/Renderer` draws them, through the clip shader with a color
filter. The downloads go through the job thread's `DownloadTiles`, as
the app's map's do.

### The layout

- **`MapTileLayer { float opacity { 1 }; TileFilter filter { }; }`**
  and **`TileFilter { TileFilterKind kind { None }; Color tint { }; }`**,
  with `TileFilterKind` `None`, `Grayscale`, `Dark`, `Tint`. There's no
  `source` member while `"osm"` is the only one.
- **`MapElement`** gets `std::optional<MapTileLayer> tiles` and
  `TextStyle attribution`. `tiles` and `attribution` stop being ignored.
- The attribution's style is decoded as a profile's labels are, with
  `label_style()`: its own properties, over its `style`, over a base of
  the map's own font and color at 14 design pixels with a shadow, so
  that a map without `attribution` gets a small credit that reads over
  footage, not one in the map's 48 px text size. The spec's "the
  `label` style's look by default" can't be had in a layout without a
  `label` style; the dashboard says `"style": "label"`.
- `layout_fonts()` and `layout_codepoints()` take in the attribution's
  font and text (the `©`) for a map with tiles.

### Which tiles, and where (`overlay/TrackMap`)

No drawing, so tests reach it:

- **`tile_level(MapTransform const &, float detail)`**: the OSM level
  nearest the scale, `round(log2(scale / detail))`, from 0 to 18.
  `detail` is how many drawing pixels a pixel of the result is: 2 in a
  render, which draws at twice the size, 1 in the preview. Without it a
  1080p render would fetch level 16 tiles, four times as many.
- **`map_tiles(MapTransform const &, Rectangle box, u8 level)`**: the
  `PlacedTile`s covering the box, each with its rectangle on the screen,
  its edges rounded out to whole pixels so that scaled tiles leave no
  seams. Tiles past the world's edges are left out, as in
  `MapView::tiles()`.
- **`attribution_box(Rectangle map, ClipKind, Vector2 text, float gap,
  Rectangle area)`**: below the map, right-aligned with it, or centred
  under a circle; above it when it would go past the area's bottom.
- **`color_filter(TileFilter)`**: the filter as a 4x4 color matrix and
  an offset, which the shader applies; `apply()` on a color, for tests.
  `Grayscale` is Rec. 601 luma (0.299, 0.587, 0.114) in each channel;
  `Dark` is one less that; `Tint` the luma times the tint's color.

### The tiles a renderer has (`overlay/OverlayTiles`)

```cpp
// The tiles of one renderer's maps: loaded from the cache at once, else
// downloaded on the job thread, one download at a time.
struct OverlayTiles {
    OverlayTiles(TileFetcher cached, std::function<void(pTileDownload)> schedule);
    ~OverlayTiles(); // Unloads the textures, cancels the download

    // Loads the cached ones of `tiles`, and asks for the others, unless
    // they failed before. True when all are loaded or failed.
    bool request(std::vector<Tile> const &tiles);
    Texture2D const *texture(Tile const &tile) const; // Null if not loaded

    void add_tile(MapTile const &tile);       // From the job thread's notifications;
    void tiles_done(MapTilesDone const &done); // others' downloads are ignored
    [[nodiscard]] size_t failed() const;
};
```

- A request replaces the download going on with one of the missing
  tiles unless it has them all already, as `next_download()` decides for
  the app's map: in the preview, the tiles of a mark the user scrubbed
  past aren't fetched. A render's frame waits for its download, so it's
  never replaced.
- Textures are unloaded past 64, least recently drawn first, with
  `tiles_to_unload()`; 64 is many times what a frame has in view.
- The logic is in pure functions where it can be, as for the app's map,
  so that tests don't need a GL context: the texture loading is behind
  the `cached` fetcher and a small loader. `OverlayTiles` is tested with
  a fake fetcher and scheduler, without textures, through a loader that
  a test replaces.
- `TileDownload::map` (an id) is how the notifications find their owner:
  `notif_map_tile()` and `notif_map_tiles_done()` hand each to the app's
  map, the preview's tiles and the render's tiles, and each ignores
  downloads that aren't its own. Its downloads get ids from the same
  counter as the app's map's.
- It's in the app, not the Widget library: it's about OSM tiles and the
  app's jobs. The app's map keeps its own tile code, which also draws
  placeholders from other levels while zooming; moving it onto
  `OverlayTiles` could come later.

### Rendering and the preview

- `OverlayRenderer` gets `set_tiles(OverlayTiles *, float detail)`; with
  none, as in tests, tiles aren't drawn. And `tiles_needed(values,
  area)`: the tiles of every map with tiles at that moment, worked out
  as `draw()` does.
- **`OverlayRender`** owns an `OverlayTiles`. `step()` asks for the next
  frame's tiles before drawing it, and if they're not all there, returns
  until a later step, while the notifications bring them. `waiting()`
  says so, and the status line says "Rendering x.mov: 42%, downloading
  map tiles". At the end, the failures are added: "Rendered the overlay
  to …. Couldn't download 3 map tiles".
- **`OverlayPreview`** owns an `OverlayTiles` too, and requests the
  tiles before each draw, without waiting: what's missing is left out.
  The preview fetches tiles at its own scale, which is smaller than the
  video's, so it shows a lower level: fewer downloads, and the video's
  level drawn at the preview's size couldn't be read anyway.
- Two `OverlayTiles`, the preview's and the render's, rather than one
  shared: their requests would replace each other's downloads, and a
  frame has only a few tiles to load twice.

### Drawing

- The tiles are drawn first inside the clip shader, before the track,
  over the panel's background, with `DrawTexturePro()` at their placed
  rectangles, bilinear, tinted white at the layer's `opacity`.
- **`ClipShader::begin(box, radius, ColorMatrix const & = identity)`**:
  the shader gets a color matrix and offset as uniforms, applied before
  the clip's fade; the identity for the track and the marker. In the
  Widget library, as a general color matrix, with nothing of OSM in it.
  The scissor fallback draws the tiles unfiltered.
- The attribution is drawn after the border, at `attribution_box()`,
  with the renderer's `draw_text()`, outside the clip.

### Tile code prerequisites

- **`UserAgent`** in `Http.h`: `SweatTrails/1.0
  (+https://github.com/JanDeVisser/sweattrails)`, used by `http_fetch()`
  and the tile `download()`.
- `download()` stops logging verbosely, and gets a timeout of 30
  seconds, as `http_fetch()` has one of 60: a connection that hangs
  would otherwise keep a render waiting forever.

### Built-in layout

The dashboard's map gets `"tiles": { "opacity": 0.6, "filter": "dark" }`
and `"attribution": { "style": "label", "font_size": 14 }`.

## Test plan

- `OverlayLayoutTest`: `tiles` with each filter and an opacity, `null`
  and left out; the errors (not an object, another source, an opacity
  out of range, an unknown filter); the attribution's style over its
  named style, and its default; the fonts and codepoints a map with
  tiles needs.
- `OverlayTrackMapTest`: `tile_level()` at 1080p, at 4K, at 1440p and
  for a render's `detail` of 2, and at the limits; `map_tiles()` for a
  box over four tiles, scaled tiles' whole-pixel edges, and tiles past
  the world's edge; `attribution_box()` below, centred under a circle,
  and above at the bottom of the area; `color_filter()` on a few colors.
- `OverlayTilesTest` (new): cached tiles loaded at once, missing ones
  scheduled as one download, a request covered by the download going on
  not scheduling another, failed ones not asked for again and counted,
  and another owner's tiles ignored. With a fake fetcher, scheduler and
  loader.
- `LayoutFileTest`: the dashboard's map with its tiles and attribution.
- By hand, with a throwaway frame dump: tiles under the track in a
  circle, each filter, no seams between tiles at 1440p, the attribution
  at the top and the bottom of the frame; a render waiting for tiles,
  and offline, finishing with the failures reported; the preview
  scrubbing; a render in Final Cut. Drawing can't be tested without a
  GL context.

## Review notes

*Filled in by the Reviewer.*
