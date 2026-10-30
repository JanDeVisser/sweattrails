/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Laps made after the fact: splits by distance or time, and ranges marked
 * as laps. They're kept as the times the laps after the first start, and
 * replace the device's laps: see specs/after-the-fact-laps.md.
 */

#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <storage/Activity.h>

namespace ST {

// A split that would make more laps than this, such as every 1 m, is
// refused.
constexpr size_t MaxLaps = 1000;

// A marked range's end this close to a lap's start, in seconds, takes
// that start, rather than leaving a lap of a few seconds.
constexpr i64 MarkSnapSeconds = 5;

struct Split {
    enum class Kind {
        Distance, // `every` in meters
        Time,     // `every` in seconds
    };

    Kind    kind { Kind::Distance };
    float32 every { 0 };
};

// "1 km", "0.4km", "10 min": a distance in km or a time in minutes, more
// than 0. Otherwise, what to tell the user.
std::expected<Split, std::string> parse_split(std::string_view text);

// The starts of the laps after the first, Unix times, when the activity is
// split every `split`: each the first record that reaches the next
// multiple of it from the start. The last lap is what's left. Otherwise,
// what to tell the user.
std::expected<std::vector<i64>, std::string> split_starts(Activity const &activity, Split const &split);

// `starts` with the records from `first` to `last` made a lap: starts at
// `first`, and at the record after `last` unless it's the last record,
// each moved to a start within MarkSnapSeconds of it, and the starts in
// between gone.
std::vector<i64> mark_starts(Activity const &activity, std::vector<i64> const &starts, size_t first, size_t last);

// The laps that `starts` make, from the activity's first record to its
// last, with their stats from range_segment(), each up to the next lap's
// first record, so that they add up to the activity. A start that would make an
// empty lap, at or before the first record, after the last, or on the same
// record as another, is passed over.
std::vector<Lap> laps_at(Activity const &activity, std::vector<i64> const &starts);

// The starts of the device's laps after the first.
std::vector<i64> device_lap_starts(Activity const &activity);

}
