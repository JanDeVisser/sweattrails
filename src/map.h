#include <stdbool.h>
#include <stdint.h>

#include "fs.h"
#include "slice.h"

#ifndef __MAP_H__
#define __MAP_H__

typedef struct _coordinates {
    float lat;
    float lon;
} coordinates_t;

typedef struct _box {
    coordinates_t sw;
    coordinates_t ne;
} box_t;

typedef struct _tile {
    uint32_t x;
    uint32_t y;
    uint8_t  zoom;
} tile_t;

typedef RES(slice_t, int) map_res;
typedef RES(opt_slice_t, int) opt_map_res;

typedef struct _atlas {
    uint8_t  zoom;
    uint32_t x;
    uint32_t y;
    uint16_t width;
    uint16_t height;
    uint16_t columns;
    uint16_t rows;
    uint16_t num_tiles;
    slices_t maps;
} atlas_t;

coordinates_t coordinates_for_tile(tile_t tile);
bool          coordinates_on_tile(coordinates_t this, tile_t tile);
bool          coordinates_in_box(coordinates_t this, box_t box);
box_t         box_for_tile(tile_t tile);
coordinates_t box_center(box_t this);
float         box_width(box_t this);
float         box_height(box_t this);
bool          box_contains(box_t this, box_t other);
bool          box_has(box_t this, coordinates_t point);
tile_t        tile_for_coordinates(coordinates_t pos, uint8_t zoom);
box_t         tile_box(tile_t this);
map_res       tile_get_map(tile_t this);
opt_map_res   tile_get_cached_map(tile_t this);
map_res       tile_cache_map(tile_t this, slice_t map);
path_t        tile_get_cache_dir(tile_t this);
atlas_t       atlas_for_box(box_t, uint8_t width, uint8_t height);
void          atlas_free(atlas_t *this);
tile_t        atlas_tile(atlas_t this, size_t ix);
tile_t        atlas_tile_xy(atlas_t this, uint32_t x, uint32_t y);
slices_t      atlas_get_maps(atlas_t *this);
box_t         atlas_box(atlas_t this);
box_t         atlas_sub_box(atlas_t this, size_t ix);

#endif /* __MAP_H__ */
