#include "tile_map.h"
#include "fit_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <curl/curl.h>

#define OSM_TILE_URL "https://tile.openstreetmap.org/%d/%d/%d.png"
#define USER_AGENT "fitpower/1.0 (https://github.com/fitpower)"

// Curl write callback for file download
static size_t write_file_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    FILE *file = (FILE *)userp;
    return fwrite(contents, size, nmemb, file);
}

// Create directories recursively
static bool create_directory_recursive(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

void tile_cache_init(TileCache *cache) {
    memset(cache, 0, sizeof(TileCache));

    const char *home = getenv("HOME");
    if (!home) home = ".";

#ifdef __APPLE__
    snprintf(cache->cache_dir, sizeof(cache->cache_dir),
             "%s/Library/Application Support/fitpower/tiles", home);
#else
    snprintf(cache->cache_dir, sizeof(cache->cache_dir),
             "%s/.local/share/fitpower/tiles", home);
#endif

    create_directory_recursive(cache->cache_dir);
    cache->initialized = true;
}

void tile_cache_free(TileCache *cache) {
    for (size_t i = 0; i < cache->tile_count; i++) {
        if (cache->tiles[i].loaded) {
            UnloadTexture(cache->tiles[i].texture);
        }
    }
    cache->tile_count = 0;
    cache->initialized = false;
}

// Find or evict a tile slot
static CachedTile *find_tile_slot(TileCache *cache, int x, int y, int z) {
    // First, check if tile already exists
    for (size_t i = 0; i < cache->tile_count; i++) {
        if (cache->tiles[i].x == x && cache->tiles[i].y == y && cache->tiles[i].z == z) {
            cache->tiles[i].last_used = time(NULL);
            return &cache->tiles[i];
        }
    }

    // If cache has space, use new slot
    if (cache->tile_count < MAX_CACHED_TILES) {
        CachedTile *tile = &cache->tiles[cache->tile_count++];
        memset(tile, 0, sizeof(CachedTile));
        tile->x = x;
        tile->y = y;
        tile->z = z;
        tile->last_used = time(NULL);
        return tile;
    }

    // Evict least recently used tile
    size_t lru_idx = 0;
    time_t lru_time = cache->tiles[0].last_used;
    for (size_t i = 1; i < cache->tile_count; i++) {
        if (cache->tiles[i].last_used < lru_time) {
            lru_time = cache->tiles[i].last_used;
            lru_idx = i;
        }
    }

    CachedTile *tile = &cache->tiles[lru_idx];
    if (tile->loaded) {
        UnloadTexture(tile->texture);
    }
    memset(tile, 0, sizeof(CachedTile));
    tile->x = x;
    tile->y = y;
    tile->z = z;
    tile->last_used = time(NULL);
    return tile;
}

// Download a tile from OSM
static bool download_tile(const char *cache_dir, int x, int y, int z, char *out_path, size_t path_size) {
    // Build cache path
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/%d/%d", cache_dir, z, x);
    create_directory_recursive(dir_path);

    snprintf(out_path, path_size, "%s/%d/%d/%d.png", cache_dir, z, x, y);

    // Check if file already exists
    struct stat st;
    if (stat(out_path, &st) == 0 && st.st_size > 0) {
        return true;  // Already cached
    }

    // Build URL
    char url[256];
    snprintf(url, sizeof(url), OSM_TILE_URL, z, x, y);

    // Download with curl
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    FILE *file = fopen(out_path, "wb");
    if (!file) {
        curl_easy_cleanup(curl);
        return false;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: " USER_AGENT);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(file);

    if (res != CURLE_OK) {
        remove(out_path);  // Remove incomplete file
        return false;
    }

    return true;
}

Texture2D *tile_cache_get(TileCache *cache, int x, int y, int z) {
    if (!cache->initialized) return NULL;

    // Clamp tile coordinates
    int max_tile = (1 << z) - 1;
    if (x < 0 || x > max_tile || y < 0 || y > max_tile) {
        return NULL;
    }

    CachedTile *tile = find_tile_slot(cache, x, y, z);

    if (tile->loaded) {
        return &tile->texture;
    }

    // Try to load from disk or download
    char path[512];
    if (download_tile(cache->cache_dir, x, y, z, path, sizeof(path))) {
        Image img = LoadImage(path);
        if (img.data) {
            tile->texture = LoadTextureFromImage(img);
            UnloadImage(img);
            tile->loaded = true;
            return &tile->texture;
        }
    }

    return NULL;
}

// Convert lat/lon to tile coordinates at given zoom
void lat_lon_to_tile(double lat, double lon, int zoom, int *tile_x, int *tile_y) {
    double n = pow(2.0, zoom);
    *tile_x = (int)((lon + 180.0) / 360.0 * n);

    double lat_rad = lat * M_PI / 180.0;
    *tile_y = (int)((1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * n);

    // Clamp to valid range
    int max_tile = (1 << zoom) - 1;
    if (*tile_x < 0) *tile_x = 0;
    if (*tile_x > max_tile) *tile_x = max_tile;
    if (*tile_y < 0) *tile_y = 0;
    if (*tile_y > max_tile) *tile_y = max_tile;
}

// Convert lat/lon to pixel coordinates at given zoom (absolute, not relative to view)
void lat_lon_to_pixel(double lat, double lon, int zoom, double *pixel_x, double *pixel_y) {
    double n = pow(2.0, zoom);
    *pixel_x = ((lon + 180.0) / 360.0 * n) * TILE_SIZE;

    double lat_rad = lat * M_PI / 180.0;
    *pixel_y = ((1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * n) * TILE_SIZE;
}

// Convert tile coordinates to lat/lon (top-left corner of tile)
void tile_to_lat_lon(int tile_x, int tile_y, int zoom, double *lat, double *lon) {
    double n = pow(2.0, zoom);
    *lon = tile_x / n * 360.0 - 180.0;
    double lat_rad = atan(sinh(M_PI * (1 - 2 * tile_y / n)));
    *lat = lat_rad * 180.0 / M_PI;
}

// Calculate zoom level and center to fit bounds
void map_view_fit_bounds(MapView *view, double min_lat, double max_lat,
                         double min_lon, double max_lon, int view_width, int view_height) {
    view->view_width = view_width;
    view->view_height = view_height;

    // Center point
    view->center_lat = (min_lat + max_lat) / 2.0;
    view->center_lon = (min_lon + max_lon) / 2.0;

    // Calculate zoom to fit bounds
    (void)max_lat;  // Used in loop below
    (void)max_lon;

    // Find zoom level that fits both dimensions
    int best_zoom = MAX_ZOOM;
    for (int z = MAX_ZOOM; z >= MIN_ZOOM; z--) {
        double px1, py1, px2, py2;
        lat_lon_to_pixel(min_lat, min_lon, z, &px1, &py1);
        lat_lon_to_pixel(max_lat, max_lon, z, &px2, &py2);

        double width_needed = fabs(px2 - px1);
        double height_needed = fabs(py2 - py1);

        if (width_needed <= view_width && height_needed <= view_height) {
            best_zoom = z;
            break;
        }
    }

    view->zoom = best_zoom;
}

void tile_map_draw(TileCache *cache, MapView *view, int screen_x, int screen_y) {
    // Calculate center pixel position
    double center_px, center_py;
    lat_lon_to_pixel(view->center_lat, view->center_lon, view->zoom, &center_px, &center_py);

    // Calculate which tiles are visible
    double left_px = center_px - view->view_width / 2.0;
    double top_py = center_py - view->view_height / 2.0;
    double right_px = center_px + view->view_width / 2.0;
    double bottom_py = center_py + view->view_height / 2.0;

    int tile_x_start = (int)(left_px / TILE_SIZE);
    int tile_y_start = (int)(top_py / TILE_SIZE);
    int tile_x_end = (int)(right_px / TILE_SIZE);
    int tile_y_end = (int)(bottom_py / TILE_SIZE);

    // Clip all drawing to view bounds
    BeginScissorMode(screen_x, screen_y, view->view_width, view->view_height);

    // Draw background
    DrawRectangle(screen_x, screen_y, view->view_width, view->view_height, (Color){200, 200, 200, 255});

    // Draw visible tiles
    for (int ty = tile_y_start; ty <= tile_y_end; ty++) {
        for (int tx = tile_x_start; tx <= tile_x_end; tx++) {
            Texture2D *tex = tile_cache_get(cache, tx, ty, view->zoom);

            // Calculate screen position of this tile
            double tile_px = tx * TILE_SIZE;
            double tile_py = ty * TILE_SIZE;
            int draw_x = screen_x + (int)(tile_px - left_px);
            int draw_y = screen_y + (int)(tile_py - top_py);

            if (tex) {
                DrawTexture(*tex, draw_x, draw_y, WHITE);
            } else {
                // Draw placeholder for missing tile
                DrawRectangle(draw_x, draw_y, TILE_SIZE, TILE_SIZE, (Color){180, 180, 180, 255});
                DrawRectangleLines(draw_x, draw_y, TILE_SIZE, TILE_SIZE, (Color){150, 150, 150, 255});
            }
        }
    }

    EndScissorMode();
}

void tile_map_draw_path(MapView *view, int screen_x, int screen_y,
                        const FitPowerSample *samples, size_t count) {
    if (count < 2) return;

    // Calculate view offset
    double center_px, center_py;
    lat_lon_to_pixel(view->center_lat, view->center_lon, view->zoom, &center_px, &center_py);
    double left_px = center_px - view->view_width / 2.0;
    double top_py = center_py - view->view_height / 2.0;

    // Enable scissor mode to clip path to view bounds
    BeginScissorMode(screen_x, screen_y, view->view_width, view->view_height);

    // Draw path
    Vector2 prev_point = {0};
    bool have_prev = false;

    for (size_t i = 0; i < count; i++) {
        if (!samples[i].has_gps) continue;

        double lat = samples[i].latitude * FIT_SEMICIRCLE_TO_DEGREES;
        double lon = samples[i].longitude * FIT_SEMICIRCLE_TO_DEGREES;

        double px, py;
        lat_lon_to_pixel(lat, lon, view->zoom, &px, &py);

        Vector2 point = {
            .x = screen_x + (float)(px - left_px),
            .y = screen_y + (float)(py - top_py)
        };

        if (have_prev) {
            // Color based on power if available
            Color line_color = (Color){255, 80, 80, 255};  // Default red
            if (samples[i].has_power) {
                uint16_t power = samples[i].power;
                if (power < 150) {
                    line_color = (Color){80, 180, 255, 255};  // Blue for easy
                } else if (power < 250) {
                    line_color = (Color){80, 255, 120, 255};  // Green for moderate
                } else {
                    line_color = (Color){255, 100, 80, 255};  // Red for hard
                }
            }
            DrawLineEx(prev_point, point, 3.0f, line_color);
        }

        prev_point = point;
        have_prev = true;
    }

    // Draw start and end markers
    if (count > 0) {
        // Find first GPS point
        for (size_t i = 0; i < count; i++) {
            if (samples[i].has_gps) {
                double lat = samples[i].latitude * FIT_SEMICIRCLE_TO_DEGREES;
                double lon = samples[i].longitude * FIT_SEMICIRCLE_TO_DEGREES;
                double px, py;
                lat_lon_to_pixel(lat, lon, view->zoom, &px, &py);
                int x = screen_x + (int)(px - left_px);
                int y = screen_y + (int)(py - top_py);
                DrawCircle(x, y, 6, GREEN);
                break;
            }
        }

        // Find last GPS point
        for (size_t i = count; i > 0; i--) {
            if (samples[i-1].has_gps) {
                double lat = samples[i-1].latitude * FIT_SEMICIRCLE_TO_DEGREES;
                double lon = samples[i-1].longitude * FIT_SEMICIRCLE_TO_DEGREES;
                double px, py;
                lat_lon_to_pixel(lat, lon, view->zoom, &px, &py);
                int x = screen_x + (int)(px - left_px);
                int y = screen_y + (int)(py - top_py);
                DrawCircle(x, y, 6, RED);
                break;
            }
        }
    }

    EndScissorMode();
}

void tile_map_draw_attribution(int x, int y, int font_size) {
    const char *attribution = "© OpenStreetMap contributors";
    DrawRectangle(x, y, MeasureText(attribution, font_size) + 10, font_size + 4, (Color){255, 255, 255, 200});
    DrawText(attribution, x + 5, y + 2, font_size, DARKGRAY);
}
