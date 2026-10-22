# Download Map Tiles in the Background

Status: In progress, on branch feature/async-map-tiles

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

*Filled in by the Reviewer.*
