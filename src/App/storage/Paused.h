/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The track a Wahoo logged while it was paused, where the activity has no
 * records, to draw on the map. See specs/64-wahoo-paused-track.md.
 */

#pragma once

#include <span>
#include <vector>

#include <Coordinates.h>
#include <fit/Profile.h>

namespace ST {

// The paused track's positions between records[before] and
// records[before + 1].
struct PausedTrack {
    size_t                   before { 0 };
    std::vector<Coordinates> positions { };
};

// The paused track in each pause of `records`: two records more than
// RangeMovingGap apart. A filled gap has records again, close together,
// so it's left out.
[[nodiscard]] std::vector<PausedTrack> paused_tracks(std::span<record const> records, std::span<record const> paused);

}
