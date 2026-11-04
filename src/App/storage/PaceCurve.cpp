/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>

#include <storage/PaceCurve.h>
#include <storage/PowerCurve.h>
#include <storage/Volume.h>

namespace ST {

namespace {

// The distance that counts, from the first record with one.
struct Covered {
    std::vector<double> seconds { };
    std::vector<double> metres { };
};

Covered covered(std::span<FIT::record const> records)
{
    Covered ret;
    float   last_distance = 0;
    u32     last_at = 0;
    for (auto const &rec : records) {
        if (!rec.distance) {
            continue;
        }
        auto const at = rec.timestamp.timestamp;
        if (ret.seconds.empty()) {
            ret.seconds.push_back(0);
            ret.metres.push_back(0);
        } else {
            if (at <= last_at) {
                continue; // Out of order, or a second already there
            }
            auto const gained = static_cast<double>(*rec.distance - last_distance);
            auto const counts = gained > 0 && gained <= MaxRunSpeed * (at - last_at);
            ret.seconds.push_back(ret.seconds.back() + (at - last_at));
            ret.metres.push_back(ret.metres.back() + (counts ? gained : 0));
        }
        last_distance = *rec.distance;
        last_at = at;
    }
    return ret;
}

// The shortest time over `distance`, ending on a record, and starting
// where the distance was `distance` before it, between two records.
double fastest(Covered const &run, double distance)
{
    auto best = std::numeric_limits<double>::infinity();
    auto start = 0uz;
    for (auto end = 0uz; end < run.metres.size(); ++end) {
        auto const from = run.metres[end] - distance;
        if (from < 0) {
            continue;
        }
        while (start + 1 < end && run.metres[start + 1] <= from) {
            ++start;
        }
        auto at = run.seconds[start];
        if (auto const span = run.metres[start + 1] - run.metres[start]; span > 0) {
            at += (from - run.metres[start]) / span * (run.seconds[start + 1] - run.seconds[start]);
        }
        best = std::min(best, run.seconds[end] - at);
    }
    return best;
}

}

std::vector<u32> pace_curve(std::span<FIT::record const> records)
{
    auto const       run = covered(records);
    std::vector<u32> ret;
    for (auto const distance : PaceDistances) {
        if (run.metres.empty() || run.metres.back() < distance) {
            break;
        }
        ret.push_back(static_cast<u32>(std::max(1L, std::lround(fastest(run, distance)))));
    }
    return ret;
}

bool outdoor_run(ActivitySummary const &summary)
{
    if (summary.sub_sport == FIT::sub_sport::treadmill || summary.sub_sport == FIT::sub_sport::indoor_running) {
        return false;
    }
    return volume_sport(summary.sport, summary.sub_sport) == VolumeSport::Running;
}

std::vector<PaceBest> best_pace_curve(ActivitySummaries const &summaries, std::chrono::year_month_day first, std::chrono::year_month_day last)
{
    std::vector<PaceBest> ret;
    for (auto ix = 0uz; ix < summaries.size(); ++ix) {
        auto const &summary = summaries[ix];
        auto const  day = start_day(summary);
        if (!outdoor_run(summary) || day < first || day > last) {
            continue;
        }
        for (auto d = 0uz; d < summary.best_times.size(); ++d) {
            if (d == ret.size()) {
                ret.push_back(PaceBest { .seconds = summary.best_times[d], .summary = ix });
            } else if (summary.best_times[d] < ret[d].seconds) {
                ret[d] = PaceBest { .seconds = summary.best_times[d], .summary = ix };
            }
        }
    }
    return ret;
}

// The distance against the time.
std::optional<CriticalSpeed> critical_speed(std::vector<PaceBest> const &bests)
{
    std::vector<Point> points;
    u32                longest = 0;
    for (auto d = 0uz; d < bests.size(); ++d) {
        auto const t = bests[d].seconds;
        if (t >= FitFrom && t <= FitTo) {
            points.push_back(Point { .x = static_cast<double>(t), .y = static_cast<double>(PaceDistances[d]) });
            longest = std::max(longest, t);
        }
    }
    if (points.size() < PaceFitPoints || longest < PaceFitLongest) {
        return { };
    }
    auto const line = fit_line(points);
    if (!line || line->slope <= 0 || line->intercept <= 0) {
        return { };
    }
    return CriticalSpeed { .cs = line->slope, .d_prime = line->intercept };
}

}
