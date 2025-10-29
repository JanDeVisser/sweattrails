#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../fitsdk/fit_convert.h"
#include "raylib.h"

#define CMDLINE_IMPLEMENTATION
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define FS_IMPLEMENTATION
#define IO_IMPLEMENTATION
#define ZORRO_IMPLEMENTATION
#include "cmdline.h"
#include "da.h"
#include "fs.h"
#include "io.h"
#include "zorro.h"

#define SCHEMA_IMPLEMENTATION
#include "schema.h"
#include "sweattrails.h"

#define RECORD_TABLE 0

uint32_t const FIT_EPOCH_OFFSET = 631065600;

typedef struct _time_of_day {
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint16_t millis;
} time_of_day_t;

time_of_day_t time_of_day_from_float(float t)
{
    uint32_t time_in_seconds = (uint32_t) trunc(t);
    uint8_t  hour = (uint8_t) trunc(((float) time_in_seconds / 3600.0));
    uint8_t  minute = (uint8_t) trunc(((float) (time_in_seconds % 3600) / 60.0));
    uint8_t  second = (uint8_t) time_in_seconds % 60;
    return (time_of_day_t) {
        .hour = hour % 24,
        .minute = minute,
        .second = second,
        .millis = (uint16_t) (t - trunc(t)) * 1000.0,
    };
}

