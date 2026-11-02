/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>

#include <storage/Activity.h>
#include <storage/PowerCurve.h>
#include <storage/Volume.h>

namespace ST {

namespace {

u16 counted(std::optional<u16> power)
{
    return power && *power <= MaxPower ? *power : 0;
}

// The power of each second from the first record to the last.
std::vector<u16> by_second(std::span<FIT::record const> records)
{
    std::vector<u16> ret;
    auto const       first = records.front().timestamp.timestamp;
    for (auto const &rec : records) {
        auto const at = rec.timestamp.timestamp;
        if (at < first || at - first < ret.size()) {
            continue; // Out of order, or a second already there
        }
        auto const gap = at - first - ret.size();
        // The seconds before this record: held through a short gap.
        ret.resize(ret.size() + gap, gap < RangeMovingGap ? counted(rec.power) : u16 { 0 });
        ret.push_back(counted(rec.power));
    }
    return ret;
}

}

std::vector<u16> power_curve(std::span<FIT::record const> records)
{
    // Some devices write 0 W without a power meter, as NP has it.
    if (std::ranges::none_of(records, [](FIT::record const &rec) { return counted(rec.power) > 0; })) {
        return { };
    }
    auto const       seconds = by_second(records);
    std::vector<u64> sums(seconds.size() + 1, 0);
    for (auto ix = 0uz; ix < seconds.size(); ++ix) {
        sums[ix + 1] = sums[ix] + seconds[ix];
    }
    std::vector<u16> ret;
    for (auto const duration : PowerDurations) {
        if (duration > seconds.size()) {
            break;
        }
        u64 best = 0;
        for (auto end = static_cast<size_t>(duration); end < sums.size(); ++end) {
            best = std::max(best, sums[end] - sums[end - duration]);
        }
        ret.push_back(static_cast<u16>(std::lround(static_cast<double>(best) / duration)));
    }
    return ret;
}

std::vector<PowerBest> best_curve(ActivitySummaries const &summaries, std::chrono::year_month_day first, std::chrono::year_month_day last)
{
    std::vector<PowerBest> ret;
    for (auto ix = 0uz; ix < summaries.size(); ++ix) {
        auto const &summary = summaries[ix];
        auto const  day = start_day(summary);
        if (volume_sport(summary.sport, summary.sub_sport) != VolumeSport::Cycling || day < first || day > last) {
            continue;
        }
        for (auto d = 0uz; d < summary.power.size(); ++d) {
            if (d == ret.size()) {
                ret.push_back(PowerBest { .watts = summary.power[d], .summary = ix });
            } else if (summary.power[d] > ret[d].watts) {
                ret[d] = PowerBest { .watts = summary.power[d], .summary = ix };
            }
        }
    }
    return ret;
}

// Least squares of the work against the time.
std::optional<CriticalPower> critical_power(std::vector<PowerBest> const &bests)
{
    double n = 0;
    double sum_t = 0;
    double sum_w = 0;
    double sum_tt = 0;
    double sum_tw = 0;
    for (auto d = 0uz; d < bests.size(); ++d) {
        auto const t = static_cast<double>(PowerDurations[d]);
        if (t < FitFrom || t > FitTo) {
            continue;
        }
        auto const w = static_cast<double>(bests[d].watts) * t;
        n += 1;
        sum_t += t;
        sum_w += w;
        sum_tt += t * t;
        sum_tw += t * w;
    }
    if (bests.size() <= static_cast<size_t>(std::ranges::find(PowerDurations, FitTo) - PowerDurations.begin())) {
        return { };
    }
    auto const cp = (n * sum_tw - sum_t * sum_w) / (n * sum_tt - sum_t * sum_t);
    auto const w_prime = (sum_w - cp * sum_t) / n;
    if (cp <= 0 || w_prime <= 0) {
        return { };
    }
    return CriticalPower { .cp = cp, .w_prime = w_prime };
}

}
