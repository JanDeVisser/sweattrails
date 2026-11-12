/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Gives the store's activities their titles from Strava: see
 * specs/24-strava-titles.md. Their descriptions come in the background:
 * see StravaBacklog.h.
 */

#pragma once

#include <span>
#include <vector>

#include <storage/Storage.h>
#include <storage/Strava.h>

namespace ST {

// A Strava activity matches an activity in the store that starts at most
// this many seconds before or after it.
constexpr i64 StravaMatchSeconds = 60;

struct StravaMatch {
    StoredStart    stored { }; // The activity in the store
    StravaActivity strava { };
};

// For each of `starts`, activities in the store, the Strava activity
// nearest it, if one is within StravaMatchSeconds. Two
// activities in the store can match the same Strava activity: the same ride
// recorded on two devices.
std::vector<StravaMatch> match_strava(std::span<StravaActivity const> strava, std::span<StoredStart const> starts);

struct StravaUpdate {
    size_t           titles { 0 }; // Activities given a new name from Strava
    std::string      error { };    // Why the list stopped short, if it did
    std::vector<i64> changed { };  // The activities whose title changed, by start time
};

// Gives the activities in the store that started between `after` and
// `before`, Unix times, the names of the Strava activities they match. A
// new name drops the description that went with the old one, for the
// background to fetch again. A list that stops short keeps the titles it
// has. Runs on the job thread.
StravaResult<StravaUpdate> update_strava_titles(Strava &strava, Storage const &storage, i64 after, i64 before);

}
