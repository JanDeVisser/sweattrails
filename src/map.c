#include <curl/curl.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "da.h"
#include "fs.h"
#include "io.h"
#include "map.h"

int long2tilex(double lon, int z)
{
    return (int) (floor((lon + 180.0) / 360.0 * (1 << z)));
}

int lat2tiley(double lat, int z)
{
    double latrad = lat * M_PI / 180.0;
    return (int) (floor((1.0 - asinh(tan(latrad)) / M_PI) / 2.0 * (1 << z)));
}

double tilex2long(int x, int z)
{
    return x / (double) (1 << z) * 360.0 - 180;
}

double tiley2lat(int y, int z)
{
    double n = M_PI - 2.0 * M_PI * y / (double) (1 << z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

coordinates_t coordinates_for_tile(tile_t tile)
{
    return (coordinates_t) {
        .lat = tiley2lat(tile.y, tile.zoom),
        .lon = tilex2long(tile.x, tile.zoom),
    };
}

bool coordinates_on_tile(coordinates_t this, tile_t tile)
{
    coordinates_t sw = coordinates_for_tile(
        (tile_t) {
            .x = tile.x,
            .y = tile.y + 1,
            .zoom = tile.zoom });
    coordinates_t ne = coordinates_for_tile(
        (tile_t) {
            .x = tile.x + 1,
            .y = tile.y,
            .zoom = tile.zoom });
    return coordinates_in_box(this, (box_t) { .sw = sw, .ne = ne });
}

bool coordinates_in_box(coordinates_t this, box_t box)
{
    return box_has(box, this);
}

box_t box_for_tile(tile_t tile)
{
    return tile_box(tile);
}

box_t box_with_margins(box_t this, float margin)
{
    coordinates_t mid = box_center(this);
    float         f = 1.0 + margin;
    return (box_t) {
        .sw = (coordinates_t) {
            .lon = mid.lon - (box_width(this) * f) / 2.0,
            .lat = mid.lat - (box_height(this) * f) / 2.0,
        },
        .ne = (coordinates_t) {
            .lon = mid.lon + (box_width(this) * f) / 2.0,
            .lat = mid.lat + (box_height(this) * f) / 2.0,
        },
    };
}

coordinates_t box_center(box_t this)
{
    return (coordinates_t) {
        .lat = (this.sw.lat + this.ne.lat) / 2.0,
        .lon = (this.sw.lon + this.ne.lon) / 2.0
    };
}

float box_width(box_t this)
{
    return this.ne.lon - this.sw.lon;
}

float box_height(box_t this)
{
    return this.ne.lat - this.sw.lat;
}

bool box_contains(box_t this, box_t other)
{
    return box_has(this, other.sw) && box_has(this, other.ne);
}

bool box_has(box_t this, coordinates_t point)
{
    return point.lat >= this.sw.lat
        && point.lon >= this.sw.lon
        && point.lat <= this.ne.lat
        && point.lon <= this.ne.lon;
}

tile_t tile_for_coordinates(coordinates_t pos, uint8_t zoom)
{
    return (tile_t) {
        .zoom = zoom,
        .x = long2tilex(pos.lon, zoom),
        .y = lat2tiley(pos.lat, zoom),
    };
}

box_t tile_box(tile_t this)
{
    coordinates_t sw = coordinates_for_tile(
        (tile_t) {
            .x = this.x,
            .y = this.y + 1,
            .zoom = this.zoom });
    coordinates_t ne = coordinates_for_tile(
        (tile_t) {
            .x = this.x + 1,
            .y = this.y,
            .zoom = this.zoom });
    return (box_t) { .sw = sw, .ne = ne };
}

size_t write_data(char *ptr, size_t size, size_t nmemb, void *user_data)
{
    sb_t *sb = (sb_t *) user_data;
    for (size_t ix = 0; ix < size * nmemb; ++ix) {
        sb_append_char(sb, ptr[ix]);
    }
    return size * nmemb;
}

map_res tile_get_map(tile_t this)
{
    map_res     ret = { 0 };
    opt_map_res cached_map = tile_get_cached_map(this);
    if (cached_map.ok) {
        return cached_map.value;
    }
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        atexit(curl_global_cleanup);
        curl_initialized = true;
    }
    size_t      cp = temp_save();
    char const *url = temp_sprintf("https://tile.openstreetmap.org/%u/%u/%u.png", this.zoom, this.x, this.y);
    trace("Retrieving map %s", url);
    sb_t  map = { 0 };
    CURL *curl_handle = curl_easy_init();
    if (curl_handle == NULL) {
        trace("Error initializing cURL");
        ret = RESERR(map_res, -1);
        goto exit;
    }
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Sweattrails 0.1 (+https://www.finiandarcy.com; contact: jan@finiandarcy.com)");
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &map);
    CURLcode res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        trace("Error downloading map: CURLcode: %s", curl_easy_strerror(res));
        ret = RESERR(map_res, (int) res);
        goto exit;
    }
    ret = tile_cache_map(this, sb_as_slice(map));
exit:
    if (curl_handle != NULL) {
        curl_easy_cleanup(curl_handle);
    }
    temp_rewind(cp);
    return ret;
}