nodeptr read_fit_file(sweattrails_entities_t *entities, slice_t file_name)
{
    FIT_CONVERT_RETURN convert_return = FIT_CONVERT_CONTINUE;
    // FIT_UINT32         mesg_index = 0;

    FitConvert_Init(FIT_TRUE);
    slice_t    contents = sb_as_slice(MUSTOPT(sb_t, slurp_file(file_name)));
    activity_t activity = { 0 };
    session_t  session = { 0 };
    nodeptr    session_id;
    nodeptr    ret = nullptr;

    printf("Reading fit file `" SL "`\n", SLARG(file_name));
    while (convert_return == FIT_CONVERT_CONTINUE) {
        uint16_t last_mesg_num = FIT_MESG_NUM_MFG_RANGE_MAX;
        size_t   mesg_num_count = 0;
        do {
            convert_return = FitConvert_Read(contents.items, contents.len);

            switch (convert_return) {
            case FIT_CONVERT_MESSAGE_AVAILABLE: {
                const FIT_UINT8 *mesg = FitConvert_GetMessageData();
                FIT_UINT16       mesg_num = FitConvert_GetMessageNumber();

                if (mesg_num != last_mesg_num) {
                    if (last_mesg_num < FIT_MESG_NUM_MFG_RANGE_MAX) {
                        switch (mesg_num_count) {
                        case 1:
                            printf("\n");
                            break;
                        default:
                            printf(" x%zu\n", mesg_num_count);
                            break;
                        }
                    }
                    printf("Got message %s", FitConvert_mesg_name(mesg_num));
                    last_mesg_num = mesg_num;
                    mesg_num_count = 0;
                }
                ++mesg_num_count;

                switch (mesg_num) {
                case FIT_MESG_NUM_FILE_ID: {
                    const FIT_FILE_ID_MESG *id = (FIT_FILE_ID_MESG *) mesg;
                    activity.serial_number = id->serial_number;
                    break;
                }
                case FIT_MESG_NUM_SESSION: {
                    const FIT_SESSION_MESG *s = (FIT_SESSION_MESG *) mesg;
                    activity.start_time = s->start_time - FIT_EPOCH_OFFSET;
                    activity.end_time = s->start_time + s->total_elapsed_time;
                    session = (session_t) {
                        .start_time = s->start_time,
                        .end_time = s->start_time + s->total_elapsed_time,
                        .description = file_name,
                        .elapsed_time = s->total_elapsed_time,
                        .moving_time = s->total_moving_time,
                        .distance = s->total_distance,
                        .min_elevation = s->enhanced_min_altitude,
                        .max_elevation = s->enhanced_max_altitude,
                        .max_power = s->max_power,
                        .max_speed = s->enhanced_max_speed,
                        .min_hr = s->min_heart_rate,
                        .max_hr = s->max_heart_rate,
                        .time_range = (Vector2) {
                            .x = s->start_time,
                            .y = s->start_time + s->total_elapsed_time,
                        },
                    };
                    break;
                }

                case FIT_MESG_NUM_LAP: {
                    // const FIT_LAP_MESG *lap = (FIT_LAP_MESG *) mesg;
                    // printf("Lap: timestamp=%u\n", lap->timestamp);
                    break;
                }

                case FIT_MESG_NUM_RECORD: {
                    const FIT_RECORD_MESG *record = (FIT_RECORD_MESG *) mesg;
                    float                  speed, distance;

                    if ((record->compressed_speed_distance[0] != FIT_BYTE_INVALID)
                        || (record->compressed_speed_distance[1] != FIT_BYTE_INVALID)
                        || (record->compressed_speed_distance[2] != FIT_BYTE_INVALID)) {
                        static FIT_UINT32 accumulated_distance16 = 0;
                        static FIT_UINT32 last_distance16 = 0;
                        FIT_UINT16        speed100;
                        FIT_UINT32        distance16;
                        speed100 = record->compressed_speed_distance[0] | ((record->compressed_speed_distance[1] & 0x0F) << 8);
                        distance16 = (record->compressed_speed_distance[1] >> 4) | (record->compressed_speed_distance[2] << 4);
                        accumulated_distance16 += (distance16 - last_distance16) & 0x0FFF;
                        last_distance16 = distance16;
                        distance = accumulated_distance16 / 16.0f,
                        speed = speed100 / 100.f;
                    } else {
                        speed = record->enhanced_speed;
                        distance = record->distance / 100.0f;
                    }

                    static float const semicircle = 180.0 / ((float) (1u << 31));

                    dynarr_append_s(sweattrails_entity_t, entities,
                        .type = EntityType_record,
                        .record = (record_t) {
                            .timestamp = record->timestamp,
                            .position = (coordinates_t) {
                                .lat = ((float) record->position_lat) * semicircle,
                                .lon = ((float) record->position_long) * semicircle,
                            },
                            .elevation = (record->enhanced_altitude / 5.0) - 500.0,
                            .distance = distance,
                            .power = record->power,
                            .speed = speed,
                            .hr = record->heart_rate,
                        });
                    dynarr_append(&session.records, nodeptr_ptr(entities->len - 1));
                    break;
                }

                case FIT_MESG_NUM_EVENT: {
                    //                    const FIT_EVENT_MESG *event = (FIT_EVENT_MESG *) mesg;
                    //                    printf("Event: timestamp=%u\n", event->timestamp);
                    break;
                }

                case FIT_MESG_NUM_DEVICE_INFO: {
                    //                    const FIT_DEVICE_INFO_MESG *device_info = (FIT_DEVICE_INFO_MESG *) mesg;
                    //                    printf("Device Info: timestamp=%u\n", device_info->timestamp);
                    break;
                }

                default:
                    // printf("Unknown\n");
                    break;
                }
                break;
            }

            default:
                break;
            }
        } while (convert_return == FIT_CONVERT_MESSAGE_AVAILABLE);
    }

    if (convert_return == FIT_CONVERT_ERROR) {
        printf("Error decoding file.\n");
        return nullptr;
    }

    if (convert_return == FIT_CONVERT_CONTINUE) {
        printf("Unexpected end of file.\n");
        return nullptr;
    }

    if (convert_return == FIT_CONVERT_DATA_TYPE_NOT_SUPPORTED) {
        printf("File is not FIT.\n");
        return nullptr;
    }

    if (convert_return == FIT_CONVERT_PROTOCOL_VERSION_NOT_SUPPORTED) {
        printf("Protocol version not supported.\n");
        return nullptr;
    }

    assert(convert_return == FIT_CONVERT_END_OF_FILE);
    printf("\n" SL ": File converted successfully.\n", SLARG(file_name));

    dynarr_append_s(sweattrails_entity_t, entities,
        .type = EntityType_activity,
        .activity = activity);
    ret = nodeptr_ptr(entities->len - 1);
    session.activity_id = ret;
    dynarr_append_s(sweattrails_entity_t, entities,
        .type = EntityType_session,
        .session = session);
    session_id = nodeptr_ptr(entities->len - 1);
    dynarr_append(&entities->items[ret.value].activity.sessions, session_id);

    box_t route_area = {
        .sw = (coordinates_t) { .lon = 200, .lat = 100 },
        .ne = (coordinates_t) { .lon = -200, .lat = -100 },
    };
    dynarr_foreach(nodeptr, p, &entities->items[session_id.value].session.records)
    {
        record_t *record = &entities->items[p->value].record;
        if (fabs(record->position.lat) > 0.1 && fabs(record->position.lon) > 0.1) {
            route_area.sw.lat = MIN(route_area.sw.lat, record->position.lat);
            route_area.sw.lon = MIN(route_area.sw.lon, record->position.lon);
            route_area.ne.lat = MAX(route_area.ne.lat, record->position.lat);
            route_area.ne.lon = MAX(route_area.ne.lon, record->position.lon);
        }
        record->session_id = session_id;
    }
    session_t *s = &entities->items[session_id.value].session;
    s->route_area = route_area;
    s->atlas = atlas_for_box(route_area, 2, 2);
    return ret;
}

