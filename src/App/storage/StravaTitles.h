/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Gives the store's activities their titles and descriptions from Strava:
 * see specs/strava-titles.md.
 */

#pragma once

#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <storage/Storage.h>
#include <storage/Strava.h>

namespace ST {

// A Strava activity matches an activity in the store that starts at most
// this many seconds before or after it.
constexpr i64 StravaMatchSeconds = 60;

struct StravaMatch {
    i64            start { 0 }; // The activity in the store
    StravaActivity strava { };
};

// For each of `starts`, the start times of activities in the store, the
// Strava activity nearest it, if one is within StravaMatchSeconds. Two
// activities in the store can match the same Strava activity: the same ride
// recorded on two devices.
std::vector<StravaMatch> match_strava(std::span<StravaActivity const> strava, std::span<i64 const> starts);

struct StravaUpdate {
    size_t             titles { 0 };       // Activities given a new name from Strava
    size_t             descriptions { 0 }; // Descriptions fetched
    size_t             waiting { 0 };      // Descriptions still to fetch
    std::optional<i64> resets_at { };      // When Strava's limit resets, if it was reached
    std::vector<i64>   changed { };        // The activities whose title or notes changed, by start time
};

using StravaTitlesProgress = std::function<void(size_t index, size_t count)>;

// Gives the activities in the store that started between `after` and
// `before`, Unix times, the names of the Strava activities they match, and
// then fetches their descriptions, with `progress` called before each.
// Descriptions stop at Strava's limit, or at the first one that fails, and
// the rest are left for the next time. Runs on the job thread.
StravaResult<StravaUpdate> update_strava_titles(Strava &strava, Storage const &storage, i64 after, i64 before, StravaTitlesProgress const &progress = { });

}
