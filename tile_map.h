#ifndef TILE_MAP_H
#define TILE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "raylib.h"
#include "fit_parser.h"

#define TILE_SIZE 256
#define MAX_CACHED_TILES 64
#define MIN_ZOOM 1
#define MAX_ZOOM 18

typedef struct {
    int x, y, z;           // Tile coordinates (x, y) and zoom level (z)
    Texture2D texture;
    bool loaded;
    bool loading;          // Currently being downloaded
    time_t last_used;
} CachedTile;

typedef struct {
    CachedTile tiles[MAX_CACHED_TILES];
    size_t tile_count;
    char cache_dir[512];
    bool initialized;
} TileCache;

typedef struct {
    double center_lat, center_lon;
    int zoom;
    int view_width, view_height;
} MapView;

// Tile cache lifecycle
void tile_cache_init(TileCache *cache);
void tile_cache_free(TileCache *cache);

// Get a tile texture (downloads if needed, returns placeholder if loading)
Texture2D *tile_cache_get(TileCache *cache, int x, int y, int z);

// Coordinate conversion functions
void lat_lon_to_tile(double lat, double lon, int zoom, int *tile_x, int *tile_y);
void lat_lon_to_pixel(double lat, double lon, int zoom, double *pixel_x, double *pixel_y);
void tile_to_lat_lon(int tile_x, int tile_y, int zoom, double *lat, double *lon);

// Calculate view to fit GPS bounds
void map_view_fit_bounds(MapView *view, double min_lat, double max_lat,
                         double min_lon, double max_lon, int view_width, int view_height);

// Rendering functions
void tile_map_draw(TileCache *cache, MapView *view, int screen_x, int screen_y);
void tile_map_draw_path(MapView *view, int screen_x, int screen_y,
                        const FitPowerSample *samples, size_t count);

// Draw OSM attribution (required by OSM terms)
void tile_map_draw_attribution(int x, int y, int font_size);

#endif // TILE_MAP_H
