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
    StoredStart    stored { }; // The activity in the store
    StravaActivity strava { };
    // Of a multisport activity, the leg it's of, by its index among the
    // activity's legs, and whether that's a transition.
    std::optional<size_t> leg { };
    bool                  transition { false };
};

// For each of `starts`, activities in the store, the Strava activity
// nearest it, if one is within StravaMatchSeconds. Two
// activities in the store can match the same Strava activity: the same ride
// recorded on two devices.
std::vector<StravaMatch> match_strava(std::span<StravaActivity const> strava, std::span<StoredStart const> starts);

// A leg of a multisport activity in the store, in Unix times. A Strava
// activity that starts within it is its: from StravaMatchSeconds before
// its start up to its end.
struct LegSpan {
    i64  start { 0 };
    i64  end { 0 }; // The next leg's start, or the last leg's end
    bool transition { false };
};

// An activity in the store, and its legs if it's a multisport activity.
struct StoredLegs {
    StoredStart          stored { };
    std::vector<LegSpan> legs { };
};

// For each leg of a multisport activity in the store, the Strava activity
// that starts within it. One that starts within two legs, as it can in the
// minute before a leg's start, is the one's with the nearest start; then
// each leg takes the nearest of its own. Strava's start isn't always the
// leg's: see specs/58-multisport-leg-titles.md.
std::vector<StravaMatch> match_legs(std::span<StravaActivity const> strava, StoredLegs const &stored);

// The legs of each of `starts`, from the summary file, or from its FIT
// file for one the summary file doesn't have as it is now. None for an
// activity with one session, or whose file can't be read.
std::vector<StoredLegs> stored_legs(Storage &storage, std::span<StoredStart const> starts);

struct StravaUpdate {
    size_t           titles { 0 }; // Activities given a new name from Strava
    std::string      error { };    // Why the list stopped short, if it did
    std::vector<i64> changed { };  // The activities whose title changed, by start time
};

// Gives the activities in the store that started between `after` and
// `before`, Unix times, the names of the Strava activities they match, and
// the legs of a multisport activity theirs. A new name drops the
// description that went with the old one, for the background to fetch
// again; a transition's is never fetched, and is saved empty. A list that
// stops short keeps the titles it has. Runs on the job thread.
StravaResult<StravaUpdate> update_strava_titles(Strava &strava, Storage &storage, i64 after, i64 before);

}
