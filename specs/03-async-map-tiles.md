# Download Map Tiles in the Background

Status: Done

## Goal

Opening an activity whose map needs OpenStreetMap tiles that aren't cached
yet stalls the app: the tiles are downloaded on the GUI thread, one after
the other, before the activity is shown. The app should stay responsive
while tiles download, and show the activity right away.

## How it works now

`ActivityMap::set_activity()` calls `Map::init()`, which picks the zoom
level and the grid of tiles that covers the route, and then gets every tile
with `Tile::get_tile()`: from the cache in `Storage::tiles_dir`, or else
downloaded with curl and then cached. Only then are the tiles combined into
one texture, and the track is computed.

- Working out the zoom level, the grid and the track's pixel positions
  needs no tiles: only getting the tiles is slow.
- If any tile fails to download, `Map::init()` fails and the map isn't shown
  at all.

## Feature

- Opening an activity shows it right away: the data, the graph and the map
  with the track.
- Tiles that are cached are read from disk at once, on the UI thread, as
  now: a cached map appears whole, not tile by tile.
- Tiles that aren't cached are drawn as a plain, neutral grey square, with
  the track over it, and are downloaded in the background. Each tile
  appears as soon as it has downloaded. There's no "loading" text.
- Opening another activity while tiles are still downloading drops the old
  activity's tiles that are still waiting, so the new activity's tiles come
  first. A tile that is being downloaded at that moment finishes and is
  saved to the cache, but isn't drawn on the new map. Closing the activity
  does the same.
- A tile that fails to download stays grey, and the rest of the map is
  shown. It's tried again the next time an activity that needs it is
  opened.
- Once all of a map's downloads are done, if any failed, the status line
  says so once: "Couldn't download 3 map tiles". Tiles that download
  without problems aren't reported. Downloads dropped because another
  activity was opened aren't failures.
