# Map Pan and Zoom

Status: In review

## Goal

The map shows the whole route, fitted to the map's corner of the window,
and that's all it can show. On a long ride a lap, a climb or a turn in a
town is a few pixels. The user wants to zoom in on a part of the route,
move the map around, and go back to the whole route, as on any map.

## How it works now

- `ActivityMap::set_activity()` asks `Map::init()` for the zoom level
  and the grid of OpenStreetMap tiles that fit the route's box to the
  map's viewport, with 10% to spare. It draws the tiles into one image,
  the grid's size, and works out the track's pixel positions, in screen
  coordinates, once.
- Cached tiles are drawn right away; the others are grey until the job
  thread has downloaded them (`async-map-tiles.md`). Opening another
  activity cancels the downloads still waiting. Downloads follow the OSM
  tile usage policy: one at a time, and cached.
- The map doesn't take any input (`ActivityMap::process_input()` is
  empty).
- The map isn't redone when its viewport changes: the TODO at
  `ActivityMap::resize()`. Since the lap display, the lap table showing
  or hiding changes the map's height when an activity is opened, which
  `ActivityDisplay::set_activity()` works around by laying out first.
  Resizing the window still leaves the map as it was.
- The track is red, a selected range or lap is blue on top of it, and
  the marked record is a black dot.
- The README credits OpenStreetMap, but the map itself doesn't.

## Feature

### Zooming and panning

- **Zoom** with the mouse wheel, or two fingers on the trackpad, over
  the map. Each notch is one OSM zoom level, and the zoom is centered on
  the point under the mouse, which stays where it is. A trackpad reports
  fractions of a notch, which add up to whole ones, as in the lap table.
  Zoom steps are whole OSM levels so that the tiles stay sharp, as on
  openstreetmap.org.
- **Pan** by dragging the map. While it's being dragged, the cursor is a
  grabbing hand. A click on the map does nothing else.
- **Two-finger scrolling zooms rather than pans**, although a Mac
  trackpad often pans with it: raylib doesn't report pinching on the
  desktop, so with scrolling for panning there'd be no zooming with the
  trackpad at all.
- **Limits:**
  - In, to OSM zoom level 18, street level. GPS isn't more precise than
    that, and OSM's server stops at 19.
  - Out, two levels past the whole-route view, for some context, and not
    half a continent of tiles.
  - Panning keeps the map's center inside the route's bounding box: the
    user can look around the route, but not lose it.
- The track, the selected lap or range, and the marked record are drawn
  as now, where they are in the zoomed and panned map.

### Keys

Each is a task, for the command palette to come:

| Task                 | Key       | Does                                                  |
|----------------------|-----------|-------------------------------------------------------|
| `st-map-zoom-in`     | `=` / `+` | Zooms in one level, around the map's center           |
| `st-map-zoom-out`    | `-`       | Zooms out one level, around the map's center          |
| `st-map-whole-route` | `0`       | Back to the whole route, as when the activity opened  |

They do nothing under a modal, as the lap keys, and nothing for an
activity without a map. There are no keys for panning: Up and Down step
through the laps.

### Tiles

- Zooming and panning download the tiles the new view needs in the
  background, as opening an activity does now, following the OSM tile
  usage policy: one download at a time, and cached on disk.
- **While a tile downloads, the previous view shows in its place,
  scaled**: zoomed in, the old tiles enlarged and blurred, until each
  sharp tile arrives. A tile with nothing from the previous view to show
  is grey, as now. Without this the whole map would flash grey on every
  zoom. (`async-map-tiles.md` deferred showing scaled tiles; zooming
  makes it needed.)
- Only the current view's tiles are downloaded. Each change of the view
  drops the downloads still waiting for the one before, with the
  cancelling there is now, so zooming through a few levels doesn't queue
  every level's tiles. A tile being downloaded at that moment finishes
  and is cached.
- The status line reports failed downloads as now, once the current
  view's downloads are done.

### When the view changes by itself

- **Opening an activity** shows its whole route, as now. The zoom and
  pan aren't kept per activity.
- **Selecting a lap doesn't move the map**: moving it would be jarring,
  and holding Down to step through the laps would download tiles for
  every lap. The lap shows in blue, as now.
- **When the map's viewport changes**, such as when the window is
  resized or the status line appears: if the user hasn't zoomed or
  panned, the map is fitted to the whole route again, at the new size;
  if they have, it keeps its center and zoom level, and shows more or
  less around them. This fixes the TODO at `ActivityMap::resize()`, and
  makes the lap display's laying out before the map gets the activity
  unnecessary.

### Attribution

