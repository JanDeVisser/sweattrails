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

#include "../fitsdk/fit_convert.h"
#include "io.h"
#include "schema.h"
#include "sweattrails.h"

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

static float const semicircle = 180.0 / ((float) (1u << 31));

opt_coordinates_t coordinates_from_semicircles(int32_t lon, int32_t lat)
{
    if (lon == FIT_SINT32_INVALID || lat == FIT_SINT32_INVALID) {
        return OPTNULL(coordinates_t);
    }
    if (lon == 1 << 31 || lon == 1 << 31) {
        return OPTNULL(coordinates_t);
    }
    coordinates_t ret = (coordinates_t) {
        .lon = ((float) lon) * semicircle,
        .lat = ((float) lat) * semicircle,
    };
    return OPTVAL(coordinates_t, ret);
}

opt_box_t box_from_start_end_coordinates(int32_t start_lon, int32_t start_lat, int32_t end_lon, int32_t end_lat)
{
    opt_coordinates_t start = coordinates_from_semicircles(start_lon, start_lat);
    opt_coordinates_t end = coordinates_from_semicircles(end_lon, end_lat);
    if (!start.ok || !end.ok) {
        return OPTNULL(box_t);
    }
    box_t ret = box_from_coordinates(start.value, end.value);
    return OPTVAL(box_t, ret);
}

int sort_sessions(void *thunk, void const *p1, void const *p2)
{
    sweattrails_entities_t *repo = (sweattrails_entities_t *) thunk;
    session_t              *s1 = get_entity(session, repo, *((nodeptr *) p1));
    session_t              *s2 = get_entity(session, repo, *((nodeptr *) p2));
    if (s1->start_time < s2->start_time) {
        return -1;
    } else if (s1->start_time > s2->start_time) {
        return 1;
    } else {
        return (s1->end_time < s2->end_time) ? -1 : ((s1->end_time > s2->end_time) ? 1 : 0);
    }
}

int sort_laps(void *thunk, void const *p1, void const *p2)
{
    sweattrails_entities_t *repo = (sweattrails_entities_t *) thunk;
    lap_t                  *l1 = get_entity(lap, repo, *((nodeptr *) p1));
    lap_t                  *l2 = get_entity(lap, repo, *((nodeptr *) p2));
    if (l1->start_time < l2->start_time) {
        return -1;
    } else if (l1->start_time > l2->start_time) {
        return 1;
    } else {
        return (l1->end_time < l2->end_time) ? -1 : ((l1->end_time > l2->end_time) ? 1 : 0);
    }
}

