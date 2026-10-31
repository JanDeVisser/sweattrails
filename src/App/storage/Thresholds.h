/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The user's thresholds, dated, from the settings, and the training
 * stress an activity scores against them.
 */

#pragma once

#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <JSON.h>

#include <fit/Profile.h>

namespace ST {

// From a day on: an FTP, and a lactate threshold heart rate for running
// and for cycling. A threshold left out keeps the one before.
struct Thresholds {
    std::chrono::year_month_day from { };
    std::optional<float64>      ftp { };          // W
    std::optional<float64>      lthr_running { }; // bpm
    std::optional<float64>      lthr_cycling { }; // bpm

    bool operator==(Thresholds const &) const = default;
};

using ThresholdList = std::vector<Thresholds>;

// The settings' `thresholds`, earliest first, or what's wrong with them.
// None in the settings is an empty list.
[[nodiscard]] std::expected<ThresholdList, std::string> decode_thresholds(std::optional<JSONValue> const &json);

// The thresholds in force on `day`, each from the entry that last set it.
// The first entry's count back before its date. None for an empty list.
[[nodiscard]] std::optional<Thresholds> thresholds_on(ThresholdList const &list, std::chrono::year_month_day day);

struct TrainingStress {
    float64 value { 0 };
    bool    heart_rate { false }; // An hrTSS, not power's

    bool operator==(TrainingStress const &) const = default;
};

// What a stretch of training scored: power's TSS if it has an NP and
// there's an FTP, else an hrTSS from `hr_load`, Σ HR² × seconds, with the
// LTHR for its sport: cycling's for a ride, running's for anything else.
// None without either.
[[nodiscard]] std::optional<TrainingStress> training_stress(
    std::optional<float64>           np,
    u32                              moving,
    std::optional<float64>           hr_load,
    std::optional<FIT::sport>        sport,
    std::optional<FIT::sub_sport>    sub_sport,
    std::optional<Thresholds> const &thresholds);

// Power's intensity factor, NP against FTP.
[[nodiscard]] std::optional<float64> intensity_factor(std::optional<float64> np, std::optional<Thresholds> const &thresholds);

}