OSM's license requires "© OpenStreetMap contributors" on the map. A small
label in the map's bottom right corner, on a translucent background, says
so. It's needed with or without zooming, and is part of this spec because
it touches the same drawing.

### Version 1 scope

Everything above. Deferred:

- Smooth zooming, between OSM levels *(deferred)*.
- Pinching to zoom, which raylib doesn't report on the desktop
  *(deferred)*.
- A task that zooms the map to the selected lap or range *(deferred)*.
- Keeping the zoom and pan per activity *(deferred)*.
- A cache of tiles in memory, besides the one on disk *(deferred)*.

## Open questions

None.

## Architecture

### The view: `MapView` in `Map.h`, replacing `Map`

The map stops being one image of a grid of tiles, laid out once. It's a
view, and the tiles and the track are drawn where the view puts them,
every frame.

- **World pixels.** A point's place on the map at zoom level `z` is its
  Web Mercator position in pixels, 0 to `256 << z`. At zoom 18 that's up
  to 67 million: more than a `float` holds to the pixel, so world pixels
  are `double`s. `world_pixel(coordinates, zoom)` and its inverse are the
  projection that `Tile::for_coordinates()` and the track code now each
  do by hand.
- **`struct MapView { u8 zoom; double center_x, center_y; float width,
  height; }`**: the world pixel at the middle of the map's viewport, at a
  zoom level, for a viewport of a size. Plain arithmetic, no raylib
  drawing, so that it's all testable:
  - `fit(route, width, height)`: the whole route, as `Map::init()` picks
    it now: the highest zoom up to 16 where the route, with 10% to spare,
    fits, centered on the route.
  - `to_screen(world point)`, `to_world(screen point)`: relative to the
    viewport's top left corner.
  - `zoomed(steps, around)`: one level per step, keeping the world point
    under `around` (the mouse, or the middle for the keys) where it is.
  - `panned(dx, dy)`, `resized(width, height)`.
  - `clamped(route, min_zoom, max_zoom)`: the limits, the zoom to
    `[fit zoom - 2, 18]`, the center to the route's box.
  - `tiles()`: the tiles that cover the view, and where on screen each
    goes. Tiles outside the world, past the poles or the antimeridian,
    are left out (grey).
- `Map` and `Map::init()` go; their tests become `MapView` tests.

### Placeholders: `TileTextures`, and `cover()`

- **`ActivityMap` keeps the tiles it has loaded as textures**, one per
  tile, keyed by `Tile`, rather than one image: panning only moves them,
  and a zoom can draw the previous level's tiles, scaled, while the new
  ones download. They're dropped when another activity is opened, and
  the least recently drawn ones beyond about 150 (some 40 MB of video
  memory) are unloaded. This is how the spec's "previous view shows in
  its place" works: it isn't the in-memory tile cache that's deferred,
  which would keep tiles across activities.
- **`cover(tile, loaded)`** (in `Map.h`, pure) says what to draw for a
  tile that isn't loaded: the nearest loaded ancestor, up to three levels
  up, and the part of it that covers the tile, for zooming in; or else
  the tile's four children, if they're loaded, for zooming out; or
  nothing, which is grey.
- Tiles cached on disk are read when the view first needs them, on the
  UI thread, as now.

### Downloads

- Every change of the view cancels the waiting download, and schedules
  one for the visible tiles that are neither loaded nor cached, with the
  existing `TileDownload`, job and cancelling. A view change that needs
  the same tiles as the download that's going on, as when a drag moves
  less than a tile, leaves it be. `missing_tiles(view, have)` is the
  choice of tiles, testable.
- `MapTile` gets the `Tile` in place of its index in the grid, which no
  longer exists. `TileDownload` gets the `map` it's for, a number
  `ActivityMap` counts up for each activity: a tile that arrives from a
  cancelled download of the same activity is still loaded, since it may
  be in view and is a placeholder; one for another activity is dropped,
  as now.
- `fetch_tiles()` returns the tiles that failed, not their number, and
  `MapTilesDone` carries them: the map doesn't ask for them again while
  the activity is open, so that panning over a tile the server refuses
  doesn't ask for it on every frame. The status line reports failures
  for the current download, as now.

### Input, keys and resizing

- **`MapNavigator`** (in `Map.h`) holds the route, the `MapView`, whether
  the user has `moved` it, and the drag, and takes made-up input as
  `Scrollbar` does: `handle(input, viewport)` zooms around the mouse on
  the wheel over the map, and starts a drag on a press there, which pans
  by the mouse's movement until the button is let go, wherever the mouse
  is. It also has `zoom_by()`, `whole_route()` and `resize()`.
  `ActivityMap` owns one and draws what it says; its
  `process_input()` passes it raylib's input. This keeps the input
  testable without the app, which `ActivityMap` needs. `ActivityMap`'s
  code moves to `widget/ActivityMap.cpp`, as it grew.