Image session_graph_image(ptr this, uint32_t width, uint32_t height)
{
    session_t *session = get_p(session, this);
    Image      image = GenImageColor(width, height, BLANK);
    float      prev_x = 0;
    float      prev_speed = 0;
    float      prev_power = 0;
    float      width_f = width;
    float      height_f = height;
    float      dt = width_f / session->records.len;
    float      dalt = height_f / (session->max_elevation - session->min_elevation);
    float      dspeed = height_f / session->max_speed;
    float      dpower = (session->max_power > 0) ? height_f / session->max_power : 0.0;

    size_t window = 0;
    float  power_window[64];
    for (size_t ix = 0; ix < session->records.len; ++ix) {
        record_t *record = get_p(record, make_ptr(this, session->records.items[ix]));
        float     x = ix * dt;
        float     alt = record->elevation;
        float     alt_y = height_f - (alt - session->min_elevation) * dalt;
        float     speed_y = height_f - record->speed * dspeed;
        power_window[window] = record->power;
        window += 1;
        if (x - prev_x > 1.0) {
            ImageDrawRectangleRec(
                &image,
                (Rectangle) {
                    .x = prev_x,
                    .y = alt_y,
                    .width = ceil(x - prev_x),
                    .height = height_f - alt_y,
                },
                LIGHTGRAY);
            ImageDrawLineV(
                &image,
                (Vector2) {
                    .x = prev_x,
                    .y = ceil(prev_speed),
                },
                (Vector2) {
                    .x = x,
                    .y = ceil(speed_y),
                },
                DARKGREEN);
            prev_speed = speed_y;
            if (dpower > 0) {
                float sum = 0.0;
                for (size_t ix = 0; ix < window; ++ix) {
                    float p = power_window[ix];
                    sum += p;
                }
                float avg_power = sum / (float) window;
                float power_y = height_f - avg_power * dpower;
                ImageDrawLineV(
                    &image,
                    (Vector2) {
                        .x = prev_x,
                        .y = prev_power,
                    },
                    (Vector2) {
                        .x = x,
                        .y = power_y,
                    },
                    DARKBLUE);
                prev_power = avg_power;
            }
            prev_x = x;
            window = 0.0;
        }
    }
    return image;
}

