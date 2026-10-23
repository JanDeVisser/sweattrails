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

std::optional<CadenceStats> cadence_stats(std::span<CadenceSample const> samples)
{
    auto const pedaling = [](CadenceSample const &sample) {
        return sample.cadence >= PedalingCadence && sample.power.value_or(PedalingPower) >= PedalingPower;
    };

    u64    sum = 0;
    size_t count = 0;
    for (auto const &sample : samples) {
        if (pedaling(sample)) {
            sum += sample.cadence;
            ++count;
        }
    }
    if (count == 0) {
        return { };
    }
    CadenceStats stats { .average = static_cast<float32>(sum) / static_cast<float32>(count) };

    // A rolling sum over the current run of consecutive pedaling records.
    u64    run_sum = 0;
    size_t run_length = 0;
    for (size_t ix = 0; ix < samples.size(); ++ix) {
        auto const &sample = samples[ix];
        bool const  follows = ix > 0 && samples[ix - 1].at + 1 == sample.at;
        if (!pedaling(sample)) {
            run_sum = 0;
            run_length = 0;
            continue;
        }
        if (!follows) {
            run_sum = 0;
            run_length = 0;
        }
        run_sum += sample.cadence;
        ++run_length;
        if (run_length > MinCadenceRecords) {
            run_sum -= samples[ix - MinCadenceRecords].cadence;
        }
        if (run_length >= MinCadenceRecords) {
            auto const average = static_cast<float32>(run_sum) / static_cast<float32>(MinCadenceRecords);
            if (!stats.min || average < *stats.min) {
                stats.min = average;
                stats.min_at = sample.at;
            }
        }
    }
    return stats;
}

