/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <StringUtil.h>

#include <storage/Thresholds.h>
#include <storage/Volume.h>

namespace ST {

namespace chrono = std::chrono;

namespace {

std::optional<chrono::year_month_day> parse_day(std::string_view text)
{
    auto const parts = split(text, '-');
    if (parts.size() != 3) {
        return { };
    }
    auto const year = string_to_integer<int>(parts[0], 10);
    auto const month = string_to_integer<unsigned>(parts[1], 10);
    auto const day = string_to_integer<unsigned>(parts[2], 10);
    if (!year || !month || !day) {
        return { };
    }
    auto const ret = chrono::year { *year } / chrono::month { *month } / chrono::day { *day };
    if (!ret.ok()) {
        return { };
    }
    return ret;
}

std::expected<std::optional<float64>, std::string> threshold(JSONValue const &entry, std::string_view name, size_t ix)
{
    if (!entry.has(name)) {
        return std::optional<float64> { };
    }
    auto const value = entry.try_get<float64>(name);
    if (!value || *value <= 0) {
        return std::unexpected(std::format("threshold {}'s `{}` isn't a number above 0", ix + 1, name));
    }
    return std::optional<float64> { *value };
}

}

std::expected<ThresholdList, std::string> decode_thresholds(std::optional<JSONValue> const &json)
{
    if (!json || json->is_null()) {
        return ThresholdList { };
    }
    if (!json->is_array()) {
        return std::unexpected(std::string { "`thresholds` isn't a list" });
    }
    ThresholdList ret;
    for (auto ix = 0uz; ix < json->size(); ++ix) {
        auto const &entry = (*json)[static_cast<unsigned>(ix)];
        if (!entry.is_object()) {
            return std::unexpected(std::format("threshold {} isn't an object", ix + 1));
        }
        auto const from_text = entry.try_get<std::string>("from");
        auto const from = from_text ? parse_day(*from_text) : std::nullopt;
        if (!from) {
            return std::unexpected(std::format("threshold {}'s `from` isn't a date, as \"2024-03-01\"", ix + 1));
        }
        if (!ret.empty() && *from <= ret.back().from) {
            return std::unexpected(std::format("threshold {} isn't after the one before it", ix + 1));
        }
        ret.push_back(Thresholds {
            .from = *from,
            .ftp = TRY_EVAL(threshold(entry, "ftp", ix)),
            .lthr_running = TRY_EVAL(threshold(entry, "lthr_running", ix)),
            .lthr_cycling = TRY_EVAL(threshold(entry, "lthr_cycling", ix)),
        });
    }
    return ret;
}

std::optional<Thresholds> thresholds_on(ThresholdList const &list, chrono::year_month_day day)
{
    if (list.empty()) {
        return { };
    }
    auto ret = list.front();
    for (auto const &entry : list) {
        if (entry.from > day) {
            break;
        }
        ret.from = entry.from;
        ret.ftp = entry.ftp.or_else([&ret] { return ret.ftp; });
        ret.lthr_running = entry.lthr_running.or_else([&ret] { return ret.lthr_running; });
        ret.lthr_cycling = entry.lthr_cycling.or_else([&ret] { return ret.lthr_cycling; });
    }
    return ret;
}

std::optional<TrainingStress> training_stress(
    std::optional<float64>           np,
    u32                              moving,
    std::optional<float64>           hr_load,
    std::optional<FIT::sport>        sport,
    std::optional<FIT::sub_sport>    sub_sport,
    std::optional<Thresholds> const &thresholds)
{
    if (!thresholds) {
        return { };
    }
    // An hour at threshold is 100: 3600 s × 1² / 36.
    if (np && *np > 0 && thresholds->ftp) {
        auto const ratio = *np / *thresholds->ftp;
        return TrainingStress { .value = moving * ratio * ratio / 36 };
    }
    auto const lthr = volume_sport(sport, sub_sport) == VolumeSport::Cycling ? thresholds->lthr_cycling : thresholds->lthr_running;
    if (hr_load && *hr_load > 0 && lthr) {
        return TrainingStress { .value = *hr_load / (*lthr * *lthr * 36), .heart_rate = true };
    }
    return { };
}

std::optional<float64> intensity_factor(std::optional<float64> np, std::optional<Thresholds> const &thresholds)
{
    if (!np || *np <= 0 || !thresholds || !thresholds->ftp) {
        return { };
    }
    return *np / *thresholds->ftp;
}

}