- Downloads follow the OpenStreetMap tile usage policy
  (https://operations.osmfoundation.org/policies/tiles/): at most two
  downloads at a time, and tiles are cached. One at a time is fine.

### Version 1 scope

In:

- Downloading uncached tiles in the background, and showing them as they
  arrive.
- Showing the map, with whatever tiles there are, when some tiles fail.

Deferred:

- Rebuilding the map when its viewport changes (the TODO at
  `ActivityMap::resize()`).
- Retrying failed downloads while the activity is shown.
- Showing a scaled-up cached tile from a lower zoom level until the real
  tile arrives.
- Showing download progress.
- Zooming and panning the map.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

Notes from the Analyst:

- Download on the existing AppBus job thread, with a new job type, and
  send the tiles back to the UI thread with a new notification. No second
  worker thread:
  - The only other jobs, `ProcessInbox` and `FindNewestActivity`, run once
    at startup. An inbox usually holds a handful of activities; an import of
    thousands is a one-time thing. Opening the activity list doesn't use a
    job.
  - At startup the first activity is only shown after the inbox has been
    processed anyway, so its tiles don't wait any longer than it does.
  - Jobs run one at a time, so there is one download at a time, as the
    OpenStreetMap policy asks, and the static curl handle in `download()`
    is only ever used from one thread. The UI thread must only read cached
    tiles, never download.
- Fetching tiles should go through an interface that a test can replace,
  so tests don't need the network.

### Decisions

**`Map::init()` only does geometry.** It loses its `Storage` parameter and
the `tiles` vector: it picks the zoom level and the grid, and can only fail
when no zoom level fits. Getting the tiles moves to `ActivityMap`.
`Tile::get_cached_tile()` becomes public, for the UI thread.
`Tile::get_tile()` (cache, else download) is for the job thread only.

**One job per map, not one per tile.** `ActivityMap::set_activity()` reads
the cached tiles, and collects the missing ones in a `TileDownload`, which
it schedules as one `DownloadTiles` job:

```cpp
// Map.h
struct MissingTile {
    size_t index;   // in the map's grid
    Tile   tile;
};

// The tiles of a map that weren't cached, for the job thread to download.
// The UI thread sets `cancelled` when it shows another map.
struct TileDownload {
    std::vector<MissingTile> tiles;
    std::atomic<bool>        cancelled { false };
};
using pTileDownload = std::shared_ptr<TileDownload>;
```

The job downloads the tiles one by one, and checks `cancelled` before and
after each. That gives what the spec asks: once cancelled, the tiles still
waiting are dropped, and the one being downloaded finishes and is cached,
but isn't sent. A single job also knows when the map's downloads are done,
and how many failed, which per-tile jobs would have to count in shared
state.

**The loop is a free function with the fetching passed in**, so a test can
run it without the network or the job thread:

```cpp
using TileFetcher = std::function<std::expected<std::string, LibCError>(Tile const &)>;
using TileReceiver = std::function<void(size_t index, std::string png)>;

// Returns the number of tiles that failed.
size_t fetch_tiles(TileDownload const &download, TileFetcher const &fetch, TileReceiver const &on_tile);
```

The job passes `Tile::get_tile()` as the fetcher, and a receiver that
sends a notification.

**Two new notifications**, with their payloads in `Map.h`:

- `MapTile { pTileDownload download; size_t index; std::string png; }` for
  each tile that downloaded.
- `MapTilesDone { pTileDownload download; size_t failed; }` when the job is
  done, unless it was cancelled.

`SweatTrails` keeps the `ActivityDisplay` it creates, and hands both to its
`ActivityMap`. The map ignores them unless `download` is its current
download: a tile could arrive just after another activity was opened.

**Drawing tiles as they arrive.** The map keeps its composed `Image` as it
does now, filled with a neutral grey instead of transparent. For each
`MapTile` it decodes the PNG, draws it into the image, and uploads the image
to the texture again with `UpdateTexture()`. That's a few MB per tile, for a
dozen tiles at most, which is fine. The PNG is decoded on the UI thread: one
256x256 PNG per frame is cheap, and it keeps raylib off the job thread.

**Failures.** On a `MapTilesDone` with failures, the map sets the status
line with `SweatTrails::set_status()`, "Couldn't download 3 map tiles" (or
"1 map tile"), and remembers that it did. When it shows another activity,
or none, it clears the status line again.

**Nothing for `src/Lib/Widget`** this time: the new code is specific to
maps.

## Test plan

A new `test/MapTest.cpp` (`map_test`), with `Map.cpp` and `Coordinates.cpp`:

- `fetch_tiles()`, with a fake fetcher that serves tiles by index:
  - all tiles arrive, in order, with their index, and none fail;
  - failed tiles are counted, and not sent;
  - nothing is fetched when the download is already cancelled;
  - cancelling while a tile is being fetched (the fetcher sets
    `cancelled`) doesn't send that tile, and doesn't fetch the rest.
- `Map::init()` without storage: for a route box, the grid covers the box,
  `num_tiles` is `columns * rows`, and `tile()` gives the grid's tiles.

Not tested automatically, because it needs a window and textures:
`ActivityMap` reading cached tiles, drawing arriving tiles, ignoring stale
ones, and the status line. Checked by hand: open an activity with an empty
tile cache (the app stays responsive and the tiles appear one by one),
switch activities while it downloads, and open one while offline (grey
tiles and the status line message).

## Review notes

1. **Bug: a tile that is still downloading counts as cached.** `download()`
   (`src/App/Map.cpp:124`) writes straight into the tile's cache file, and
   `get_cached_tile()` (`Map.cpp:86`) only checks that the file exists.
   Before, only the UI thread used the cache, and it blocked while a tile
   downloaded. Now the UI thread reads the cache while the job thread is
   writing to it. Two ways this goes wrong:
   - Open activity A with tile T not cached. While T downloads, open B whose
     map also has T: a route that overlaps A, which is common for a regular
     commute, or A again. `set_activity()` finds T's empty or half-written
     file, takes it as cached, and leaves it out of B's `TileDownload`. The
     PNG doesn't decode, so T stays grey on B's map. It isn't counted as a
     failure, so the status line doesn't mention it.
   - Quit while T downloads. This is now easy to do because the UI no
     longer blocks, and the job thread isn't stopped. The truncated file is
     left in the cache, and from then on every map that needs T shows it
     grey and never downloads it again. The spec says a failed tile "is
     tried again the next time".

   Fix: download to a temporary file in the same directory, and rename it to
   `<y>.png` only once the download has succeeded. I didn't add a test,
   because it needs `download()` itself, so either the network or a change
   to app code.
2. **Bug (from before this branch, but it now breaks the spec's failure
   handling): HTTP errors are saved as tiles.** `download()` doesn't set
   `CURLOPT_FAILONERROR` or check the response code. A 403, 429 or 5xx from
   tile.openstreetmap.org returns `CURLE_OK`, so its body is cached as the
   tile and `get_tile()` reports success. On the map, `draw_tile()`
   (`src/App/widget/Activity.cpp:515`) can't decode it and doesn't say so,
   so the tile stays grey, isn't counted in "Couldn't download N map
   tiles", and is never retried. OpenStreetMap answers 403 or 429 exactly
   when a client breaks the usage policy, so this is the failure the spec
   most needs to report. Fix: treat a response other than 200 as a failure,
   which also removes the file. A check in `fetch_tiles()` or the job that
   the bytes decode as a PNG would also catch tiles that were already
   cached badly.
3. **Bug (from before this branch, in `set_activity()`, which this branch
   changes): `image` and `texture` are freed twice.** Open activity A (with
   GPS), then B with no `segment.box` (an indoor workout), or one whose
   `Map::init()` fails, then C. Opening B frees A's image
   (`Activity.cpp:425`) and returns at line 432 or 442 without making a new
   one. Opening C then calls `UnloadImage(image)` again on A's freed
   buffer, which is a double free. Also, `Image image;` and
   `Texture2D texture;` (`Activity.h:52-53`) have no initializers, so if no
   activity was ever shown, `~ActivityMap()` frees an indeterminate pointer.
   Fix: give both `{ }` defaults, and unload only when `image.data` is set,
   or reset them after unloading. The new code doesn't make this worse,
   because `stop_download()` runs first, so no `MapTile` can draw into the
   freed image.
4. **Missing test, with a suggestion for making it testable: the
   `ActivityMap` logic.** Most of what the Test plan leaves to manual
   checking can be tested without a window if it's pulled out of the
   widget into `Map.h` or `Map.cpp`:
   - Splitting a map's tiles into cached and missing: a free function
     taking the `Map` and a cache reader (`Tile -> expected<string>`), which
     returns the cached PNGs by index and the `TileDownload` of the rest.
     `set_activity()` would then only decode and draw.
   - The status text: `"Couldn't download {} map {}"` with "tile" or
     "tiles", as a function of `failed`. 0, 1 and 3 should be tested.
   - Ignoring stale notifications: `add_tile()` and `tiles_done()` return
     before they touch raylib or `SweatTrails` when the download isn't
     current. As it stands, though, a test can't create an `ActivityMap`:
     `Activity.cpp` needs `SweatTrails::the()`, whose `.cpp` holds `main()`,
     and the destructor frees the uninitialized `image` (see 3).
     Either the helpers above, or making the "is this my download" check a
     small type of its own, would let a test cover it.
5. **Suggestion: the failure message shrinks a map that was laid out for
   the taller viewport.** When `tiles_done()` shows the status line,
   `set_status()` resizes the app, and the map's viewport gets shorter. But
   `ActivityMap::resize()` is empty, so `map.rectangle` and the track's
   screen positions keep the old size, and the bottom of the track can be
   drawn below the map's frame, over the graph. This is the TODO the spec
   defers, but the feature now triggers it on every map whose tiles fail,
   for example every activity opened offline. It might be worth checking
   during the manual test with the network off. Opening the next activity
   clears the status line before `Map::init()` runs, so that map is laid
   out correctly.

Checked and found correct: the `+ 1` in `make_job()`'s `in_place_index`
(the payload variant starts with a `std::monostate`, so `DownloadTiles`
gets `pTileDownload`), the job's receiver holding a reference to the
`pTileDownload` in the job's own payload copy, cancelling on another
activity, on closing the activity and in the destructor, `MapTilesDone` not
being sent when the download is cancelled, and maps with no missing tiles
scheduling no job.

Tests added to `test/MapTest.cpp` (all pass): an empty download, cancelling
while the last tile fails, `Map::tile(ix)` matching the image position
`draw_tile()` uses for several grid shapes, and the tile cache in a
temporary directory (`get_cached_tile()` hits and misses, and `get_tile()`
taking a cached tile without downloading it).

### Resolution

1. Fixed in 0c53a21: tiles download to `<y>.png.part`, renamed to
   `<y>.png` when complete. `download()` also closes the file and checks
   that writing it worked.
2. Fixed in add0074 with `CURLOPT_FAILONERROR`. Checked against
   tile.openstreetmap.org with a scratch program: the old code cached "You
   requested an invalid tile." for tile 1/9/9; now `get_tile()` fails and
   nothing is cached. Not done: checking that cached tiles decode as PNGs,
   for error pages cached before this fix.
3. Fixed in 82f595a: `image` and `texture` start empty, and
   `set_activity()` always unloads and resets them.
4. Not done, by Jan's choice.
5. The overflow is fixed in fb011a8: the map and track are drawn clipped to
   the viewport. Rebuilding the map for a new viewport stays deferred.