- **Trackpad fractions**: `Scrollbar`'s `wheel_total` becomes a small
  `WheelNotches` in `Scroll.h`, which `Scrollbar` and the map share:
  `int add(float wheel)` returns the whole notches and keeps the rest.
- **The cursor while dragging is `MOUSE_CURSOR_RESIZE_ALL`**, the
  four-way arrow: raylib has no grabbing hand.
- The track is kept as world pixels at zoom 0 (`double`s), and scaled to
  the view when drawn: 10,000 records a frame is cheap. The selected
  range and the mark are drawn from it, as now.
- `ActivityMap::resize()` resizes the view: refitted to the whole route
  if the user hasn't zoomed or panned (`moved` is false), else
  `resized()`, keeping the center and zoom. The first layout in
  `ActivityDisplay::set_activity()`, for the lap table, isn't needed
  any more and goes.
- Tasks `st-map-zoom-in` (`=`, Shift `=` for `+`, keypad `+`),
  `st-map-zoom-out` (`-`, keypad `-`) and `st-map-whole-route` (`0`) in
  SweatTrails.cpp, calling `ActivityMap::zoom_by()` and
  `ActivityMap::show_whole_route()`, with the modal check of the lap
  keys.
- The attribution is drawn last, in the map's bottom right corner, in the
  small font on a translucent black.

## Test plan

- `MapTest` (`MapView`): `fit()` covers the route with the zoom it picked
  before, for a few viewport sizes; `to_screen()` and `to_world()` are
  each other's inverse, also at zoom 18 to the pixel; `zoomed()` keeps
  the point under the mouse in place, in and out; `clamped()` at both
  zoom limits and the center on each side of the route's box;
  `resized()` keeps the center; `tiles()` covers the view exactly, and
  leaves out tiles past the world's edges; `world_pixel()` against
  `Tile::for_coordinates()`.
- `MapTest` (`cover()`): a loaded parent, grandparent and great-grand-
  parent, and the part of each that's used; none past three levels;
  children when zooming out; nothing loaded.
- `ScrollTest`: `WheelNotches` adds up fractions, both ways; the
  trackpad test still passes.
- `MapTest` (`MapNavigator`), with made-up input: a wheel notch zooms
  around the mouse; trackpad fractions add up; a drag pans by its
  movement and goes on outside the map; a wheel or press elsewhere does
  nothing; the limits hold; `resize()` refits before the user moved the
  map and keeps the view after; `whole_route()` clears `moved`.
- The download scheduling: which tiles a view change asks for, given the
  loaded and cached ones, as a function of the view and two predicates.
- By hand: zooming with the wheel, the trackpad and the keys; dragging;
  the limits; a slow download while zooming (the placeholders); a
  resize, zoomed and not; another activity while tiles download; the
  attribution; an activity without a map.

## Review notes

1. **Bug: eviction unloads tiles that are in view.**
   `ActivityMap::unload_least_recently_drawn()` (ActivityMap.cpp, called
   from `view_changed()`) evicts by `drawn`, but a tile that was loaded
   earlier and is in the new view keeps the `drawn` of the last frame it
   was drawn in: `view_changed()`'s predicate only checks `loaded(tile)`.
   Scenario: open a long ride, zoom in a few levels and pan along it until
   more than 150 tiles are loaded, then press `0`. The whole-route tiles
   were loaded when the activity opened and are now the least recently
   drawn, so they're the first unloaded, and the whole route shows grey
   (or the zoomed-in children, scaled down) until the next view change;
   pressing `0` again doesn't change the view, so it stays that way. The
   comment "the tiles in view were drawn in this frame, or are about to
   be" doesn't hold. The same happens when more than 150 tiles share the
   latest `drawn`, on a very large map. Fix: in `view_changed()`, set
   `drawn = frame` on every loaded tile of the new view (and the cover
   pieces it will use) before evicting, or never evict a tile of
   `navigator.view.tiles()`. Not testable without a window as it stands;
   pulling the choice of tiles to evict into a pure function of `(tile ->
   drawn)` and the view's tiles would make it testable.

