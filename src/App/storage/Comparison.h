/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Comparing two recordings of one activity, by the clock: their records
 * paired by time, and their power, cadence and heart rate over the seconds
 * both recorded. See specs/33-comparing-recordings.md.
 */

#pragma once

#include <optional>
#include <span>
#include <vector>

#include <fit/Profile.h>

namespace ST {

// A record's values hold until the next one: with smart recording, a
// device writes one every few seconds. The other's record this much
// earlier still pairs with one of the recording on display.
constexpr u32 PairHoldSeconds = 2;

// For each of `shown`'s records, the index of `other`'s record at its
// time: the one at the same second, or the last one before it, at most
// PairHoldSeconds earlier. None where the other has no record then. Both
// are in time order. The indices are into `records` and `recorded`
// alike, which have the same records.
[[nodiscard]] std::vector<std::optional<size_t>> pair_records(std::span<FIT::record const> shown, std::span<FIT::record const> other);

// A number of both recordings, where they have it.
struct Compared {
    std::optional<float64> shown { };
    std::optional<float64> other { };
    // Over seconds both recorded. Not if they never had the value at the
    // same time: then each is over its own, and there's no difference.
    bool together { true };

    // The one on display minus the other, and that as a percentage of the
    // other's. None unless both are there together, or for a percentage of
    // 0.
    [[nodiscard]] std::optional<float64> difference() const;
    [[nodiscard]] std::optional<float64> percent() const;
};

struct Comparison {
    Compared avg_power { };
    Compared max_power { };
    Compared np { };
    Compared avg_cadence { };
    Compared avg_hr { };
    Compared max_hr { };
};

// The comparison over `shown`'s records [first, end), as recorded, with
// `other`'s paired with them by `pairs`. Each number only counts the
// records where both have its value. Cadence leaves out the seconds where
// either one isn't pedaling. NP takes more than 30 of those seconds. A
// value they never both have is each one's own, where it has it, over the
// other's records paired with [first, end): the line then says "245 / –".
[[nodiscard]] Comparison compare(std::span<FIT::record const> shown, std::span<FIT::record const> other, std::span<std::optional<size_t> const> pairs, size_t first, size_t end);

}
