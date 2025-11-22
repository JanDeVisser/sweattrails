/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libpq-fe.h>
#include <raylib.h>

#define CMDLINE_IMPLEMENTATION
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define HASH_IMPLEMENTATION
#define FS_IMPLEMENTATION
#define IO_IMPLEMENTATION
#define ZORRO_IMPLEMENTATION
#include "cmdline.h"
#include "da.h"
#include "fs.h"
#include "hash.h"
#include "io.h"
#include "zorro.h"

#define SCHEMA_IMPLEMENTATION
#include "schema.h"
#include "sweattrails.h"

#define WINDOW_SIZE 64

record_t *session_get_record(ptr session, size_t ix)
{
    session_t *s = get_p(session, session);
    return get_entity(record, session.repo, s->records.items[ix]);
}

Image session_graph_image(ptr this, uint32_t width, uint32_t height)
{
    session_t *session = get_p(session, this);
    Image      image = GenImageColor(width, height, BLANK);
    float      height_f = height;
    float      dalt = height_f / (session->max_elevation - session->min_elevation);
    trace("max_elev %f min_elev %f dalt %f", session->max_elevation, session->min_elevation, dalt);
    float dspeed = height_f / session->max_speed;
    float dpower = (session->max_power > 0) ? height_f / session->max_power : 0.0;

    record_t *record = session_get_record(this, 0);
    float     prev_speed = height_f - record->speed * dspeed;
    float     prev_power = (dpower > 0) ? height_f - record->power * dpower : 0.0;
    size_t    last_ix = 0;
    size_t    d_ix = session->records.len / width;
    for (size_t x = 1; x < width; ++x) {
        size_t rec_ix = x * d_ix;
        float  alt_total = 0.0, speed_total = 0.0, power_total = 0.0;
        for (size_t ix = last_ix + 1; ix <= rec_ix && ix < session->records.len; ++ix) {
            record = session_get_record(this, rec_ix);
            alt_total += height_f - (record->elevation - session->min_elevation) * dalt;
            speed_total += height_f - record->speed * dspeed;
            power_total += height_f - record->power * dpower;
        }

        float d_ix = (float) (rec_ix - last_ix + 1);
        float alt_y = alt_total / d_ix;
        float speed_y = speed_total / d_ix;
        float power_y = power_total / d_ix;
        last_ix = rec_ix;

        ImageDrawRectangleRec(
            &image,
            (Rectangle) { .x = x - 1, .y = alt_y, .width = 1, .height = height_f - alt_y },
            RAYWHITE);
        ImageDrawLineV(
            &image,
            (Vector2) { .x = x - 1, .y = ceil(prev_speed) },
            (Vector2) { .x = x, .y = ceil(speed_y) },
            GREEN);
        if (dpower > 0) {
            ImageDrawLineV(
                &image,
                (Vector2) { .x = x - 1, .y = prev_power },
                (Vector2) { .x = x - 1, .y = power_y },
                ORANGE);
            prev_power = power_y;
        }
        prev_speed = speed_y;
    }
    return image;
}