2. **Bug: failures in a cancelled download aren't recorded, so the tiles
   are asked for again.** `job_download_tiles()` only sends
   `MapTilesDone` when the download wasn't cancelled, and
   `ActivityMap::tiles_done()` only takes `failed` from the current
   download. The spec says a failed tile isn't asked for again while the
   activity is open, so that panning over a tile the server refuses
   doesn't ask for it over and over. Scenario: the server answers 403 or
   429 for tile T (the usage policy responses that `CURLOPT_FAILONERROR`
   is there for). A download `[a, T, b, c]` runs; the user drags, the
   view's tiles change (or see 3), the download is cancelled after T
   failed, and the next download asks for T again; every reschedule does.
   Being rate-limited and asking again is what the policy forbids. Fix:
   send the failed tiles for a cancelled download too (e.g. always send
   `MapTilesDone`, with a `cancelled` flag), and in `tiles_done()` add
   them to `failed` whenever `done.download->map == map_id`, keeping the
   status line report for the current download only.

3. **Bug: the download is replaced on every drag frame after a tile
   arrives.** `view_changed()` keeps the download only if
   `download->tiles == missing`. Once one of its tiles has arrived and is
   loaded, `missing` no longer contains it, so the next drag frame, even
   one moving a pixel, cancels the download and schedules a new one,
   although the view needs no tile it didn't have. The spec says such a
   view change "leaves it be". Effects: the tile being fetched is dropped
   by `fetch_tiles()` and read again from the cache by the next job; the
   new job queues behind any other job (a Garmin or Wahoo sync), so tiles
   stop arriving while one runs; and with 2, a failure in the cancelled
   download is lost and retried each time. Fix: keep the download when
   every missing tile is in `download->tiles` (a subset, not equality).

4. **Missing test: the download scheduling decision.** The test plan
   promises "which tiles a view change asks for, given the loaded and
   cached ones, as a function of the view and two predicates".
   `missing_tiles()` takes one predicate and is tested, but the decision
   that 3 is about (keep, cancel, or schedule) and the handling of
   `failed` are inline in `ActivityMap::view_changed()`, which needs
   `SweatTrails::the()` and textures, so they can't be tested. Suggest
   pulling it into a pure function in `Map.h`, e.g.
   `std::optional<std::vector<Tile>> next_download(view, have, current)`,
   which returns nothing when the current download should be left be,
   and testing it, including the subset case from 3.

5. **Bug (minor): a download is started for a view that's never shown.**
   `ActivityDisplay::set_activity()` no longer lays out first, so
   `ActivityMap::set_activity()` fits the route to the stale viewport and
   `view_changed()` loads that view's cached tiles and schedules a
   download for it; the `resize()` right after then refits and cancels
   it. When the lap table shows or hides, the height differs, so the
   stale view can be at another zoom. The job thread is idle and may have
   started fetching the first tile already: one request for a tile the
   user never sees, against "only the current view's tiles are
   downloaded". The final view is right: the map ends up fitted to its
   final viewport when the lap table shows or hides, and when it hides
   for an activity without positions (it isn't resized while hidden, and
   is refitted if its size changed when it shows again). Fix: in
   `set_activity()` start the navigator but leave `view_changed()` to the
   `resize()` that follows (e.g. a `view_pending` flag that `resize()`
   and `draw()` act on), or call `view_changed()` from `resize()` even
   when the size didn't change, after a `set_activity()`.

Checked and fine: `world_pixel()`/`world_coordinates()`; `fit()` picks
the same zoom as `Map::init()` for routes of many sizes and latitudes
(new test); `zoomed()` keeps the point under the mouse to the pixel at
zoom 18; `tiles()` at the world's edges and at zoom 0; `cover()` at
zoom 0, 2 and 18; the navigator's limits, the drag outside the map and
its end on release, `resize()` refitting only before a move; `tiles.at()`
for cover pieces (only loaded tiles are returned); tiles from cancelled
downloads of the same map are loaded and of another map dropped; the
track's NaN points, and the selected range now drawn to its last record
inclusive (the old code stopped one short); the map keys' modal check
and doing nothing without a map.

Tests added (commit e04f1eb, `test/MapTest.cpp`): `fit()` against the
old `Map::init()` zoom; `tiles()` covering the view exactly, on tile
boundaries, at the world's bottom right, at zoom 0 and 1 in a viewport
larger than the world, and at zoom 18; `zoomed()` at street level;
`cover()` three levels up, at zoom 0 and 2, at zoom 18, and with all four
children; `MapNavigator`: a click doesn't move the map, the keys zoom
around the middle, the pan limits north and east and after zooming out
around a corner, the lowest zoom for a route that fits only at zoom 1,
the whole route after a drag, and a resize the route doesn't fit. All
pass, as does the full run (`ctest -E oauth`).
