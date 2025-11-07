#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../fitsdk/fit_convert.h"
#include <libpq-fe.h>
#include <raylib.h>

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

#define FIT_EPOCH_OFFSET 631065600u

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

slice_t sport_name(FIT_SPORT sport)
{
    switch (sport) {
#undef S
#define S(Sport, Name) \
    case Sport:        \
        return C(Name);
        SPORTS(S)
#undef S
    default:
        UNREACHABLE();
    }
}

#define get_entity(T, entities, ix)                                 \
    (                                                               \
        {                                                           \
            sweattrails_entities_t *__entities = (entities);        \
            size_t                  __ix = (ix).value;              \
            sweattrails_entity_t   *__e = __entities->items + __ix; \
            assert(__e->type == EntityType_##T);                    \
            ((T##_t *) &(__e->T));                                  \
        })

static float const semicircle = 180.0 / ((float) (1u << 31));

nodeptr read_fit_file(sweattrails_entities_t *entities, slice_t file_name)
{
    FIT_CONVERT_RETURN convert_return = FIT_CONVERT_CONTINUE;
    // FIT_UINT32         mesg_index = 0;

    FitConvert_Init(FIT_TRUE);
    path_t     path = path_parse(file_name);
    slice_t    contents = sb_as_slice(MUSTOPT(sb_t, slurp_file(file_name)));
    activity_t activity = { 0 };
    nodeptrs   sessions = { 0 };
    nodeptrs   laps = { 0 };
    nodeptrs   records = { 0 };
    nodeptr    ret = nullptr;

    trace("Reading fit file `" SL "`", SLARG(file_name));
    while (convert_return == FIT_CONVERT_CONTINUE) {
        uint16_t last_mesg_num = FIT_MESG_NUM_MFG_RANGE_MAX;
        size_t   mesg_num_count = 0;
        do {
            convert_return = FitConvert_Read(contents.items, contents.len);

            switch (convert_return) {
            case FIT_CONVERT_MESSAGE_AVAILABLE: {
                const FIT_UINT8 *mesg = FitConvert_GetMessageData();
                FIT_UINT16       mesg_num = FitConvert_GetMessageNumber();

                if (do_trace && mesg_num != last_mesg_num) {
                    if (last_mesg_num < FIT_MESG_NUM_MFG_RANGE_MAX) {
                        switch (mesg_num_count) {
                        case 1:
                            fprintf(stderr, "\n");
                            break;
                        default:
                            fprintf(stderr, " x%zu\n", mesg_num_count);
                            break;
                        }
                    }
                    fprintf(stderr, "Got message %s", FitConvert_mesg_name(mesg_num));
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
                case FIT_MESG_NUM_ACTIVITY: {
                    const FIT_ACTIVITY_MESG *a = (FIT_ACTIVITY_MESG *) mesg;
                    activity.start_time = a->timestamp;
                    break;
                }
                case FIT_MESG_NUM_SESSION: {
                    const FIT_SESSION_MESG *s = (FIT_SESSION_MESG *) mesg;
                    dynarr_append_s(sweattrails_entity_t, entities,
                        .type = EntityType_session,
                        .session = (session_t) {
                            .timestamp = s->timestamp,
                            .has_position_data = (s->start_position_lat != (1 << 31)) || (s->start_position_long != (1 << 31)),
                            .start_time = s->start_time,
                            .end_time = s->start_time + s->total_elapsed_time,
                            .sport = s->sport,
                            .sub_sport = s->sub_sport,
                            .description = path_basename(&path),
                            .elapsed_time = s->total_elapsed_time / 1000.0,
                            .moving_time = s->total_moving_time / 1000.0,
                            .distance = s->total_distance / 100.0,
                            .min_elevation = (s->enhanced_min_altitude != (FIT_UINT32) -1) ? (s->enhanced_min_altitude / 5.0) - 500.0 : 10000,
                            .max_elevation = (s->enhanced_max_altitude != (FIT_UINT32) -1) ? (s->enhanced_max_altitude / 5.0) - 500.0 : -1000,
                            .avg_elevation = (s->enhanced_avg_altitude != (FIT_UINT32) -1) ? (s->enhanced_avg_altitude / 5.0) - 500.0 : -1000,
                            .max_power = s->max_power,
                            .avg_power = s->avg_power,
                            .max_speed = s->enhanced_max_speed / 1000.0,
                            .avg_speed = s->enhanced_avg_speed / 1000.0,
                            .min_hr = s->min_heart_rate,
                            .max_hr = s->max_heart_rate,
                            .avg_hr = s->avg_heart_rate,
                            .time_range = (Vector2) {
                                .x = s->start_time,
                                .y = s->start_time + s->total_elapsed_time,
                            },
                            .route_area = (box_t) {
                                .ne = (coordinates_t) { .lat = -200, .lon = -200 },
                                .sw = (coordinates_t) { .lat = 200, .lon = 200 },
                            },
                        });
                    dynarr_append(&sessions, nodeptr_ptr(entities->len - 1));
                    if (s->start_time + s->total_elapsed_time > activity.end_time) {
                        activity.end_time = s->start_time + s->total_elapsed_time;
                    }
                    if (sessions.len > 2) {
                        session_t *s1 = get_entity(session, entities, sessions.items[sessions.len - 1]);
                        session_t *s2 = get_entity(session, entities, sessions.items[sessions.len - 2]);
                        assert(s2->start_time < s1->start_time);
                    }
                    break;
                }

                case FIT_MESG_NUM_LAP: {
                    const FIT_LAP_MESG *l = (FIT_LAP_MESG *) mesg;
                    dynarr_append_s(sweattrails_entity_t, entities,
                        .type = EntityType_lap,
                        .lap = (lap_t) {
                            .timestamp = l->timestamp,
                            .start_time = l->start_time,
                            .end_time = l->start_time + l->total_elapsed_time,
                            .description = path_basename(&path),
                            .elapsed_time = l->total_elapsed_time / 1000.0,
                            .moving_time = l->total_moving_time / 1000.0,
                            .distance = l->total_distance / 100.0,
                            .min_elevation = (l->enhanced_min_altitude != (FIT_UINT32) -1) ? (l->enhanced_min_altitude / 5.0) - 500.0 : 10000,
                            .max_elevation = (l->enhanced_max_altitude != (FIT_UINT32) -1) ? (l->enhanced_max_altitude / 5.0) - 500.0 : -1000,
                            .avg_elevation = (l->enhanced_avg_altitude != (FIT_UINT32) -1) ? (l->enhanced_avg_altitude / 5.0) - 500.0 : -1000,
                            .max_power = l->max_power,
                            .avg_power = l->avg_power,
                            .max_speed = l->enhanced_max_speed / 1000.0,
                            .avg_speed = l->enhanced_avg_speed / 1000.0,
                            .min_hr = l->min_heart_rate,
                            .max_hr = l->max_heart_rate,
                            .avg_hr = l->avg_heart_rate,
                            .time_range = (Vector2) {
                                .x = l->start_time,
                                .y = l->start_time + l->total_elapsed_time,
                            },
                            .route_area = (box_t) {
                                .ne = (coordinates_t) { .lat = -200, .lon = -200 },
                                .sw = (coordinates_t) { .lat = 200, .lon = 200 },
                            },
                        });

                    dynarr_append(&laps, nodeptr_ptr(entities->len - 1));
                    if (laps.len > 1) {
                        lap_t *l1 = get_entity(lap, entities, laps.items[laps.len - 1]);
                        lap_t *l2 = get_entity(lap, entities, laps.items[laps.len - 2]);
                        assert(l2->start_time < l1->start_time);
                    }
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
                        speed = record->enhanced_speed / 1000.0;
                        distance = record->distance / 100.0f;
                    }

                    dynarr_append_s(sweattrails_entity_t, entities,
                        .type = EntityType_record,
                        .record = (record_t) {
                            .timestamp = record->timestamp,
                            .position = (coordinates_t) {
                                .lat = ((float) record->position_lat) * semicircle,
                                .lon = ((float) record->position_long) * semicircle,
                            },
                            .elevation = (record->enhanced_altitude != (FIT_UINT32) -1) ? (record->enhanced_altitude / 5.0) - 500.0 : -1000,
                            .distance = distance,
                            .power = record->power,
                            .speed = speed,
                            .hr = record->heart_rate,
                        });
                    dynarr_append(&records, nodeptr_ptr(entities->len - 1));
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
        trace("Error decoding file");
        return nullptr;
    }

    if (convert_return == FIT_CONVERT_CONTINUE) {
        trace("Unexpected end of file");
        return nullptr;
    }

    if (convert_return == FIT_CONVERT_DATA_TYPE_NOT_SUPPORTED) {
        trace("File is not FIT");
        return nullptr;
    }

    if (convert_return == FIT_CONVERT_PROTOCOL_VERSION_NOT_SUPPORTED) {
        trace("Protocol version not supported");
        return nullptr;
    }

    assert(convert_return == FIT_CONVERT_END_OF_FILE);
    if (do_trace) {
        fprintf(stderr, "\n" SL ": File converted successfully.\n", SLARG(file_name));
    }

    activity.sessions = sessions;
    dynarr_append_s(sweattrails_entity_t, entities,
        .type = EntityType_activity,
        .activity = activity);
    ret = nodeptr_ptr(entities->len - 1);
    activity_t *a = &entities->items[ret.value].activity;
    dynarr_foreach(nodeptr, p, &a->sessions)
    {
        session_t *s = &entities->items[p->value].session;
        s->activity_id = ret;
    }
    dynarr_foreach(nodeptr, p, &laps)
    {
        lap_t *l = get_entity(lap, entities, *p);
        for (size_t ix = 0; ix < a->sessions.len; ++ix) {
            session_t *s = get_entity(session, entities, a->sessions.items[ix]);
            assert(l->start_time >= s->start_time);
            session_t *s_next = (ix < a->sessions.len - 1) ? get_entity(session, entities, a->sessions.items[ix + 1]) : NULL;
            if (s_next == NULL || (l->start_time < s_next->start_time)) {
                l->session_id = a->sessions.items[ix];
                dynarr_append(&s->laps, *p);
                break;
            }
        }
    }

    size_t lap_ix = 0;
    lap_t *l = get_entity(lap, entities, laps.items[lap_ix]);
    lap_t *l_next = (lap_ix < laps.len - 1) ? get_entity(lap, entities, laps.items[lap_ix + 1]) : NULL;
    dynarr_foreach(nodeptr, p, &records)
    {
        record_t *r = &entities->items[p->value].record;
        while ((l_next != NULL && l_next->start_time < r->timestamp)) {
            l = l_next;
            ++lap_ix;
            l_next = (lap_ix < laps.len - 1) ? get_entity(lap, entities, laps.items[lap_ix + 1]) : NULL;
        }
        r->lap_id = laps.items[lap_ix];
        dynarr_append(&l->records, *p);
        session_t *s = get_entity(session, entities, l->session_id);
        ++s->num_records;
        if (s->has_position_data) {
            if (fabs(r->position.lat) > 0.1 && fabs(r->position.lon) > 0.1) {
                l->route_area.sw.lat = MIN(l->route_area.sw.lat, r->position.lat);
                l->route_area.sw.lon = MIN(l->route_area.sw.lon, r->position.lon);
                l->route_area.ne.lat = MAX(l->route_area.ne.lat, r->position.lat);
                l->route_area.ne.lon = MAX(l->route_area.ne.lon, r->position.lon);
                l->max_elevation = MAX(l->max_elevation, r->elevation);
                l->min_elevation = MIN(l->min_elevation, r->elevation);
                s->route_area.sw.lat = MIN(s->route_area.sw.lat, r->position.lat);
                s->route_area.sw.lon = MIN(s->route_area.sw.lon, r->position.lon);
                s->route_area.ne.lat = MAX(s->route_area.ne.lat, r->position.lat);
                s->route_area.ne.lon = MAX(s->route_area.ne.lon, r->position.lon);
                s->max_elevation = MAX(s->max_elevation, r->elevation);
                s->min_elevation = MIN(s->min_elevation, r->elevation);
            }
        }
    }
    dynarr_foreach(nodeptr, s, &a->sessions)
    {
        session_t *session = get_entity(session, entities, *s);
        if (session->has_position_data) {
            session->atlas = atlas_for_box(session->route_area, 3, 3);
        }
    }
    return ret;
}

#define WINDOW_SIZE 64

record_t *session_get_record(ptr session, size_t ix, size_t *lap_ix, size_t *offset)
{
    session_t *s = get_p(session, session);
    assert(ix < s->num_records);

    lap_t *lap = get_entity(lap, session.entities, s->laps.items[*lap_ix]);
    while (*lap_ix < s->laps.len && (*offset + lap->records.len <= ix)) {
        *offset += lap->records.len;
        *lap_ix = *lap_ix + 1;
        lap = get_entity(lap, session.entities, s->laps.items[*lap_ix]);
    }
    assert(*lap_ix < s->laps.len);
    return get_entity(record, session.entities, lap->records.items[ix - *offset]);
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

    size_t    lap_ix = 0;
    size_t    offset = 0;
    record_t *record = session_get_record(this, 0, &lap_ix, &offset);
    float     prev_speed = height_f - record->speed * dspeed;
    float     prev_power = (dpower > 0) ? height_f - record->power * dpower : 0.0;
    size_t    last_ix = 0;
    size_t    d_ix = session->num_records / width;
    for (size_t x = 1; x < width; ++x) {
        size_t rec_ix = x * d_ix;
        float  alt_total = 0.0, speed_total = 0.0, power_total = 0.0;
        for (size_t ix = last_ix + 1; ix <= rec_ix && ix < session->num_records; ++ix) {
            record = session_get_record(this, rec_ix, &lap_ix, &offset);
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
        size_t y = (ix / session->atlas.columns) * 256;
        ImageDraw(
            &m,
            images[ix],
            (Rectangle) { .x = 0, .y = 0, .width = 256, .height = 256 },
            (Rectangle) { .x = (ix % session->atlas.columns) * 256, .y = y, .width = 256, .height = 256 },
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
    size_t    offset = 0;
    size_t    lap_ix = 0;
    for (size_t ix = 1; ix < session->num_records; ++ix) {
        record_t *record = session_get_record(this, ix, &lap_ix, &offset);
        if (fabs(record->position.lat) < 0.1 || fabs(record->position.lon) < 0.1) {
            continue;
        }
        float const dlat = 1.0 - (record->position.lat - box.sw.lat) / box_height(box);
        float const dlon = (record->position.lon - box.sw.lon) / box_width(box);
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

ptr activity_import(sweattrails_entities_t *entities, path_t inbox_path)
{
    nodeptr ix = read_fit_file(entities, sb_as_slice(inbox_path.path));
    if (!ix.ok) {
        fatal("Error importing file `" SL "`", SLARG(inbox_path.path));
    }
    return (ptr) { .entities = entities, .ptr = ix };
}

bool must_include(column_def_t *col)
{
    switch (col->kind) {
    case SQLTypeKind_Builtin:
    case SQLTypeKind_Composite:
        return true;
    case SQLTypeKind_Reference:
        return col->reference.cardinality == Card_ManyToOne;
    default:
        NYI("sql type kind " SL, SLARG(sql_type_kind_name(col->kind)));
    }
}

char *render_parameter(void *value_ptr, column_def_t *col)
{
    switch (col->kind) {
    case SQLTypeKind_Builtin:
        switch (col->type) {
        case SQLType_Bool:
            return temp_sprintf("%s", *((bool *) value_ptr) ? "TRUE" : "FALSE");
            break;
        case SQLType_Int32:
            return temp_sprintf("%d", *((int *) value_ptr));
            break;
        case SQLType_UInt32:
            return temp_sprintf("%u", *((uint32_t *) value_ptr));
            break;
        case SQLType_Float:
            return temp_sprintf("%f", *((float *) value_ptr));
            break;
        case SQLType_String:
            return temp_sprintf("'" SL "'", SLARG(*((slice_t *) value_ptr)));
            break;
        case SQLType_Point: {
            Vector2 v = *((Vector2 *) value_ptr);
            return temp_sprintf("(%f,%f)", v.x, v.y);
        } break;
        case SQLType_Box: {
            box_t box = *((box_t *) value_ptr);
            return temp_sprintf("((%f,%f),(%f,%f))", box.sw.lon, box.sw.lat, box.ne.lon, box.ne.lat);
        } break;
        default:
            NYI("builtin sql type " SL, SLARG(sql_type_name(col->type)));
        }
        break;
    case SQLTypeKind_Composite: {
        NYI("Nested composite types");
    } break;
    case SQLTypeKind_Reference:
        NYI("Nested reference types");
    default:
        NYI("sql type kind " SL, SLARG(sql_type_kind_name(col->kind)));
    }
}

void assign_parameter(ptr entity, schema_def_t *schema, column_def_t *col, cstrs *values)
{
    void *data = get_p(entity, entity);
    void *value_ptr = data + col->offset;
    switch (col->kind) {
    case SQLTypeKind_Builtin:
        dynarr_append(values, render_parameter(value_ptr, col));
        break;
    case SQLTypeKind_Composite: {
        type_def_t *type = schema->types.items + col->composite.value;
        sb_t        v = sb_make_cstr("(");
        bool        first = true;
        dynarr_foreach(column_def_t, type_col, &type->composite)
        {
            if (!first) {
                sb_append_char(&v, ',');
            }
            first = false;
            sb_append_cstr(&v, render_parameter(value_ptr + type_col->offset, type_col))
        }
        sb_append_char(&v, ')');
        sb_append_char(&v, 0);
        dynarr_append(values, temp_strdup(v.items));
    } break;
    case SQLTypeKind_Reference:
        if (col->reference.cardinality == Card_ManyToOne) {
            nodeptr               p = *((nodeptr *) value_ptr);
            sweattrails_entity_t *e = entity.entities->items + p.value;
            assert(e->entity.id.ok);
            dynarr_append(values, temp_sprintf("%d", e->entity.id.value));
        }
        break;
    default:
        NYI("sql type kind " SL, SLARG(sql_type_kind_name(col->kind)));
    }
}

char const *entity_store(ptr entity, db_t *db, int def_ix)
{
    void        *data = get_p(entity, entity);
    serial      *id = &((entity_t *) data)->id;
    size_t       cp = temp_save();
    PGresult    *res;
    table_def_t *def = db->schema.tables.items + def_ix;
    sb_t         sql = { 0 };
    cstrs        values = { 0 };

    // trace("Storing ID %zu type " SL, entity.ptr.value, SLARG(def->name));

    if (id->ok) {
        sb_printf(&sql, "UPDATE sweattrails." SL " SET ", SLARG(def->name));
        dynarr_foreach(column_def_t, col, &def->columns)
        {
            if (slice_eq(col->name, C("id"))) {
                continue;
            }
            if (!must_include(col)) {
                continue;
            }
            if (values.len > 0) {
                sb_append_cstr(&sql, ", ");
            }
            sb_printf(&sql, SL " = $%zu::" SL, SLARG(col->name), values.len + 1, SLARG(sql_type_sql(col->type)));
            assign_parameter(entity, &db->schema, col, &values);
        }
        sb_printf(&sql, " WHERE id = $%zu RETURNING id", values.len);
        dynarr_append(&values, temp_sprintf("%u", id->value));
    } else {
        sb_printf(&sql, "INSERT INTO sweattrails." SL " ( ", SLARG(def->name));
        dynarr_foreach(column_def_t, col, &def->columns)
        {
            if (slice_eq(col->name, C("id"))) {
                continue;
            }
            if (!must_include(col)) {
                continue;
            }
            if (values.len > 0) {
                sb_append_cstr(&sql, ", ");
            }
            sb_append(&sql, col->name);
            assign_parameter(entity, &db->schema, col, &values);
        }
        sb_append_cstr(&sql, " ) VALUES ( ");
        for (size_t ix = 0; ix < values.len; ++ix) {
            if (ix > 0) {
                sb_append_cstr(&sql, ", ");
            }
            sb_printf(&sql, "$%zu", ix + 1);
        }
        sb_append_cstr(&sql, " ) RETURNING id");
    }
    // trace("SQL: " SL, SLARG(sql));
    //    for (size_t ix = 0; ix < values.len; ++ix) {
    //        trace("value[%zu]: %s", ix + 1, values.items[ix]);
    //    }
    res = PQexecParams(db->conn, sql.items, values.len, NULL, (char const *const *) values.items, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        sb_t msg = sb_format(SL "_store() failed: %s", SLARG(def->name), PQerrorMessage(db->conn));
        trace("PQ error: " SL, SLARG(msg));
        PQclear(res);
        PQfinish(db->conn);
        return msg.items;
    }
    assert(atoi(PQcmdTuples(res)) == 1);
    int returned_id = atoi(PQgetvalue(res, 0, 0));
    if (id->ok) {
        assert(id->value == returned_id);
    } else {
        *(id) = OPTVAL(int, returned_id);
        // trace("ID: %zu %d %d", entity.ptr.value, get_p(entity, entity)->id.ok, get_p(entity, entity)->id.value);
    }
    PQclear(res);
    sb_free(&sql);
    dynarr_free(&values);
    temp_rewind(cp);
    return NULL;
}

char const *record_store(ptr record, db_t *db)
{
    return entity_store(record, db, RECORD_DEF);
}

char const *lap_store(ptr lap, db_t *db)
{
    char const *ret = entity_store(lap, db, LAP_DEF);
    if (ret == NULL) {
        lap_t *l = get_p(lap, lap);
        dynarr_foreach(nodeptr, r, &l->records)
        {
            ptr         record = make_ptr(lap, *r);
            char const *ret = record_store(record, db);
            if (ret != NULL) {
                break;
            }
        }
        trace("Stored lap nodeptr %zu with psql id %d and %zu records", lap.ptr.value, l->id.value, l->records.len);
    }
    return ret;
}

char const *session_store(ptr session, db_t *db)
{
    char const *ret = entity_store(session, db, SESSION_DEF);
    if (ret == NULL) {
        session_t *s = get_p(session, session);
        dynarr_foreach(nodeptr, l, &s->laps)
        {
            ptr         lap = make_ptr(session, *l);
            char const *ret = lap_store(lap, db);
            if (ret != NULL) {
                break;
            }
        }
        trace("Stored session nodeptr %zu with psql id %d and %zu laps", session.ptr.value, s->id.value, s->laps.len);
    }
    return ret;
}

char const *activity_store(ptr activity, db_t *db)
{
    size_t cp = temp_save();
    allocator_push(&temp_allocator);
    char const *ret = entity_store(activity, db, ACTIVITY_DEF);
    if (ret == NULL) {
        activity_t *a = get_p(activity, activity);
        dynarr_foreach(nodeptr, s, &a->sessions)
        {
            ptr         session = make_ptr(activity, *s);
            char const *ret = session_store(session, db);
            if (ret != NULL) {
                break;
            }
        }
        trace("Stored activity nodeptr %zu with psql id %d and %zu sessions", activity.ptr.value, a->id.value, a->sessions.len);
    }
    allocator_pop();
    temp_rewind(cp);
    return ret;
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
    nodeptrs               activities;
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
    ptr        a = { .entities = &gui->entities, .ptr = activity };
    ptr        s = make_ptr(a, gui->entities.items[activity.value].activity.sessions.items[0]);
    session_t *session = get_entity(session, &gui->entities, s.ptr);
    int        map_height = 0;
    if (session->has_position_data) {
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
    float    x = 20, y = 20;
    uint32_t row = 0;
    for (size_t ix = 0; ix < gui->entities.len; ++ix) {
        sweattrails_entity_t *e = gui->entities.items + ix;
        if (e->type != EntityType_activity) {
            continue;
        }

        sweattrails_entity_t *s = gui->entities.items + e->activity.sessions.items[0].value;
        time_t                st64 = (time_t) e->activity.start_time;
        struct tm            *st = localtime(&st64);
        size_t                cp = temp_save();
        char const           *txt = temp_sprintf(
            "%02d-%02d-%04d  %02d:%02d  " SL " " SL,
            st->tm_mday, st->tm_mon + 10, st->tm_year + 1900, st->tm_hour, st->tm_min, SLARG(s->session.description), SLARG(sport_name(s->session.sport)));
        DrawTextEx(
            gui->font,
            txt,
            (Vector2) { .x = x, .y = y },
            20, 1.0, RAYWHITE);
        temp_rewind(cp);
        if (row == gui->s.list_state.cur) {
            gui->s.list_state.current_activity = nodeptr_ptr(ix);
            Vector2 text_size = MeasureTextEx(gui->font, txt, 20, 1.0);
            DrawLineEx((Vector2) { .x = x, .y = y + text_size.y + 2 }, (Vector2) { .x = x + text_size.x, .y = y + text_size.y + 2 }, 3.0, RAYWHITE);
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
        C(""),
        C("localhost"),
        5432);
    sweattrails_init_schema(&db);
    // read_fit_file(C(argv[1]));

    gui_t gui = { 0 };
    gui.screen_width = 1550;
    gui.screen_height = 1024;
    gui.db = db;
    gui_run(&gui);

    return 0;
}