Image session_map_image(ptr this)
{
    session_t *session = get_p(session, this);
    assert(session->route_area.ok);
    coordinates_t mid = box_center(session->route_area.value);
    Image        *images = (Image *) calloc(session->atlas.num_tiles, sizeof(Image));
    assert(images != NULL);

    slices_t maps = atlas_get_maps(&session->atlas);
    dynarr_foreach(slice_t, map, &maps)
    {
        if (map->len > 0) {
            images[map - maps.items] = LoadImageFromMemory(".png", (unsigned char *) map->items, map->len);
        }
    }

    Image m = GenImageColor(session->atlas.columns * 256, session->atlas.rows * 256, BLANK);
    for (size_t ix = 0; ix < session->atlas.num_tiles; ++ix) {
        size_t y = (ix / session->atlas.columns) * 256;
        if (images[ix].data != NULL) {
            ImageDraw(
                &m,
                images[ix],
                (Rectangle) { .x = 0, .y = 0, .width = 256, .height = 256 },
                (Rectangle) { .x = (ix % session->atlas.columns) * 256, .y = y, .width = 256, .height = 256 },
                WHITE);
            UnloadImage(images[ix]);
        }
    }

    box_t const box = atlas_box(session->atlas);
    ImageDrawLine(&m, 0, session->atlas.height * 256, session->atlas.columns * 256, session->atlas.height * 256, GREEN);
    ImageDrawLine(&m, 0, (session->atlas.height + 1) * 256, session->atlas.columns * 256, (session->atlas.height + 1) * 256, GREEN);
    ImageDrawLine(&m, session->atlas.width * 256, 0, session->atlas.width * 256, session->atlas.rows * 256, GREEN);
    ImageDrawLine(&m, (session->atlas.width + 1) * 256, 0, (session->atlas.width + 1) * 256, session->atlas.rows * 256, GREEN);

    trace("atlas: zoom: %d x: %d y: %d  width: %d height: %d columns: %d rows: %d",
        session->atlas.zoom,
        session->atlas.x,
        session->atlas.y,
        session->atlas.width,
        session->atlas.height,
        session->atlas.columns,
        session->atlas.rows);
    box_t route_area = session->route_area.value;
    trace("atlas.route_area: sw: %f,%f ne: %f,%f dim %fx%f",
        route_area.sw.lon,
        route_area.sw.lat,
        route_area.ne.lon,
        route_area.ne.lat,
        box_width(route_area),
        box_width(route_area));
    trace("box: sw: %f,%f ne: %f,%f dim %fx%f",
        box.sw.lon,
        box.sw.lat,
        box.ne.lon,
        box.ne.lat,
        box_width(box),
        box_height(box));
    Rectangle const r = {
        .x = (route_area.sw.lon - box.sw.lon) / box_width(box) * session->atlas.columns * 256,
        .y = (1.0 - (route_area.ne.lat - box.sw.lat) / box_height(box)) * session->atlas.rows * 256,
        .width = box_width(route_area) / box_width(box) * session->atlas.columns * 256,
        .height = box_height(route_area) / box_height(box) * session->atlas.rows * 256,
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
    float const w = session->atlas.width * 256;
    float const h = session->atlas.height * 256;
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

    opt_float prev_x = { 0 };
    opt_float prev_y = { 0 };
    for (size_t ix = 1; ix < session->records.len; ++ix) {
        record_t *record = session_get_record(this, ix);
        if (!record->position.ok) {
            continue;
        }
        float const dlat = 1.0 - (record->position.value.lat - box.sw.lat) / box_height(box);
        float const dlon = (record->position.value.lon - box.sw.lon) / box_width(box);
        float const x = session->atlas.columns * 256 * dlon;
        float const y = session->atlas.rows * 256 * dlat;
        ImageDrawCircleV(
            &m,
            (Vector2) { .x = x, .y = y },
            2,
            RED);
        if (prev_x.ok && prev_y.ok && hypot(x - prev_x.value, y - prev_y.value) > 2) {
            ImageDrawLineV(
                &m,
                (Vector2) { .x = prev_x.value, .y = prev_y.value },
                (Vector2) { .x = x, .y = y },
                RED);
        }
        prev_x = OPTVAL(float, x);
        prev_y = OPTVAL(float, y);
    }
    ImageCrop(&m, square);
    ImageResize(&m, session->atlas.width * 256, session->atlas.height * 256);
    // ImageResize(&m, 984, 984);

    free(images);
    return m;
}

ptr activity_import(repo_t *repo, path_t inbox_path)
{
    nodeptr ix = read_fit_file(repo, sb_as_slice(inbox_path.path));
    if (!ix.ok) {
        fatal("Error importing file `" SL "`", SLARG(inbox_path.path));
    }
    return (ptr) { .repo = repo, .ptr = ix };
}

typedef enum _gui_state {
    GUIState_Importing,
    GUIState_List,
    GUIState_Display,
} gui_state_t;

typedef struct _gui {
    int         screen_width;
    int         screen_height;
    repo_t      repo;
    nodeptrs    activities;
    Font        font;
    gui_state_t state;
    db_t        db;
    import_t    import;

    union {
        struct {
            nodeptr   activity;
            Texture2D map_texture;
            Texture2D graph_texture;
        } display_state;
        struct {
            uint32_t top;
            uint32_t cur;
            uint32_t bottom;
            nodeptr  current_activity;
        } list_state;
    } s;
} gui_t;

void gui_leave_state(gui_t *gui)
{
    switch (gui->state) {
    case GUIState_Display:
        UnloadTexture(gui->s.display_state.map_texture);
        UnloadTexture(gui->s.display_state.graph_texture);
        break;
    default:
        break;
    }
    memset(&gui->s, 0, sizeof(gui->s));
}

void gui_display_activity(gui_t *gui, nodeptr activity)
{
    gui->state = GUIState_Display;
    gui->s.display_state.activity = activity;
    ptr         a = { .repo = &gui->repo, .ptr = activity };
    activity_t *act = get_p(activity, a);
    ptr         f = make_ptr(a, act->files.items[0]);
    file_t     *file = get_p(file, f);
    ptr         s = make_ptr(a, file->sessions.items[0]);
    session_t  *session = get_p(session, s);
    int         map_height = 0;
    if (session->route_area.ok) {
        Image const map_img = session_map_image(s);
        gui->s.display_state.map_texture = LoadTextureFromImage(map_img);
        map_height = map_img.height;
        trace("map_img.height: %d", map_img.height);
        trace("screenHeight: %d", gui->screen_height);
        trace("screenHeight - map_img.height %d", gui->screen_height - map_img.height);
        UnloadImage(map_img);
    }
    Image const graph_img = session_graph_image(s, gui->screen_width - 40, gui->screen_height - map_height - 60);
    gui->s.display_state.graph_texture = LoadTextureFromImage(graph_img);
    UnloadImage(graph_img);
}

void gui_render_import_state(gui_t *gui)
{
    slice_t label = { 0 };
    switch (gui->import.import_status.status) {
    case ImportStatus_Start:
        label = C("start");
        break;
    case ImportStatus_Importing:
        label = gui->import.import_status.importing;
        break;
    case ImportStatus_Processing:
        label = C("Processing . . .");
        break;
    case ImportStatus_Idle:
        label = C("<Idle>");
        break;
    case ImportStatus_Crashed: {
        sb_t mesg = sb_format(
            "Crashed processing file `" SL "`: " SL,
            SLARG(gui->import.import_status.crashed.filename),
            SLARG(gui->import.import_status.crashed.message));
        label = sb_as_slice(mesg);
    } break;
    }
    size_t cp = temp_save();
    DrawTextEx(gui->font, temp_sprintf("Progress             : " SL, SLARG(label)), (Vector2) { .x = 40, .y = 40 }, 20, 1.0, RAYWHITE);
    DrawTextEx(gui->font, temp_sprintf("Successfully imported: %d", gui->import.total_imported), (Vector2) { .x = 40, .y = 40 + 20 * 1.2 }, 20, 1.0, RAYWHITE);
    DrawTextEx(gui->font, temp_sprintf("Errors               : %d", gui->import.total_errors), (Vector2) { .x = 40, .y = 40 + 2 * 20 * 1.2 }, 20, 1.0, RAYWHITE);
    temp_rewind(cp);

    if (gui->import.import_status.status == ImportStatus_Idle) {
        gui_leave_state(gui);
        gui->state = GUIState_List;
    }
}

void gui_render_display_state(gui_t *gui)
{
    assert(gui->state == GUIState_Display && gui->s.display_state.activity.ok);
    activity_t *activity = get_entity(activity, &gui->repo, gui->s.display_state.activity);
    file_t     *file = get_entity(file, &gui->repo, activity->files.items[0]);
    session_t  *session = get_entity(session, &gui->repo, file->sessions.items[0]);
    DrawTexture(gui->s.display_state.map_texture, 20, 20, WHITE);
    DrawTexture(gui->s.display_state.graph_texture, 20, gui->s.display_state.map_texture.height + 40, WHITE);

    struct tm st;
    time_t    start = (time_t) activity->start_time;
    localtime_r(&start, &st);
    time_of_day_t t = time_of_day_from_float(session->elapsed_time);
    size_t        cp = temp_save();
    DrawTextEx(
        gui->font,
        temp_sprintf("Start time    : %02d:%02d", st.tm_hour, st.tm_min),
        (Vector2) { .x = gui->s.display_state.map_texture.width + 40, .y = 40 },
        20, 1.0, RAYWHITE);
    DrawTextEx(
        gui->font,
        temp_sprintf("Total distance: %.3fkm", session->distance / 1000.0),
        (Vector2) { .x = gui->s.display_state.map_texture.width + 40, .y = 40 + 20 * 1.2 },
        20, 1.0, RAYWHITE);
    DrawTextEx(
        gui->font,
        temp_sprintf("Elapsed time  : %dh %02d' %02d\"", t.hour, t.minute, t.second),
        (Vector2) { .x = gui->s.display_state.map_texture.width + 40, .y = 40 + 2 * 20 * 1.2 },
        20, 1.0, RAYWHITE);
    temp_rewind(cp);
}

void gui_render_list_state(gui_t *gui)
{
    float    x = 20, y = 20;
    uint32_t row = 0;
    size_t   cp = temp_save();
    for (size_t ix = 0; ix < gui->repo.entities.len; ++ix) {
        temp_rewind(cp);
        entity_t *e = gui->repo.entities.items + ix;
        if (e->type != EntityType_activity) {
            continue;
        }
        activity_t *activity = get_entity(activity, &gui->repo, nodeptr_ptr(ix));
        time_t      start = (time_t) activity->start_time;
        time_t      end = (time_t) activity->end_time;
        struct tm   tm_start;
        localtime_r(&start, &tm_start);
        struct tm tm_end;
        localtime_r(&end, &tm_end);
        char const *txt = temp_sprintf(
            "%02d-%02d-%04d  %02d:%02d  %02d:%02d " SL,
            tm_start.tm_mday, tm_start.tm_mon + 10, tm_start.tm_year + 1900, tm_start.tm_hour, tm_start.tm_min,
            tm_end.tm_hour, tm_end.tm_min,
            SLARG(activity->description));
        DrawTextEx(
            gui->font,
            txt,
            (Vector2) { .x = x, .y = y },
            20, 1.0, RAYWHITE);
        if (row == gui->s.list_state.cur) {
            gui->s.list_state.current_activity = nodeptr_ptr(ix);
            Vector2 text_size = MeasureTextEx(gui->font, txt, 20, 1.0);
            DrawLineEx((Vector2) { .x = x, .y = y + text_size.y + 2 }, (Vector2) { .x = x + text_size.x, .y = y + text_size.y + 2 }, 3.0, RAYWHITE);

            float y_right = 20, x_right = 600;
            dynarr_foreach(nodeptr, f, &activity->files)
            {
                file_t *file = get_entity(file, &gui->repo, *f);
                start = (time_t) file->start_time;
                localtime_r(&start, &tm_start);
                end = (time_t) file->end_time;
                localtime_r(&end, &tm_end);
                char const *txt = temp_sprintf(
                    "%02d-%02d-%04d  %02d:%02d  %02d-%02d-%04d  %02d:%02d  " SL,
                    tm_start.tm_mday, tm_start.tm_mon + 10, tm_start.tm_year + 1900, tm_start.tm_hour, tm_start.tm_min,
                    tm_start.tm_mday, tm_start.tm_mon + 10, tm_start.tm_year + 1900, tm_end.tm_hour, tm_end.tm_min,
                    SLARG(file->file_name));
                DrawTextEx(
                    gui->font,
                    txt,
                    (Vector2) { .x = x_right, .y = y_right },
                    20, 1.0, RAYWHITE);
                y_right += 20;
                dynarr_foreach(nodeptr, s, &file->sessions)
                {
                    char const *txt = temp_sprintf("session#: %zu id: %zu type: %d", s - file->sessions.items, s->value, gui->repo.entities.items[s->value].type);
                    //                    session_t *session = get_entity(session, &gui->repo, *s);
                    //                    start = (time_t) session->start_time;
                    //                    st = localtime(&start);
                    //                    char const *txt = temp_sprintf(
                    //                        "%02d-%02d-%04d  %02d:%02d  " SL,
                    //                        tm_start.tm_mday, tm_start.tm_mon + 10, tm_start.tm_year + 1900, tm_start.tm_hour, tm_start.tm_min, SLARG(session->description));
                    DrawTextEx(
                        gui->font,
                        txt,
                        (Vector2) { .x = x_right + 40, .y = y_right },
                        20, 1.0, RAYWHITE);
                    y_right += 20;
                }
            }
        }
        y += 20;
        gui->s.list_state.bottom = row;
        if (++row > 30) {
            break;
        }
    }
}

void gui_list_state_input(gui_t *gui)
{
    if (IsKeyPressed(KEY_ENTER)) {
        nodeptr activity = gui->s.list_state.current_activity;
        gui_leave_state(gui);
        gui_display_activity(gui, activity);
    }
    if (IsKeyPressed(KEY_DOWN) && gui->s.list_state.cur < gui->s.list_state.bottom) {
        ++gui->s.list_state.cur;
    }
    if (IsKeyPressed(KEY_UP) && gui->s.list_state.cur > gui->s.list_state.top) {
        --gui->s.list_state.cur;
    }
}

void gui_run(gui_t *gui)
{
    InitWindow(gui->screen_width, gui->screen_height, "Sweattrails");
    gui->font = LoadFontEx("VictorMono-Medium.ttf", 20, NULL, 0);
    gui->import = import_init(&gui->db, &gui->repo, cmdline_is_set("reload-all"));
    import_start(&gui->import);

    SetTargetFPS(60);
    uint32_t frame = 0;
    while (!WindowShouldClose()) {
        BeginDrawing();

        ClearBackground(DARKGRAY);
        switch (gui->state) {
        case GUIState_Importing:
            gui_render_import_state(gui);
            break;
        case GUIState_List:
            gui_render_list_state(gui);
            break;
        case GUIState_Display:
            gui_render_display_state(gui);
            break;
        default:
            break;
        }
        EndDrawing();
        switch (gui->state) {
        case GUIState_List:
            gui_list_state_input(gui);
            break;
        default:
            break;
        }
        frame += 1;
    }
    (void) frame;
    UnloadFont(gui->font);
    CloseWindow();
}

static app_description_t app_descr = {
    .name = "sweattrails",
    .shortdescr = "Sweattrails performance analysis",
    .description = "Sweattrails performance analysis\n",
    .legal = "(c) finiandarcy.com",
    .options = {
        {
            .longopt = "reload-all",
            .description = "Reload all .fit files in the `done` directory",
            .value_required = false,
            .cardinality = COC_Set,
            .type = COT_Boolean,
        },
        {
            .longopt = "trace",
            .option = 't',
            .description = "Emit tracing/debug output",
            .value_required = false,
            .cardinality = COC_Set,
            .type = COT_Boolean,
        },
        /*
                {
                    .longopt = "list",
                    .option = 'l',
                    .description = "Display intermediate listings",
                    .value_required = false,
                    .cardinality = COC_Set,
                    .type = COT_Boolean,
                },
                {
                    .longopt = "verbose",
                    .option = 'v',
                    .description = "Verbose compiler outout",
                    .value_required = false,
                    .cardinality = COC_Set,
                    .type = COT_Boolean,
                },
        */
        { 0 } }
};

int main(int argc, char const **argv)
{
    parse_cmdline_args(&app_descr, argc, argv);
    do_trace = cmdline_is_set("trace");
    db_t db = db_make(
        C("sweattrails"),
        C("sweattrails"),
        C("sweattrails"),
        C("127.0.0.1"),
        5432);
    sweattrails_init_schema(&db);
    // read_fit_file(C(argv[1]));

    gui_t gui = { 0 };
    reload_everything(&gui.repo, &db);
    gui.screen_width = 1550;
    gui.screen_height = 1024;
    gui.db = db;
    gui_run(&gui);

    return 0;
}
