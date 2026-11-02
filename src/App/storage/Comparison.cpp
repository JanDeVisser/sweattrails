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

// The average and the maximum of a value.
struct Stats {
    float64 sum { 0 };
    float64 max { 0 };
    size_t  count { 0 };

    void add(float64 value)
    {
        sum += value;
        max = std::max(max, value);
        ++count;
    }

    [[nodiscard]] std::optional<float64> average() const
    {
        return count > 0 ? std::optional { sum / static_cast<float64>(count) } : std::nullopt;
    }

    [[nodiscard]] std::optional<float64> maximum() const
    {
        return count > 0 ? std::optional { max } : std::nullopt;
    }
};

// A value of both, over the paired records where both have it. Where they
// never both have it, each one's own, so that a value only one recording
// has shows as that one's, with nothing to compare it with.
struct Both {
    Stats shown { };
    Stats other { };
    Stats shown_alone { };
    Stats other_alone { };

    void add(std::optional<float64> a, std::optional<float64> b)
    {
        if (a && b) {
            shown.add(*a);
            other.add(*b);
            return;
        }
        if (a) {
            shown_alone.add(*a);
        }
        if (b) {
            other_alone.add(*b);
        }
    }

    [[nodiscard]] Compared average() const
    {
        if (shown.count > 0) {
            return Compared { .shown = shown.average(), .other = other.average() };
        }
        return Compared { .shown = shown_alone.average(), .other = other_alone.average() };
    }

    [[nodiscard]] Compared maximum() const
    {
        if (shown.count > 0) {
            return Compared { .shown = shown.maximum(), .other = other.maximum() };
        }
        return Compared { .shown = shown_alone.maximum(), .other = other_alone.maximum() };
    }
};

template<typename T>
std::optional<float64> value(std::optional<T> const &v)
{
    return v ? std::optional { static_cast<float64>(*v) } : std::nullopt;
}

// Coasting, or standing still, isn't a cadence to average.
std::optional<float64> pedaling(std::optional<u8> const &cadence)
{
    return cadence && *cadence > 0 ? value(cadence) : std::nullopt;
}

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
        static FIT::record const none { };
        auto const              &a = shown[ix];
        auto const              &b = pairs[ix] && *pairs[ix] < other.size() ? other[*pairs[ix]] : none;
        power.add(value(a.power), value(b.power));
        if (a.power && b.power) {
            shown_np.extend(*a.power);
            other_np.extend(*b.power);
        }
        cadence.add(pedaling(a.cadence), pedaling(b.cadence));
        hr.add(value(a.heart_rate), value(b.heart_rate));
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
