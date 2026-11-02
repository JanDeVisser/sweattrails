/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The power curve, the best average power held for each duration, of a
 * ride and of the rides of a stretch of days, and the critical power
 * fitted to it.
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

// Seconds. Roughly geometric, so that every scale has points, and with
// the summaries screen's table's durations among them.
constexpr std::array<u32, 36> PowerDurations {
    1,
    2,
    3,
    5,
    8,
    10,
    15,
    20,
    30,
    45,
    60,
    90,
    120,
    180,
    240,
    300,
    420,
    600,
    720,
    900,
    1200,
    1500,
    1800,
    2400,
    3000,
    3600,
    4500,
    5400,
    7200,
    9000,
    10800,
    14400,
    18000,
    21600,
    28800,
    36000,
};

// Readings above it are a dropout or a bad pairing, and count as zero.
constexpr u16 MaxPower = 1500;

// A ride's best average power for each of PowerDurations it lasts, in W.
// The records are laid out a second each: a gap of up to RangeMovingGap
// holds the power of the record after it, as smart recording leaves
// gaps while riding, and a longer gap is a pause, at zero. Empty if no
// record has power above zero.
[[nodiscard]] std::vector<u16> power_curve(std::span<FIT::record const> records);

struct PowerBest {
    u16    watts { 0 };
    size_t summary { 0 }; // The index of the summary of the ride that set it

    bool operator==(PowerBest const &) const = default;
};

// The best of the rides that start from `first` to `last`, for each of
// PowerDurations the longest of them lasts.
[[nodiscard]] std::vector<PowerBest> best_curve(ActivitySummaries const &summaries, std::chrono::year_month_day first,
    std::chrono::year_month_day last);

struct CriticalPower {
    float64 cp { 0 };      // W
    float64 w_prime { 0 }; // J

    bool operator==(CriticalPower const &) const = default;
};

// The shortest and longest efforts the critical power is fitted to.
constexpr u32 FitFrom = 180;
constexpr u32 FitTo = 1200;

// The two-parameter model, W = CP × t + W', fitted to the bests from
// FitFrom to FitTo seconds. None without a best that long, or if CP or
// W' isn't above zero.
[[nodiscard]] std::optional<CriticalPower> critical_power(std::vector<PowerBest> const &bests);

}
