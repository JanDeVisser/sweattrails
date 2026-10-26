# Map Pan and Zoom

Status: Ready

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

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
