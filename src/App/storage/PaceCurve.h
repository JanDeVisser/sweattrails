/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The pace curve, the best time over each distance, of a run and of the
 * runs of a stretch of days, and the critical speed fitted to it.
 */

#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <vector>

#include <fit/Profile.h>
#include <storage/Summaries.h>

namespace ST {

// Metres. Closer together at the short end, with the summaries screen's
// table's distances among them.
constexpr std::array<u32, 18> PaceDistances {
    400,
    800,
    1000,
    1500,
    1609,
    2000,
    3000,
    4000,
    5000,
    8000,
    10000,
    15000,
    16093,
    20000,
    21097,
    25000,
    30000,
    42195,
};

// Distance gained faster than this between two records is a GPS glitch,
// and doesn't count: faster than the 400 m world record's pace.
constexpr float64 MaxRunSpeed = 8; // m/s

// A run's best time, in whole seconds, for each of PaceDistances it
// covered, from the device's distance in its records. Time runs on
// through stops. Empty without distances.
[[nodiscard]] std::vector<u32> pace_curve(std::span<FIT::record const> records);

struct PaceBest {
    u32    seconds { 0 };
    size_t summary { 0 }; // The index of the summary of the run that set it

    bool operator==(PaceBest const &) const = default;
};

// Whether a run counts for the pace curve: not on a treadmill or
// indoors, where the distance is the watch's estimate.
[[nodiscard]] bool outdoor_run(ActivitySummary const &summary);

// The best of the outdoor runs that start from `first` to `last`, for
// each of PaceDistances the longest of them covered.
[[nodiscard]] std::vector<PaceBest> best_pace_curve(ActivitySummaries const &summaries, std::chrono::year_month_day first,
    std::chrono::year_month_day last);

struct CriticalSpeed {
    float64 cs { 0 };      // m/s
    float64 d_prime { 0 }; // m

    bool operator==(CriticalSpeed const &) const = default;
};

// A sound fit needs this many bests of FitFrom to FitTo seconds, the
// longest of them at least this long.
constexpr size_t PaceFitPoints = 3;
constexpr u32    PaceFitLongest = 900;

// The two-parameter model, D = CS × t + D', fitted to the bests that
// took FitFrom to FitTo seconds. None without a sound fit, or if CS or
// D' isn't above zero.
[[nodiscard]] std::optional<CriticalSpeed> critical_speed(std::vector<PaceBest> const &bests);

}
