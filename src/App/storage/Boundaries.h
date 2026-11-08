/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Where a selection in the graph can be extended to: the next whole km, lap
 * boundary or stop, back from its start or on from its end
 * (specs/38-extend-selection.md).
 */

#pragma once

#include <optional>
#include <span>
#include <vector>

#include <storage/Activity.h>

namespace ST {

enum class Boundary {
    Km,
    Lap,
    Stop,
};

enum class Direction {
    Back,
    On,
};

// Where the activity stood still, between two records that moved. None
// for a stop at the activity's start or end.
struct Stop {
    std::optional<size_t> last_moving { };
    std::optional<size_t> first_moving { };

    bool operator==(Stop const &) const = default;
};

// The stops of the records as recorded, as the moving time sees them: a
// record slower than RangeMovingSpeed stands still, and a stop is where
// the moving time leaves out RangeMovingGap seconds or more between two
// records that move. That's a pause of more than RangeMovingGap that
// recorded nothing, or as long standing still, but not a slowdown of a
// few seconds, however far apart the records are. Records without a
// speed move, so only pauses stop them.
[[nodiscard]] std::vector<Stop> stops(std::span<record const> recorded);

// The record the selection's start moves back to, or its end on to, from
// record `from`, for the next km, lap or stop in that direction. One
// `from` is already on doesn't count. None when there's nothing to go to:
// no km, lap or stop left, no distances, or a single lap.
[[nodiscard]] std::optional<size_t> extend_to(Activity const &activity, size_t from, Boundary boundary, Direction direction);

}
