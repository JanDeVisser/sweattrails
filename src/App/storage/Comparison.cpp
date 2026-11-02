/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <functional>

#include <Range.h>
#include <storage/Comparison.h>

namespace ST {

namespace {

// Averages and maxima of one value over the paired records.
struct Both {
    float64 shown_sum { 0 };
    float64 other_sum { 0 };
    float64 shown_max { 0 };
    float64 other_max { 0 };
    size_t  count { 0 };

    void add(float64 shown, float64 other)
    {
        shown_sum += shown;
        other_sum += other;
        shown_max = std::max(shown_max, shown);
        other_max = std::max(other_max, other);
        ++count;
    }

    [[nodiscard]] Compared average() const
    {
        if (count == 0) {
            return { };
        }
        return Compared { .shown = shown_sum / static_cast<float64>(count), .other = other_sum / static_cast<float64>(count) };
    }

    [[nodiscard]] Compared maximum() const
    {
        if (count == 0) {
            return { };
        }
        return Compared { .shown = shown_max, .other = other_max };
    }
};

}

std::vector<std::optional<size_t>> pair_records(std::span<FIT::record const> shown, std::span<FIT::record const> other)
{
    std::vector<std::optional<size_t>> ret;
    ret.reserve(shown.size());
    size_t next { 0 }; // The first of `other`'s records after the shown one's time
    for (auto const &rec : shown) {
        auto const t = rec.timestamp.timestamp;
        while (next < other.size() && other[next].timestamp.timestamp <= t) {
            ++next;
        }
        if (next > 0 && t - other[next - 1].timestamp.timestamp <= PairHoldSeconds) {
            ret.push_back(next - 1);
        } else {
            ret.push_back(std::nullopt);
        }
    }
    return ret;
}

std::optional<float64> Compared::difference() const
{
    if (!shown || !other) {
        return { };
    }
    return *shown - *other;
}

std::optional<float64> Compared::percent() const
{
    if (!shown || !other || *other == 0) {
        return { };
    }
    return (*shown - *other) / *other * 100.0;
}

Comparison compare(std::span<FIT::record const> shown, std::span<FIT::record const> other, std::span<std::optional<size_t> const> pairs, size_t first, size_t end)
{
    Both                     power;
    Both                     cadence;
    Both                     hr;
    NormalizedRange<u16, 30> shown_np;
    NormalizedRange<u16, 30> other_np;
    end = std::min({ end, shown.size(), pairs.size() });
    for (auto ix = first; ix < end; ++ix) {
        if (!pairs[ix] || *pairs[ix] >= other.size()) {
            continue;
        }
        auto const &a = shown[ix];
        auto const &b = other[*pairs[ix]];
        if (a.power && b.power) {
            power.add(*a.power, *b.power);
            shown_np.extend(*a.power);
            other_np.extend(*b.power);
        }
        if (a.cadence && b.cadence && *a.cadence > 0 && *b.cadence > 0) {
            cadence.add(*a.cadence, *b.cadence);
        }
        if (a.heart_rate && b.heart_rate) {
            hr.add(*a.heart_rate, *b.heart_rate);
        }
    }
    Comparison ret {
        .avg_power = power.average(),
        .max_power = power.maximum(),
        .avg_cadence = cadence.average(),
        .avg_hr = hr.average(),
        .max_hr = hr.maximum(),
    };
    if (shown_np.range && other_np.range && shown_np.range->samples > 30) {
        ret.np = Compared { .shown = shown_np.range->normalized, .other = other_np.range->normalized };
    }
    return ret;
}

}