path_t tile_get_file_name(tile_t this)
{
    size_t cp = temp_save();
    char  *p = temp_sprintf(
        "%s/.sweattrails/tilecache/%u",
        getenv("HOME"), this.zoom);
    path_t ret = path_parse(C(p));
    path_mkdirs(ret);
    ret = path_extend(ret, C(temp_sprintf("%u-%u.png", this.x, this.y)));
    temp_rewind(cp);
    return ret;
}

opt_map_res tile_get_cached_map(tile_t this)
{
    opt_map_res ret = { 0 };
    path_t      fname = tile_get_file_name(this);
    if (!path_exists(fname)) {
        trace("Cache file " SL " does not exist", SLARG(fname.path));
        goto exit;
    }
    opt_sb_t map_maybe = slurp_file(sb_as_slice(fname.path));
    if (!map_maybe.ok) {
        trace("Error reading cache file " SL, SLARG(fname.path));
        ret = OPTVAL(map_res, RESERR(map_res, -1));
        goto exit;
    }
    trace("Loaded cached tile map " SL, SLARG(fname.path));
    ret = OPTVAL(map_res, RESVAL(map_res, sb_as_slice(map_maybe.value)));

exit:
    path_free(&fname);
    return ret;
}

map_res tile_cache_map(tile_t this, slice_t map)
{
    map_res ret = { 0 };
    path_t  fname = tile_get_file_name(this);
    if (!write_file(sb_as_slice(fname.path), map)) {
        ret = RESERR(map_res, errno);
        goto exit;
    }
    ret = RESVAL(map_res, map);
exit:
    path_free(&fname);
    return ret;
}

atlas_t atlas_for_box(box_t box, uint8_t width, uint8_t height)
{
    assert(width > 0 && width <= 8);
    assert(height > 0 && height <= 4);
    uint8_t       min_dim = MIN(width, height);
    uint8_t       zoom = 16 - min_dim - 1;
    coordinates_t mid = box_center(box);
    printf("init zoom: %d box: (%f,%f)x(%f,%f) mid: (%f,%f)\n", zoom, box.ne.lat, box.ne.lon, box.sw.lat, box.sw.lon, mid.lat, mid.lon);
    while (zoom > 0) {
        tile_t mid_tile = tile_for_coordinates(mid, zoom);
        printf("zoom: %d mid_tile: %d %d\n", zoom, mid_tile.x, mid_tile.y);
        box_t tbox = tile_box(mid_tile);
        printf("zoom: %d tbox: (%f,%f)x(%f,%f)\n", zoom, tbox.ne.lat, tbox.ne.lon, tbox.sw.lat, tbox.sw.lon);
        if (box_width(tbox) > box_width(box) * 1.1 && box_height(tbox) > box_height(box) * 1.1) {
            zoom += min_dim - 1;
            tile_t t = tile_for_coordinates(mid, zoom);
            return (atlas_t) {
                .zoom = zoom,
                .x = t.x - width,
                .y = t.y - height,
                .width = width,
                .height = height,
                .rows = 2 * height + 1,
                .columns = 2 * width + 1,
                .num_tiles = (2 * width + 1) * (2 * height + 1),
            };
        }
        zoom -= 1;
    }
    UNREACHABLE();
}

void atlas_free(atlas_t *this)
{
    dynarr_foreach(slice_t, map, &this->maps)
    {
        free(map->items);
    }
}

tile_t atlas_tile(atlas_t this, size_t ix)
{
    assert(ix < this.num_tiles);
    return (tile_t) {
        .zoom = this.zoom,
        .x = this.x + ix % this.columns,
        .y = this.y + ix / this.columns,
    };
}

tile_t atlas_tile_xy(atlas_t this, uint32_t x, uint32_t y)
{
    assert(x < this.columns && y < this.rows);
    return (tile_t) {
        .zoom = this.zoom,
        .x = this.x + x,
        .y = this.y + y,
    };
}

slices_t atlas_get_maps(atlas_t *this)
{
    if (this->maps.len) {
        return this->maps;
    }
    for (size_t ix = 0; ix < this->num_tiles; ++ix) {
        map_res res = tile_get_map(atlas_tile(*this, ix));
        if (!res.ok) {
            fatal("could not load map");
        }
        dynarr_append(&this->maps, res.success);
    }
    return this->maps;
}

box_t atlas_box(atlas_t this)
{
    tile_t t_sw = atlas_tile_xy(this, 0, this.rows - 1);
    tile_t t_ne = atlas_tile_xy(this, this.columns - 1, 0);
    return (box_t) { .sw = tile_box(t_sw).sw, .ne = tile_box(t_ne).ne };
}

box_t atlas_sub_box(atlas_t this, size_t ix)
{
    return tile_box(atlas_tile(this, ix));
}