Image session_map_image(ptr this)
{
    session_t    *session = get_p(session, this);
    coordinates_t mid = box_center(session->route_area);
    Image        *images = (Image *) calloc(session->atlas.num_tiles, sizeof(Image));
    assert(images != NULL);

    slices_t maps = atlas_get_maps(&session->atlas);
    dynarr_foreach(slice_t, map, &maps)
    {
        images[map - maps.items] = LoadImageFromMemory(".png", (unsigned char *) map->items, map->len);
    }

    Image m = GenImageColor(session->atlas.columns * 256, session->atlas.rows * 256, BLANK);
    for (size_t ix = 0; ix < session->atlas.num_tiles; ++ix) {
        ImageDraw(
            &m,
            images[ix],
            (Rectangle) {
                .x = 0,
                .y = 0,
                .width = 256,
                .height = 256,
            },
            (Rectangle) {
                .x = (ix % session->atlas.columns) * 256,
                .y = (ix / session->atlas.columns) * 256,
                .width = 256,
                .height = 256,
            },
            WHITE);
        UnloadImage(images[ix]);
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
    trace("atlas.route_area: sw: %f,%f ne: %f,%f dim %fx%f",
        session->route_area.sw.lon,
        session->route_area.sw.lat,
        session->route_area.ne.lon,
        session->route_area.ne.lat,
        box_width(session->route_area),
        box_width(session->route_area));
    trace("box: sw: %f,%f ne: %f,%f dim %fx%f",
        box.sw.lon,
        box.sw.lat,
        box.ne.lon,
        box.ne.lat,
        box_width(box),
        box_height(box));
    Rectangle const r = {
        .x = (session->route_area.sw.lon - box.sw.lon) / box_width(box) * session->atlas.columns * 256,
        .y = (1.0 - (session->route_area.ne.lat - box.sw.lat) / box_height(box)) * session->atlas.rows * 256,
        .width = box_width(session->route_area) / box_width(box) * session->atlas.columns * 256,
        .height = box_height(session->route_area) / box_height(box) * session->atlas.rows * 256,
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
    for (size_t ix = 0; ix < session->records.len; ++ix) {
        if (ix == 0) {
            continue;
        }
        record_t *record = get_p(record, make_ptr(this, session->records.items[ix]));
        if (fabs(record->position.lat) < 0.1 || fabs(record->position.lon) < 0.1) {
            continue;
        }
        float const dlat = 1.0 - (record->position.lat - box.sw.lat) / box_height(box);
        float const dlon = (record->position.lon - box.sw.lon) / box_width(box);
        float const x = session->atlas.columns * 256 * dlon;
        float const y = session->atlas.rows * 256 * dlat;
        ImageDrawCircleV(
            &m,
            (Vector2) {
                .x = x,
                .y = y,
            },
            2,
            RED);
        if (prev_x.ok && prev_y.ok && hypot(x - prev_x.value, y - prev_y.value) > 2) {
            ImageDrawLineV(
                &m,
                (Vector2) {
                    .x = prev_x.value,
                    .y = prev_y.value,
                },
                (Vector2) {
                    .x = x,
                    .y = y,
                },
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

ptr activity_import(sweattrails_entities_t *entities, path_t inbox_path)
{
    nodeptr ix = read_fit_file(entities, sb_as_slice(inbox_path.path));
    if (!ix.ok) {
        fatal("Error importing file `" SL "`", SLARG(inbox_path.path));
    }
    return (ptr) { .entities = entities, .ptr = ix };
}

char const *activity_store(ptr activity, db_t *db)
{
    (void) activity;
    (void) db;
    return NULL;
}

typedef enum _gui_state {
    GUIState_Importing,
    GUIState_List,
    GUIState_Display,
} gui_state_t;

typedef struct _gui {
    int                    screen_width;
    int                    screen_height;
    sweattrails_entities_t entities;
    Font                   font;
    gui_state_t            state;
    db_t                   db;
    import_t               import;

    union {
        struct {
            nodeptr   activity;
            Texture2D map_texture;
            Texture2D graph_texture;
        } display_state;
        struct {
            uint32_t cur_y;
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
    default:
        break;
    }
    memset(&gui->s, 0, sizeof(gui->s));
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
    sweattrails_entity_t *activity = gui->entities.items + gui->s.display_state.activity.value;
    sweattrails_entity_t *session = gui->entities.items + activity->activity.sessions.items[0].value;
    DrawTexture(gui->s.display_state.map_texture, 20, 20, WHITE);
    DrawTexture(gui->s.display_state.graph_texture, 20, gui->s.display_state.map_texture.height + 40, WHITE);

    struct tm st;
    time_t    st64 = (time_t) activity->activity.start_time;
    localtime_r(&st64, &st);
    time_of_day_t t = time_of_day_from_float(session->session.elapsed_time);
    size_t        cp = temp_save();
    DrawTextEx(
        gui->font,
        temp_sprintf("Start time    : %02d:%02d", st.tm_hour, st.tm_min),
        (Vector2) { .x = gui->s.display_state.map_texture.width + 40, .y = 40 },
        20, 1.0, RAYWHITE);
    DrawTextEx(
        gui->font,
        temp_sprintf("Total distance: %.3fkm", session->session.distance / 1000.0),
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
    if (gui->s.list_state.cur_y == 0) {
        gui->s.list_state.cur_y = 20;
    }
    float x = 20, y = 20;
    for (size_t ix = 0; ix < gui->entities.len; ++ix) {
        sweattrails_entity_t *e = gui->entities.items + ix;
        if (e->type != EntityType_activity) {
            continue;
        }
        struct tm             st;
        sweattrails_entity_t *s = gui->entities.items + e->activity.sessions.items[0].value;
        time_t                st64 = (time_t) e->activity.start_time;
        localtime_r(&st64, &st);
        size_t      cp = temp_save();
        char const *txt = temp_sprintf("%02d-%02d-%04d  %02d:%02d  " SL, st.tm_mday, st.tm_mon, st.tm_year, st.tm_hour, st.tm_min, SLARG(s->session.description));
        DrawTextEx(
            gui->font,
            txt,
            (Vector2) { .x = x, .y = y },
            20, 1.0, RAYWHITE);
        temp_rewind(cp);
        if (y == gui->s.list_state.cur_y) {
            gui->s.list_state.current_activity = nodeptr_ptr(ix);
            Vector2 text_size = MeasureTextEx(gui->font, txt, 20, 1.0);
            DrawLineEx((Vector2) { .x = x, .y = y + text_size.y + 2 }, (Vector2) { .x = x + text_size.x, .y = y + text_size.y + 2 }, 3.0, RAYWHITE);
        }
        y += 20;
        if (y > 800) {
            break;
        }
    }
}

void gui_run(gui_t *gui)
{
    InitWindow(gui->screen_width, gui->screen_height, "Sweattrails");
    gui->font = LoadFontEx("VictorMono-Medium.ttf", 20, NULL, 0);
    gui->import = import_init(&gui->db, &gui->entities, cmdline_is_set("reload-all"));
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
                    .longopt = "trace",
                    .option = 't',
                    .description = "Emit tracing/debug output",
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
    db_t db = db_make(
        C("sweattrails"),
        C("sweattrails"),
        C(""),
        C("localhost"),
        5432);
    table_defs_t schema = sweattrails_init_schema(&db);
    (void) schema;
    // read_fit_file(C(argv[1]));

    gui_t gui = { 0 };
    gui.screen_width = 1550;
    gui.screen_height = 1024;
    gui.db = db;
    gui_run(&gui);

    return 0;
}
