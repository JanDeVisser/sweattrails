/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>

#include <storage/Boundaries.h>

namespace ST {

namespace {

bool still(record const &rec)
{
    return rec.speed && *rec.speed < RangeMovingSpeed;
}

// Signed: a watch can set its clock back.
i64 seconds_between(record const &from, record const &to)
{
    return static_cast<i64>(to.timestamp.timestamp) - static_cast<i64>(from.timestamp.timestamp);
}

// The distance at record `ix`, or at the last record before it that has
// one: 0 before the first.
float distance_at(std::span<record const> records, size_t ix)
{
    for (auto i = ix + 1; i > 0; --i) {
        if (records[i - 1].distance) {
            return *records[i - 1].distance;
        }
    }
    return 0;
}

bool has_distance(std::span<record const> records)
{
    return std::ranges::any_of(records, [](record const &rec) { return rec.distance.has_value(); });
}

// The first record at or past `metres`.
std::optional<size_t> first_at(std::span<record const> records, float metres)
{
    for (auto ix = 0uz; ix < records.size(); ++ix) {
        if (records[ix].distance && *records[ix].distance >= metres) {
            return ix;
        }
    }
    return { };
}

std::optional<size_t> km_on(std::span<record const> records, size_t from)
{
    auto const next = std::floor(distance_at(records, from) / 1000) + 1;
    for (auto ix = from + 1; ix < records.size(); ++ix) {
        if (records[ix].distance && *records[ix].distance >= next * 1000) {
            return ix;
        }
    }
    return { };
}

// The first km starts at the first record, whatever its distance.
std::optional<size_t> km_back(std::span<record const> records, size_t from)
{
    for (auto km = std::floor(distance_at(records, from) / 1000); km >= 0; --km) {
        auto const at = (km == 0) ? std::optional<size_t> { 0 } : first_at(records, static_cast<float>(km * 1000));
        if (at && *at < from) {
            return at;
        }
    }
    return { };
}

// The laps' last records after `from`, the nearest.
std::optional<size_t> lap_on(std::vector<Lap> const &laps, size_t from)
{
    std::optional<size_t> ret;
    for (auto const &lap : laps) {
        if (lap.end_record > lap.first_record && lap.end_record - 1 > from) {
            ret = std::min(ret.value_or(lap.end_record - 1), lap.end_record - 1);
        }
    }
    return ret;
}

std::optional<size_t> lap_back(std::vector<Lap> const &laps, size_t from)
{
    std::optional<size_t> ret;
    for (auto const &lap : laps) {
        if (lap.end_record > lap.first_record && lap.first_record < from) {
            ret = std::max(ret.value_or(lap.first_record), lap.first_record);
        }
    }
    return ret;
}

std::optional<size_t> stop_on(std::span<record const> recorded, size_t from)
{
    for (auto const &stop : stops(recorded)) {
        if (stop.last_moving && *stop.last_moving > from) {
            return stop.last_moving;
        }
    }
    return { };
}

std::optional<size_t> stop_back(std::span<record const> recorded, size_t from)
{
    std::optional<size_t> ret;
    for (auto const &stop : stops(recorded)) {
        if (stop.first_moving && *stop.first_moving < from) {
            ret = stop.first_moving;
        }
    }
    return ret;
}

}

std::vector<Stop> stops(std::span<record const> recorded)
{
    std::vector<Stop>     ret;
    std::optional<size_t> last_moving;
    // What the moving time leaves out since the last record that moved:
    // the time up to each record standing still, and each pause.
    i64  left_out = 0;
    bool paused = false;
    auto stopped = [&left_out, &paused]() { return paused || left_out >= RangeMovingGap; };
    for (auto ix = 0uz; ix < recorded.size(); ++ix) {
        auto const gap = ix > 0 ? seconds_between(recorded[ix - 1], recorded[ix]) : 0;
        // A clock going back is no time at all.
        if (gap > RangeMovingGap) {
            paused = true;
        } else if (gap > 0 && still(recorded[ix])) {
            left_out += gap;
        }
        if (still(recorded[ix])) {
            continue;
        }
        if (ix > 0 && stopped()) {
            ret.push_back({ .last_moving = last_moving, .first_moving = ix });
        }
        last_moving = ix;
        left_out = 0;
        paused = false;
    }
    if (last_moving && stopped()) {
        ret.push_back({ .last_moving = last_moving });
    }
    return ret;
}

size_t laps_with_records(Activity const &activity)
{
    return static_cast<size_t>(std::ranges::count_if(activity.laps, [](Lap const &lap) { return lap.end_record > lap.first_record; }));
}

std::optional<size_t> extend_to(Activity const &activity, size_t from, Boundary boundary, Direction direction)
{
    std::span<record const> const records = activity.records;
    if (from >= records.size()) {
        return { };
    }
    switch (boundary) {
    case Boundary::Km:
        if (!has_distance(records)) {
            return { };
        }
        return direction == Direction::On ? km_on(records, from) : km_back(records, from);
    case Boundary::Lap:
        if (laps_with_records(activity) < 2) {
            return { };
        }
        return direction == Direction::On ? lap_on(activity.laps, from) : lap_back(activity.laps, from);
    case Boundary::Stop: {
        // The speeds as recorded: the analysis smooths `records`.
        std::span<record const> const recorded = activity.recorded.size() == records.size() ? std::span<record const> { activity.recorded } : records;
        return direction == Direction::On ? stop_on(recorded, from) : stop_back(recorded, from);
    }
    }
    return { };
}

}