int sort_records(void *thunk, void const *p1, void const *p2)
{
    sweattrails_entities_t *repo = (sweattrails_entities_t *) thunk;
    record_t               *r1 = get_entity(record, repo, *((nodeptr *) p1));
    record_t               *r2 = get_entity(record, repo, *((nodeptr *) p2));
    return (int) ((int64_t) r1->timestamp - (int64_t) r2->timestamp);
}

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
    activity.start_time = FIT_UINT32_INVALID;
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
                    //    const FIT_ACTIVITY_MESG *a = (FIT_ACTIVITY_MESG *) mesg;
                    break;
                }
                case FIT_MESG_NUM_SESSION: {
                    const FIT_SESSION_MESG *s = (FIT_SESSION_MESG *) mesg;
                    if (activity.start_time == FIT_UINT32_INVALID || activity.start_time > s->start_time) {
                        activity.start_time = s->start_time;
                    }
                    dynarr_append_s(sweattrails_entity_t, entities,
                        .type = EntityType_session,
                        .session = (session_t) {
                            .timestamp = s->timestamp,
                            .start_time = s->start_time,
                            .end_time = s->start_time + s->total_elapsed_time,
                            .sport = s->sport,
                            .sub_sport = s->sub_sport,
                            .description = path_basename(&path),
                            .elapsed_time = s->total_elapsed_time / 1000.0,
                            .moving_time = s->total_moving_time / 1000.0,
                            .distance = s->total_distance / 100.0,
                            .min_elevation = (s->enhanced_min_altitude != FIT_UINT32_INVALID) ? (s->enhanced_min_altitude / 5.0) - 500.0 : 10000,
                            .max_elevation = (s->enhanced_max_altitude != FIT_UINT32_INVALID) ? (s->enhanced_max_altitude / 5.0) - 500.0 : -1000,
                            .avg_elevation = (s->enhanced_avg_altitude != FIT_UINT32_INVALID) ? (s->enhanced_avg_altitude / 5.0) - 500.0 : -1000,
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
                            .route_area = box_from_start_end_coordinates(s->start_position_long, s->start_position_lat, s->end_position_long, s->end_position_lat),
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
                    trace("lap timestamp %d start_time %d elapsed %d end %d", l->timestamp, l->start_time, l->total_elapsed_time, l->start_time + l->total_elapsed_time);
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
                            .min_elevation = (l->enhanced_min_altitude != FIT_UINT32_INVALID) ? (l->enhanced_min_altitude / 5.0) - 500.0 : 10000,
                            .max_elevation = (l->enhanced_max_altitude != FIT_UINT32_INVALID) ? (l->enhanced_max_altitude / 5.0) - 500.0 : -1000,
                            .avg_elevation = (l->enhanced_avg_altitude != FIT_UINT32_INVALID) ? (l->enhanced_avg_altitude / 5.0) - 500.0 : -1000,
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
                            .route_area = box_from_start_end_coordinates(l->start_position_long, l->start_position_lat, l->end_position_long, l->end_position_lat),
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
                            .position = coordinates_from_semicircles(record->position_long, record->position_lat),
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

    dynarr_sort(&sessions, sort_sessions, entities);
    dynarr_sort(&laps, sort_laps, entities);
    dynarr_sort(&records, sort_records, entities);
    activity.sessions = sessions;
    dynarr_append_s(sweattrails_entity_t, entities,
        .type = EntityType_activity,
        .activity = activity);
    ret = nodeptr_ptr(entities->len - 1);
    activity_t *a = &entities->items[ret.value].activity;
    dynarr_foreach(nodeptr, p, &a->sessions)
    {
        session_t *s = &entities->items[p->value].session;
        s->activity_id = ref_entity(activity, ret.value);
    }
    dynarr_foreach(nodeptr, p, &laps)
    {
        lap_t *l = get_entity(lap, entities, *p);
        for (size_t ix = 0; ix < a->sessions.len; ++ix) {
            session_t *s = get_entity(session, entities, a->sessions.items[ix]);
            assert(l->start_time >= s->start_time);
            session_t *s_next = (ix < a->sessions.len - 1) ? get_entity(session, entities, a->sessions.items[ix + 1]) : NULL;
            if (s_next == NULL || (l->start_time < s_next->start_time)) {
                l->session_id = ref_entity(session, a->sessions.items[ix].value);
                dynarr_append(&s->laps, *p);
                break;
            }
        }
    }

    size_t     session_ix = 0;
    session_t *s = get_entity(session, entities, sessions.items[session_ix]);
    session_t *s_next = (session_ix < sessions.len - 1) ? get_entity(session, entities, sessions.items[session_ix + 1]) : NULL;
    size_t     lap_ix = 0;
    lap_t     *l = (lap_ix < laps.len) ? get_entity(lap, entities, laps.items[lap_ix]) : NULL;
    lap_t     *l_next = (lap_ix < laps.len - 1) ? get_entity(lap, entities, laps.items[lap_ix + 1]) : NULL;
    dynarr_foreach(nodeptr, p, &records)
    {
        record_t *r = get_entity(record, entities, *p);
        while ((l_next != NULL && l_next->start_time < r->timestamp)) {
            l = l_next;
            ++lap_ix;
            l_next = (lap_ix < laps.len - 1) ? get_entity(lap, entities, laps.items[lap_ix + 1]) : NULL;
        }
        if (l != NULL) {
            dynarr_append(&l->records, *p);
            if (r->position.ok && l->route_area.ok) {
                l->route_area = OPTVAL(box_t, box_extend(l->route_area.value, r->position.value));
                l->max_elevation = MAX(l->max_elevation, r->elevation);
                l->min_elevation = MIN(l->min_elevation, r->elevation);
            }
        }

        while ((s_next != NULL && s_next->start_time < r->timestamp)) {
            s = s_next;
            ++session_ix;
            s_next = (session_ix < sessions.len - 1) ? get_entity(session, entities, sessions.items[session_ix + 1]) : NULL;
        }
        assert(s != NULL);
        r->session_id = ref_entity(session, sessions.items[session_ix].value);
        dynarr_append(&s->records, *p);
        if (r->position.ok && s->route_area.ok) {
            s->route_area = OPTVAL(box_t, box_extend(s->route_area.value, r->position.value));
            s->max_elevation = MAX(s->max_elevation, r->elevation);
            s->min_elevation = MIN(s->min_elevation, r->elevation);
        }
    }
    dynarr_foreach(nodeptr, s, &a->sessions)
    {
        session_t *session = get_entity(session, entities, *s);
        if (session->route_area.ok) {
            session->atlas = atlas_for_box(session->route_area.value, 3, 3);
        }
    }
    return ret;
}
