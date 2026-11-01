/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>

#include <IO.h>

#include <storage/Activity.h>
#include <storage/Gaps.h>

namespace ST {

namespace fs = std::filesystem;

namespace {

bool has_position(record const &rec)
{
    return rec.position && rec.position->has_fix();
}

u32 fit_time(DateTime const &time)
{
    return static_cast<u32>(static_cast<i64>(time.timestamp) - DateTime::FIT_TIMESTAMP_OFFSET);
}

i32 to_semicircles(float32 degrees)
{
    return static_cast<i32>(std::llround(static_cast<float64>(degrees) * (2147483648.0 / 180)));
}

// The record of `records` nearest to `time`, within CoverTime, with a
// position and a distance.
std::optional<record> nearest(std::span<record const> records, DateTime const &time)
{
    std::optional<record> ret;
    u32                   best { CoverTime + 1 };
    for (auto const &rec : records) {
        if (!has_position(rec) || !rec.distance) {
            continue;
        }
        auto const apart = static_cast<u32>(std::abs(static_cast<i64>(rec.timestamp.timestamp) - static_cast<i64>(time.timestamp)));
        if (apart < best) {
            best = apart;
            ret = rec;
        }
    }
    return ret;
}

std::string since(u32 seconds)
{
    return std::format("{}:{:02}:{:02}", seconds / 3600, (seconds % 3600) / 60, seconds % 60);
}

}

std::vector<Gap> find_gaps(std::span<record const> records)
{
    std::vector<Gap> ret;
    for (auto ix = 1uz; ix < records.size(); ++ix) {
        auto const &before = records[ix - 1];
        auto const &after = records[ix];
        if (!has_position(before) || !has_position(after)) {
            continue;
        }
        auto const apart = static_cast<i64>(after.timestamp.timestamp) - static_cast<i64>(before.timestamp.timestamp);
        if (apart > GapTime && distance_between(*before.position, *after.position) > GapDistance) {
            ret.push_back(Gap { .before = ix - 1, .stop = before.timestamp, .restart = after.timestamp, .from = *before.position, .to = *after.position });
        }
    }
    return ret;
}

std::vector<FIT::FITFill> plan_fills(std::span<record const> records, std::vector<Gap> const &gaps, std::span<record const> other)
{
    std::vector<FIT::FITFill> ret;
    // The watch's distance at a later stop doesn't have the earlier fills
    // in it, which the patch adds to its records after them.
    auto filled_before = 0.0;
    for (auto const &gap : gaps) {
        auto const at_stop = nearest(other, gap.stop);
        auto const at_restart = nearest(other, gap.restart);
        auto const stop_distance = records[gap.before].distance;
        // A watch without a distance at the stop is covered too: the patch
        // says why it can't be filled.
        if (!at_stop || !at_restart
            || distance_between(*at_stop->position, gap.from) > CoverDistance
            || distance_between(*at_restart->position, gap.to) > CoverDistance) {
            continue;
        }
        FIT::FITFill fill {
            .stop = fit_time(gap.stop),
            .restart = fit_time(gap.restart),
            .distance = std::max(0.0, static_cast<float64>(*at_restart->distance) - static_cast<float64>(*at_stop->distance)),
        };
        for (auto const &rec : other) {
            if (rec.timestamp.timestamp <= gap.stop.timestamp || rec.timestamp.timestamp >= gap.restart.timestamp) {
                continue;
            }
            FIT::FillRecord fill_record { .timestamp = fit_time(rec.timestamp), .altitude = rec.altitude, .speed = rec.speed };
            if (has_position(rec)) {
                fill_record.lat = to_semicircles(rec.position->lat);
                fill_record.lon = to_semicircles(rec.position->lon);
            }
            if (rec.distance && stop_distance) {
                fill_record.distance = *stop_distance + filled_before + std::max(0.0, static_cast<float64>(*rec.distance) - static_cast<float64>(*at_stop->distance));
            }
            fill.records.push_back(fill_record);
        }
        filled_before += fill.distance;
        ret.push_back(std::move(fill));
    }
    return ret;
}

std::string fill_question(std::vector<FIT::FITFill> const &fills, DateTime const &start)
{
    std::vector<std::string> parts;
    for (auto const &fill : fills) {
        auto const seconds = static_cast<u32>(std::max<i64>(0, static_cast<i64>(fill.stop) - static_cast<i64>(fit_time(start))));
        parts.push_back(std::format("{:.1f} km at {}", fill.distance / 1000, since(seconds)));
    }
    std::string list;
    for (auto const &part : parts) {
        list += list.empty() ? part : ", " + part;
    }
    if (fills.size() == 1) {
        return std::format("Fill the gap: {}?", list);
    }
    return std::format("Fill {} gaps: {}?", fills.size(), list);
}

std::optional<std::string> drop_problem(std::vector<std::string> const &paths, bool showing_activity)
{
    if (!showing_activity) {
        return "Show the activity to fill, and drop the file on it.";
    }
    if (paths.size() != 1) {
        return "Drop one file at a time.";
    }
    return { };
}

fs::path fit_path(Activity const &activity)
{
    return fs::path { activity.sidecar }.replace_extension(".fit");
}

fs::path original_path(Activity const &activity)
{
    return fs::path { activity.sidecar }.replace_extension(".fit.original");
}

std::expected<void, std::string> check_fills(Activity const &activity, std::vector<FIT::FITFill> const &fills)
{
    auto const fit = fit_path(activity);
    auto const bytes = read_file_by_name(fit.string());
    if (!bytes) {
        return std::unexpected(std::format("can't read {}: {}", fit.string(), bytes.error().to_string()));
    }
    if (auto const patched = FIT::patch_fit(*bytes, fills); !patched) {
        return std::unexpected(patched.error());
    }
    return { };
}

std::expected<void, std::string> fill_gaps(Activity const &activity, std::vector<FIT::FITFill> const &fills)
{
    auto const fit = fit_path(activity);
    auto const original = original_path(activity);
    auto const bytes = read_file_by_name(fit.string());
    if (!bytes) {
        return std::unexpected(std::format("can't read {}: {}", fit.string(), bytes.error().to_string()));
    }
    auto const patched = FIT::patch_fit(*bytes, fills);
    if (!patched) {
        return std::unexpected(std::format("it can't be filled: {}", patched.error()));
    }
    // The watch's, once: a second fill starts from the first's file.
    std::error_code ec { };
    if (!fs::exists(original, ec)) {
        if (auto const kept = write_file_atomically(original, *bytes); !kept) {
            return std::unexpected(std::format("can't keep the original: {}", kept.error().to_string()));
        }
    }
    if (auto const written = write_file_atomically(fit, *patched); !written) {
        return std::unexpected(std::format("can't write {}: {}", fit.string(), written.error().to_string()));
    }
    return { };
}

std::expected<void, std::string> remove_fills(Activity const &activity)
{
    auto const      original = original_path(activity);
    std::error_code ec { };
    if (!fs::exists(original, ec)) {
        return std::unexpected(std::string { "This activity has no fills to remove." });
    }
    fs::rename(original, fit_path(activity), ec);
    if (ec) {
        return std::unexpected(std::format("Couldn't put back the original: {}", ec.message()));
    }
    return { };
}

}
