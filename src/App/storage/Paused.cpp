/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <storage/Activity.h>
#include <storage/Paused.h>

namespace ST {

std::vector<PausedTrack> paused_tracks(std::span<record const> records, std::span<record const> paused)
{
    auto const               at = [](record const &r) { return static_cast<i64>(r.timestamp.timestamp); };
    std::vector<PausedTrack> ret;
    for (auto ix = 1uz; ix < records.size(); ++ix) {
        auto const stop = at(records[ix - 1]);
        auto const restart = at(records[ix]);
        if (restart - stop <= RangeMovingGap) {
            continue;
        }
        PausedTrack track { .before = ix - 1 };
        // The paused track is in time order, as the device wrote it.
        auto const first = std::ranges::upper_bound(paused, stop, { }, at);
        for (auto it = first; it != paused.end() && at(*it) < restart; ++it) {
            if (it->position && it->position->has_fix()) {
                track.positions.push_back(*it->position);
            }
        }
        if (!track.positions.empty()) {
            ret.push_back(std::move(track));
        }
    }
    return ret;
}

}
