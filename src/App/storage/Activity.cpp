/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <system_error>

#include <IO.h>
#include <JSON.h>
#include <Logging.h>
#include <StringUtil.h>
#include <fit/FIT.h>
#include <storage/Activity.h>
#include <storage/Storage.h>

namespace ST {

using namespace ST::FIT;

namespace {

bool eql_ignore_case(std::string_view const &lhs, std::string_view const &rhs)
{
    return to_lower(lhs) == to_lower(rhs);
}

}

void Segment::analyze(std::span<record> records, bool smooth)
{
    for (auto &rec : records) {
        if (rec.speed) {
            auto const smoothed = computed_speed_range.extend(*rec.speed);
            if (smooth) {
                *rec.speed = smoothed;
            }
        }
        if (rec.power) {
            computed_normalized_range.extend(*rec.power);
            auto const smoothed = computed_power_range.extend(*rec.power);
            if (smooth) {
                *rec.power = smoothed;
            }
        }
        if (rec.cadence) {
            auto const smoothed = computed_cadence_range.extend(*rec.cadence);
            if (smooth) {
                *rec.cadence = smoothed;
            }
        }
        if (rec.heart_rate) {
            auto const smoothed = computed_hr_range.extend(*rec.heart_rate);
            if (smooth) {
                *rec.heart_rate = smoothed;
            }
        }
        if (rec.altitude) {
            auto const elev_int = static_cast<i16>(*rec.altitude);
            computed_elevation_range.extend(elev_int);
        }
        if (rec.position) {
            auto const &position = *rec.position;
            if (std::abs(position.lat) > 1 && std::abs(position.lon) > 1) {
                if (box) {
                    box->extend(position);
                } else {
                    box = Box { .sw = position, .ne = position };
                }
            }
        }
    }
    if (computed_normalized_range.range && computed_power_range.range) {
        auto const &np = *computed_normalized_range.range;
        auto const &avg = *computed_power_range.range;
        variability_index = static_cast<float32>(np.normalized) / static_cast<float32>(avg.average);
        intensity_factor = static_cast<float32>(np.normalized) / JANS_FTP;
        tss = std::trunc(
            (static_cast<float32>(elapsed.elapsed) * static_cast<float32>(np.normalized) * *intensity_factor)
            / (JANS_FTP * 36));
    }
}

StorageResult<bool> Activity::load(LoadingDepth the_depth)
{
    trace(Storage, "Loading activity {}", id.name);
    depth = the_depth;
    if (auto d = id.month.dir(*storage); d) {
        return load_from_dir(*d);
    }
    return false;
}

StorageResult<bool> Activity::load_from_dir(fs::path const &dir)
{
    if (id.files.contains(ActivityFile::Fit)) {
        auto const file_name = (dir / std::format("{}.fit", id.name)).string();
        auto       fit_file_maybe = FITFile::read(file_name);
        if (!fit_file_maybe.has_value()) {
            std::println("Error reading fit file `{}`", file_name);
            return std::unexpected(StorageError { fit_file_maybe.error() });
        }
        auto &fit_file = fit_file_maybe.value();
        auto  try_load = load_fit_file(fit_file);
        if (!try_load.has_value()) {
            std::println("load_fit_file(`{}`) failed", file_name);
            return std::unexpected(try_load.error());
        }
        if (!try_load.value()) {
            std::println("load_fit_file(`{}`) returned false", file_name);
            return false;
        }
        // if (id.files.contains(ActivityFile::Strava)) {
        //     if (!TRY_EVAL(load_strava_file(dir))) {
        //         return false;
        //     }
        // }
    }
    segment.analyze(records, true);
    return true;
}

StorageResult<bool> Activity::load_from_slice(std::string_view const &buffer)
{
    assert(id.files.contains(ActivityFile::Fit));
    FITFile fit_file { buffer };
    if (!TRY_EVAL(load_fit_file(fit_file))) {
        return false;
    }
    segment.analyze(records, true);
    return true;
}

Error<StorageError> Activity::analyze()
{
    segment.analyze(records, true);

    struct RecordRange {
        std::optional<size_t> min { };
        std::optional<size_t> max { };
    };

    // The zig version builds an empty list and then iterates over it, so the
    // loops below never ran. Sizing the list to the number of laps is what was
    // intended; note that the ranges are assigned in lap order.
    std::vector<RecordRange> ranges { };
    ranges.resize(laps.size());
    for (size_t recnum = 0; recnum < records.size(); ++recnum) {
        auto const &rec = records[recnum];
        for (size_t lap = 0; lap < ranges.size(); ++lap) {
            auto &r = ranges[lap];
            if (r.max) {
                goto next_record;
            }
            if (!r.min && laps[lap].segment.start_time.less_than(rec.timestamp)) {
                r.min = recnum;
            } else if (r.min && laps[lap].segment.end_time.less_than(rec.timestamp)) {
                r.max = recnum;
                laps[lap].records = std::span { records }.subspan(*r.min, *r.max - *r.min);
                laps[lap].segment.analyze(laps[lap].records, false);
            }
        }
    next_record:;
    }
    for (size_t lap = 0; lap < ranges.size(); ++lap) {
        auto const &r = ranges[lap];
        if (r.max) {
            continue;
        }
        assert(r.min.has_value());
        laps[lap].records = std::span { records }.subspan(*r.min);
        laps[lap].segment.analyze(laps[lap].records, false);
    }
    return { };
}

bool Activity::load_fit_file_filter(FITDataRecord const &rec)
{
    switch (rec.mesg_num) {
    case mesg_num::file_id: {
        auto f = make_from_rec<file_id>(rec);
        if (!f) {
            log_error("Error converting file_id message to struct: {}", static_cast<int>(f.error()));
            return true;
        }
        if (f->type != file_type::activity) {
            return false;
        }
        number = f->number.value_or(0);
        serial = f->serial_number.value_or(0);
    } break;
    case mesg_num::session: {
        auto s = make_from_rec<session>(rec);
        if (!s) {
            log_error("Error converting session message to struct: {}", static_cast<int>(s.error()));
            return true;
        }
        segment.start_time = s->start_time;
        segment.moving = Duration { s->total_timer_time };
        segment.elapsed = Duration { s->total_elapsed_time };
        segment.end_time = segment.start_time.end(segment.moving);
        if (s->sport) {
            sport = *s->sport;
        }
        if (s->total_distance) {
            segment.distance = *s->total_distance;
        }
    } break;
    case mesg_num::workout: {
        auto w = make_from_rec<workout>(rec);
        if (!w) {
            log_error("Error converting workout message to struct: {}", static_cast<int>(w.error()));
            return true;
        }
        if (w->sport) {
            sport = *w->sport;
        }
        if (w->wkt_name && !w->wkt_name.value().empty()) {
            title = *w->wkt_name;
        }
    } break;
    case mesg_num::lap: {
        if (depth == LoadingDepth::Deep) {
            auto l = make_from_rec<ST::lap>(rec);
            if (!l) {
                log_error("Error converting lap message to struct: {}", static_cast<int>(l.error()));
                return true;
            }
            Lap lap { };
            lap.segment.start_time = l->start_time;
            lap.segment.moving = Duration { l->total_timer_time };
            lap.segment.elapsed = Duration { l->total_elapsed_time };
            lap.segment.end_time = lap.segment.start_time.end(lap.segment.moving);
            if (l->total_distance) {
                lap.segment.distance = *l->total_distance;
            }
            laps.emplace_back(lap);
        }
    } break;
    case mesg_num::record: {
        if (depth == LoadingDepth::Deep) {
            auto r = make_from_rec<ST::record>(rec);
            if (!r) {
                log_error("Error converting record message to struct: {}", static_cast<int>(r.error()));
                return true;
            }
            records.emplace_back(*r);
        }
    } break;
    default:
        break;
    }
    return false;
}

StorageResult<bool> Activity::load_fit_file(FITFile &fit_file)
{
    // zig's FITFile.openReader() reads the header as part of attaching the
    // reader; in C++ that is an explicit step.
    if (auto res = fit_file.read_header(); !res) {
        std::println("load_fit_file: read_header failed");
        return std::unexpected(StorageError { res.error() });
    }
    if (title.empty()) {
        title = id.name;
    }
    if (auto res = fit_file.read_until([this](FITDataRecord const &rec) { return load_fit_file_filter(rec); }); !res) {
        std::println("load_fit_file: read_until failed");
        return std::unexpected(StorageError { res.error() });
    }
    return segment.start_time.timestamp > 0;
}

// StorageResult<bool> Activity::load_strava_file(fs::path const &dir)
// {
//     auto const file_name = (dir / std::format("{}.strava", id.name)).string();
//     auto       text = read_file_by_name(file_name);
//     if (!text) {
//         return std::unexpected(StorageError { text.error() });
//     }
//     auto json = JSONValue::deserialize(*text);
//     if (!json) {
//         return std::unexpected(StorageError { json.error() });
//     }
//     auto strava_activity = decode<strava::Activity>(*json);
//     if (!strava_activity) {
//         return std::unexpected(StorageError { strava_activity.error() });
//     }
//     title = strava_activity->name.value_or(segment.start_time.format());
//     notes = strava_activity->description.value_or("");
//     return true;
// }

Error<StorageError> Activities::list()
{
    if (auto d = id.dir(*storage); d) {
        std::error_code ec { };
        for (auto const &entry : fs::directory_iterator { *d, ec }) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto const ext = entry.path().extension().string();
            FileTypes  file_types { };
            if (eql_ignore_case(ext, ".fit")) {
                file_types.insert(ActivityFile::Fit);
                // } else if (eql_ignore_case(ext, ".strava")) {
                //     file_types.insert(ActivityFile::Strava);
                // } else if (eql_ignore_case(ext, ".sweattrails")) {
                //     file_types.insert(ActivityFile::SweatTrails);
            }
            if (file_types.count() == 0) {
                continue;
            }
            auto const stem = entry.path().stem().string();
            auto       existing = std::find_if(
                activities.begin(), activities.end(),
                [&stem](Activity const &activity) { return stem == activity.id.name; });
            if (existing != activities.end()) {
                existing->id.files.set_union(file_types);
                continue;
            }
            activities.emplace_back(*storage, ActivityID { id, stem, file_types });
        }
        if (ec) {
            return std::unexpected(StorageError { LibCError { ec.message() } });
        }
        for (auto ix = activities.size(); ix > 0;) {
            --ix;
            if (!TRY_EVAL(activities[ix].load(LoadingDepth::Shallow))) {
                // swapRemove
                activities[ix] = std::move(activities.back());
                activities.pop_back();
            }
        }
        std::sort(activities.begin(), activities.end(), Activity::less_than);
    }
    return { };
}

}