// The ranges' min_at and max_at are indices into `records`: for a lap,
// they're relative to the lap's first record.
void Segment::analyze(std::span<record> records, bool smooth)
{
    // The lowest altitude since the last rise that counted as climbing.
    std::optional<float32> climb_from { };
    // As recorded, before they're smoothed.
    std::vector<CadenceSample> cadence_samples;
    for (size_t ix = 0; ix < records.size(); ++ix) {
        auto      &rec = records[ix];
        auto const recorded_power = rec.power;
        if (rec.speed) {
            auto const smoothed = computed_speed_range.extend(*rec.speed, ix);
            if (smooth) {
                *rec.speed = smoothed;
            }
        }
        if (rec.power) {
            computed_normalized_range.extend(*rec.power);
            auto const smoothed = computed_power_range.extend(*rec.power, ix);
            if (smooth) {
                *rec.power = smoothed;
            }
        }
        if (rec.cadence) {
            cadence_samples.push_back(CadenceSample { .at = ix, .cadence = *rec.cadence, .power = recorded_power });
            auto const smoothed = computed_cadence_range.extend(*rec.cadence, ix);
            if (smooth) {
                *rec.cadence = smoothed;
            }
        }
        if (rec.heart_rate) {
            auto const smoothed = computed_hr_range.extend(*rec.heart_rate, ix);
            if (smooth) {
                *rec.heart_rate = smoothed;
            }
        }
        if (rec.altitude) {
            auto const elev_int = static_cast<i16>(*rec.altitude);
            computed_elevation_range.extend(elev_int, ix);
            if (!climb_from || *rec.altitude < *climb_from) {
                climb_from = rec.altitude;
            } else if (*rec.altitude - *climb_from >= ClimbHysteresis) {
                computed_climb += *rec.altitude - *climb_from;
                climb_from = rec.altitude;
            }
        }
        if (rec.position) {
            auto const &position = *rec.position;
            if (std::abs(position.lat) > 1 && std::abs(position.lon) > 1) {
                if (box) {
                    box->extend(position);
                } else {
                    std::println("Initializing box");
                    box = Box { .sw = position, .ne = position };
                }
            }
        }
    }
    cadence = cadence_stats(cadence_samples);
    if (computed_normalized_range.range && computed_power_range.range) {
        auto const &np = *computed_normalized_range.range;
        auto const &avg = *computed_power_range.range;
        variability_index = static_cast<float32>(np.normalized) / static_cast<float32>(avg.average);
        intensity_factor = static_cast<float32>(np.normalized) / JANS_FTP;
        // Over the moving time: the stops weren't training.
        tss = std::trunc(
            (static_cast<float32>(moving.elapsed) * static_cast<float32>(np.normalized) * *intensity_factor)
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
        if (!TRY_EVAL(load_fit_file_by_name(dir / std::format("{}.fit", id.name)))) {
            return false;
        }
        // if (id.files.contains(ActivityFile::Strava)) {
        //     if (!TRY_EVAL(load_strava_file(dir))) {
        //         return false;
        //     }
        // }
    }
    TRY(analyze());
    return true;
}

StorageResult<bool> Activity::load_from_file(fs::path const &file)
{
    assert(id.files.contains(ActivityFile::Fit));
    if (!TRY_EVAL(load_fit_file_by_name(file))) {
        return false;
    }
    TRY(analyze());
    return true;
}

StorageResult<bool> Activity::load_fit_file_by_name(fs::path const &file)
{
    auto const file_name = file.string();
    auto       fit_file_maybe = FITFile::read(file_name);
    if (!fit_file_maybe.has_value()) {
        trace(Storage, "Error reading fit file `{}`", file_name);
        return std::unexpected(StorageError { fit_file_maybe.error() });
    }
    auto &fit_file = fit_file_maybe.value();
    auto  try_load = load_fit_file(fit_file);
    if (!try_load.has_value()) {
        trace(Storage, "load_fit_file(`{}`) failed", file_name);
        return std::unexpected(try_load.error());
    }
    if (!try_load.value()) {
        trace(Storage, "load_fit_file(`{}`) returned false", file_name);
        return false;
    }
    return true;
}

StorageResult<bool> Activity::load_from_slice(std::string_view const &buffer)
{
    assert(id.files.contains(ActivityFile::Fit));
    FITFile fit_file { buffer };
    if (!TRY_EVAL(load_fit_file(fit_file))) {
        return false;
    }
    TRY(analyze());
    return true;
}

Error<StorageError> Activity::analyze()
{
    // Laps are contiguous and in chronological order, so a lap runs from its
    // own start time up to the start time of the next lap. Everything from the
    // start of the last lap onwards belongs to the last lap.
    auto const first_at_or_after = [this](DateTime const &t) {
        return std::ranges::lower_bound(records, t.timestamp, { }, [](record const &rec) { return rec.timestamp.timestamp; });
    };
    for (size_t ix = 0; ix < laps.size(); ++ix) {
        auto      &lap = laps[ix];
        auto const begin = first_at_or_after(lap.segment.start_time);
        auto       end = (ix + 1 < laps.size()) ? first_at_or_after(laps[ix + 1].segment.start_time) : records.end();
        if (end < begin) {
            end = begin;
        }
        lap.first_record = static_cast<size_t>(begin - records.begin());
        lap.end_record = static_cast<size_t>(end - records.begin());
        lap.segment.analyze(std::span { begin, end }, false);
    }
    // Last: it smooths the records, and the laps should see them as
    // recorded, as the activity does.
    segment.analyze(records, true);
    return { };
}

bool Activity::load_fit_file_filter(FITDataRecord const &rec)
{
    switch (rec.mesg_num) {
    case mesg_num::file_id: {
        auto f = make_from_rec<file_id>(rec);
        if (!f) {
            warning(Storage, "Error converting file_id message to struct: {}", f.error());
            return true;
        }
        if (f->type != file_type::activity) {
            return false;
        }
        number = f->number.value_or(0);
        serial = f->serial_number.value_or(0);
    } break;
    case mesg_num::activity: {
        auto a = make_from_rec<ST::activity>(rec);
        if (!a) {
            warning(Storage, "Error converting activity message to struct: {}", a.error());
            return true;
        }
        if (a->local_timestamp) {
            utc_offset = static_cast<i32>(static_cast<i64>(a->local_timestamp->timestamp) - static_cast<i64>(a->timestamp.timestamp));
        }
    } break;
    case mesg_num::session: {
        auto s = make_from_rec<session>(rec);
        if (!s) {
            warning(Storage, "Error converting session message to struct: {}", s.error());
            return true;
        }
        segment.start_time = s->start_time;
        segment.moving = Duration { s->total_timer_time };
        segment.elapsed = Duration { s->total_elapsed_time };
        segment.end_time = s->timestamp;
        if (s->sport) {
            sport = *s->sport;
        }
        sub_sport = s->sub_sport;
        if (s->total_distance) {
            segment.distance = *s->total_distance;
        }
        segment.total_ascent = s->total_ascent;
    } break;
    case mesg_num::workout: {
        auto w = make_from_rec<workout>(rec);
        if (!w) {
            warning(Storage, "Error converting workout message to struct: {}", w.error());
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
                warning(Storage, "Error converting lap message to struct: {}", l.error());
                return true;
            }
            Lap lap { };
            lap.segment.start_time = l->start_time;
            lap.segment.moving = Duration { l->total_timer_time };
            lap.segment.elapsed = Duration { l->total_elapsed_time };
            lap.segment.end_time = l->timestamp;
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
                warning(Storage, "Error converting record message to struct: {}", r.error());
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
        trace(Storage, "load_fit_file: read_header failed");
        return std::unexpected(StorageError { res.error() });
    }
    if (title.empty()) {
        title = id.name;
    }
    if (auto res = fit_file.read_until([this](FITDataRecord const &rec) { return load_fit_file_filter(rec); }); !res) {
        trace(Storage, "load_fit_file: read_until failed");
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
        for (fs::directory_iterator it { *d, ec }; !ec && it != fs::directory_iterator { }; it.increment(ec)) {
            auto const     &entry = *it;
            std::error_code entry_ec { };
            if (!entry.is_regular_file(entry_ec)) {
                if (entry_ec) {
                    warning(Storage, "Skipping `{}`: {}", entry.path().string(), LibCError { entry_ec }.to_string());
                }
                continue;
            }
            auto const ext = entry.path().extension().string();
            FileTypes  file_types { };
            // The inbox stores FIT files as `<ts>.fit`, and load() opens
            // `<name>.fit`, which on a case-sensitive filesystem doesn't find
            // e.g. a `.FIT` file copied in by hand.
            if (ext == ".fit") {
                file_types.insert(ActivityFile::Fit);
            } else if (eql_ignore_case(ext, ".fit")) {
                warning(Storage, "Skipping `{}`: activity files must be named `.fit`", entry.path().string());
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
            return std::unexpected(StorageError { LibCError { ec } });
        }
        // An activity that fails to load is left out, rather than failing the
        // whole month.
        for (auto ix = activities.size(); ix > 0;) {
            --ix;
            auto const loaded = activities[ix].load(LoadingDepth::Shallow);
            if (!loaded) {
                warning(Storage, "Skipping activity `{}`: {}", activities[ix].id.name, loaded.error().to_string());
            }
            if (!loaded.value_or(false)) {
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
