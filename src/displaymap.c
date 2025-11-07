#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <raylib.h>

#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define FS_IMPLEMENTATION
#define IO_IMPLEMENTATION
#include "da.h"
#include "fs.h"
#include "io.h"

#include "map.h"

Texture2D make_texture(coordinates_t sw, coordinates_t ne)
{
    box_t         area = { .sw = sw, .ne = ne };
    atlas_t       atlas = atlas_for_box(area, 3, 3);
    coordinates_t mid = box_center(area);
    Image        *images = (Image *) calloc(atlas.num_tiles, sizeof(Image));
    assert(images != NULL);

    slices_t maps = atlas_get_maps(&atlas);
    dynarr_foreach(slice_t, map, &maps)
    {
        images[map - maps.items] = LoadImageFromMemory(".png", (unsigned char *) map->items, map->len);
    }

    Image m = GenImageColor(atlas.columns * 256, atlas.rows * 256, BLANK);
    for (size_t ix = 0; ix < atlas.num_tiles; ++ix) {
        size_t y = (ix / atlas.columns) * 256;
        ImageDraw(
            &m,
            images[ix],
            (Rectangle) { .x = 0, .y = 0, .width = 256, .height = 256 },
            (Rectangle) { .x = (ix % atlas.columns) * 256, .y = y, .width = 256, .height = 256 },
            WHITE);
        UnloadImage(images[ix]);
    }

    box_t const box = atlas_box(atlas);
    ImageDrawLine(&m, 0, atlas.height * 256, atlas.columns * 256, atlas.height * 256, GREEN);
    ImageDrawLine(&m, 0, (atlas.height + 1) * 256, atlas.columns * 256, (atlas.height + 1) * 256, GREEN);
    ImageDrawLine(&m, atlas.width * 256, 0, atlas.width * 256, atlas.rows * 256, GREEN);
    ImageDrawLine(&m, (atlas.width + 1) * 256, 0, (atlas.width + 1) * 256, atlas.rows * 256, GREEN);

    trace("atlas: zoom: %d x: %d y: %d  width: %d height: %d columns: %d rows: %d",
        atlas.zoom,
        atlas.x,
        atlas.y,
        atlas.width,
        atlas.height,
        atlas.columns,
        atlas.rows);
    trace("atlas.area: sw: %f,%f ne: %f,%f dim %fx%f",
        area.sw.lon,
        area.sw.lat,
        area.ne.lon,
        area.ne.lat,
        box_width(area),
        box_width(area));
    trace("box: sw: %f,%f ne: %f,%f dim %fx%f",
        box.sw.lon,
        box.sw.lat,
        box.ne.lon,
        box.ne.lat,
        box_width(box),
        box_height(box));
    Rectangle const r = {
        .x = (area.sw.lon - box.sw.lon) / box_width(box) * atlas.columns * 256,
        .y = (1.0 - (area.ne.lat - box.sw.lat) / box_height(box)) * atlas.rows * 256,
        .width = box_width(area) / box_width(box) * atlas.columns * 256,
        .height = box_height(area) / box_height(box) * atlas.rows * 256,
    };
    trace("r: %fx%f@(%f,%f)", r.width, r.height, r.x, r.y);
    Rectangle const fat = (Rectangle) {
        .x = r.x - (r.width * 0.05),
        .y = r.y - (r.height * 0.05),
        .width = r.width * 1.1,
        .height = r.height * 1.1,
    };
    float const mid_x = fat.x + (fat.width / 2);
    float const mid_y = fat.y + (fat.height / 2);
    float const w = atlas.width * 256;
    float const h = atlas.height * 256;
    float const img_w = m.width;
    float const img_h = m.height;

    Rectangle const square = {
        .x = (mid_x < w / 2) ? 0.0 : ((mid_x + w / 2 > img_w) ? img_w - w : mid_x - w / 2),
        .y = (mid_y < h / 2) ? 0.0 : ((mid_y + h / 2 > img_h) ? img_h - h : mid_y - h / 2),
        .width = w,
        .height = h,
    };
    trace("square: %fx%f@(%f,%f)", square.width, square.height, square.x, square.y);

    ImageDrawRectangleLines(&m, r, 2, SKYBLUE);
    ImageDrawRectangleLines(&m, fat, 2, PINK);
    ImageDrawRectangleLines(&m, square, 2, DARKBLUE);
    ImageDrawCircleV(
        &m,
        (Vector2) {
            .x = (mid.lon - box.sw.lon) / box_width(box) * img_w,
            .y = (1.0 - (mid.lat - box.sw.lat) / box_height(box)) * img_h,
        },
        3,
        BLACK);

    ImageCrop(&m, square);
    ImageResize(&m, atlas.width * 256, atlas.height * 256);
    // ImageResize(&m, 984, 984);

    free(images);
    Texture2D ret = LoadTextureFromImage(m);
    UnloadImage(m);
    return ret;
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    coordinates_t sw = { .lat = atof(argv[1]), .lon = atof(argv[2]) };
    coordinates_t ne = { .lat = atof(argv[3]), .lon = atof(argv[4]) };
    assert(sw.lat != 0.0 && sw.lon != 0.0 && ne.lat != 0.0 && ne.lon != 0);
    assert(sw.lat < ne.lat);
    assert(sw.lon < ne.lon);

    InitWindow(788, 788, "Sweattrails");
    SetTargetFPS(60);
    Texture2D map_texture = make_texture(sw, ne);
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(DARKGRAY);
        DrawTexture(map_texture, 10, 10, WHITE);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
