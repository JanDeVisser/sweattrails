/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/range.zig.
 *
 * The zig generic functions `Range(T)`, `SmoothedRange(T, smooth)` and
 * `NormalizedRange(T, smooth)` become templates; the anonymous optional struct
 * `range: ?struct { ... }` becomes std::optional<RangeData>.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <optional>
#include <type_traits>

#include <Logging.h>

namespace ST {

template<Number T, size_t Smooth = 1>
    requires(Smooth > 0)
struct SmoothedRange {
    using AccumType = std::conditional_t<
        std::floating_point<T>,
        float64,
        std::conditional_t<std::is_signed_v<T>, i64, u64>>;

    struct RangeData {
        T                     min { 0 };
        T                     max { 0 };
        T                     rolling { 0 };
        AccumType             accumulated { 0 };
        std::array<T, Smooth> window { };
        size_t                samples { 0 };
        size_t                min_at { 0 };
        size_t                max_at { 0 };
        T                     average { 0 };
    };

    std::optional<RangeData> range { };

    static AccumType to_accum_type(T value)
    {
        return static_cast<AccumType>(value);
    }

    static T calc_avg(AccumType sum, size_t samples)
    {
        if constexpr (std::floating_point<T>) {
            return static_cast<T>(sum / static_cast<AccumType>(samples));
        } else {
            return static_cast<T>(sum / static_cast<AccumType>(samples));
        }
    }

    T extend(T value)
    {
        if (!range) {
            range = RangeData { };
        }
        auto &r = *range;
        if constexpr (Smooth > 1) {
            r.window[r.samples % Smooth] = value;
        }
        r.samples += 1;
        if (r.samples < Smooth) {
            return 0;
        }
        if constexpr (Smooth > 1) {
            AccumType sum { 0 };
            for (auto const &v : r.window) {
                sum += to_accum_type(v);
            }
            r.rolling = calc_avg(sum, Smooth);
        } else {
            r.rolling = value;
        }
        if (r.samples == Smooth) {
            r.min = r.rolling;
            r.min_at = Smooth - 1;
            r.max = r.rolling;
            r.max_at = Smooth - 1;
        } else {
            r.min = std::min(r.min, r.rolling);
            if (r.min == r.rolling) {
                r.min_at = r.samples - 1;
            }
            r.max = std::max(r.max, r.rolling);
            if (r.max == r.rolling) {
                r.max_at = r.samples - 1;
            }
        }
        r.accumulated += to_accum_type(r.rolling);
        r.average = calc_avg(r.accumulated, r.samples - Smooth + 1);
        return r.rolling;
    }

    void set(T value)
    {
        if (range) {
            if (value < range->min) {
                range->min = value;
                range->max = range->min;
            } else {
                range->max = value;
            }
        } else {
            range = RangeData { .min = value, .max = value };
        }
    }

    [[nodiscard]] bool is_set() const
    {
        return range;
    }

    void clear()
    {
        range.reset();
    }

    [[nodiscard]] std::optional<T> diff() const
    {
        if (range) {
            return static_cast<T>(range->max - range->min);
        };
        return { };
    }

    [[nodiscard]] T min() const
    {
        assert(range.has_value());
        return range->min;
    }

    [[nodiscard]] T max() const
    {
        assert(range.has_value());
        return range->max;
    }
};

template<Number T>
using Range = SmoothedRange<T, 1>;

template<Number T, size_t Smooth>
    requires(Smooth > 0)
struct NormalizedRange {
    struct RangeData {
        float32               accumulated { 0 };
        std::array<T, Smooth> window { };
        size_t                samples { 0 };
        T                     normalized { 0 };
    };

    std::optional<RangeData> range { };

    void extend(T value)
    {
        if (!range) {
            range = RangeData { .samples = 1 };
            range->window[0] = value;
            return;
        }
        auto &r = *range;
        r.window[r.samples % Smooth] = value;
        r.samples += 1;
        if (r.samples > Smooth) {
            float32 rolling { 0 };
            for (auto const &v : r.window) {
                rolling += static_cast<float32>(v);
            }
            r.accumulated += std::pow(rolling / static_cast<float32>(Smooth), 4);
            r.normalized = static_cast<T>(
                std::trunc(
                    std::sqrt(
                        std::sqrt(r.accumulated / static_cast<float32>(r.samples - Smooth)))));
        }
    }
};

}
