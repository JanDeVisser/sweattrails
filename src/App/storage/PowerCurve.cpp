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

// The power of each second from the first record to the last. A pause
// longer than the longest duration is cut to it, which changes no best:
// a file with a few timestamps from when the device was switched on
// would otherwise have a pause of decades.
std::vector<u16> by_second(std::span<FIT::record const> records)
{
    std::vector<u16>   ret;
    std::optional<u32> last { };
    for (auto const &rec : records) {
        auto const at = rec.timestamp.timestamp;
        if (last && at <= *last) {
            continue; // Out of order, or a second already there
        }
        if (last) {
            auto const gap = at - *last - 1;
            // The seconds before this record: held through a short gap.
            ret.resize(ret.size() + std::min(gap, PowerDurations.back()), gap < RangeMovingGap ? counted(rec.power) : u16 { 0 });
        }
        ret.push_back(counted(rec.power));
        last = at;
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

std::optional<Line> fit_line(std::span<Point const> points)
{
    if (points.size() < 2) {
        return { };
    }
    auto const n = static_cast<double>(points.size());
    double     sum_x = 0;
    double     sum_y = 0;
    double     sum_xx = 0;
    double     sum_xy = 0;
    for (auto const &[x, y] : points) {
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }
    auto const denominator = n * sum_xx - sum_x * sum_x;
    if (denominator == 0) {
        return { };
    }
    auto const slope = (n * sum_xy - sum_x * sum_y) / denominator;
    return Line { .slope = slope, .intercept = (sum_y - slope * sum_x) / n };
}

// The work against the time.
std::optional<CriticalPower> critical_power(std::vector<PowerBest> const &bests)
{
    if (bests.size() <= static_cast<size_t>(std::ranges::find(PowerDurations, FitTo) - PowerDurations.begin())) {
        return { };
    }
    std::vector<Point> points;
    for (auto d = 0uz; d < bests.size(); ++d) {
        auto const t = static_cast<double>(PowerDurations[d]);
        if (t >= FitFrom && t <= FitTo) {
            points.push_back(Point { .x = t, .y = static_cast<double>(bests[d].watts) * t });
        }
    }
    auto const line = fit_line(points);
    if (!line || line->slope <= 0 || line->intercept <= 0) {
        return { };
    }
    return CriticalPower { .cp = line->slope, .w_prime = line->intercept };
}

}
