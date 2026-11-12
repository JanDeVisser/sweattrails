/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Fetches the descriptions of the store's activities that have a Strava
 * title but no description yet, in the background, as Strava's limits
 * allow: see specs/44-rate-limits.md.
 */

#pragma once

#include <optional>
#include <vector>

#include <storage/Storage.h>
#include <storage/Strava.h>

namespace ST {

// How many requests of each of Strava's limits the background leaves for
// what the user asks for, such as Ctrl+G.
constexpr i64 StravaSpareRequests = 20;

// A description to fetch: Strava's ID, and the activities in the store
// with it. The same ride recorded on two devices takes one request.
struct Undescribed {
    u64                      id { 0 };
    std::vector<StoredStart> stored { };
};

// The store's activities with a Strava title and no description, newest
// first.
StorageResult<std::vector<Undescribed>> strava_backlog(Storage const &storage);

// When the background may ask Strava again, by what `usage` says is left:
// now, which is nothing, at the next quarter hour, or the next day.
std::optional<i64> backlog_resume_at(std::optional<StravaUsage> const &usage, i64 now);

// What a chunk of the background did.
struct BacklogChunk {
    size_t             fetched { 0 };     // Descriptions, by Strava ID
    std::optional<i64> resume_at { };     // Stopped for Strava's limit, or a failure: when to go on
    bool               stopped { false }; // Not logged in: nothing to do until a login
    std::vector<i64>   changed { };       // The activities given a description, by start time
};

// Fetches the descriptions of up to `count` of `waiting`, from the front,
// and takes off the ones it's done with. Stops early when
// backlog_resume_at() says so, or at an error: a limit, a failure, or the
// login. An activity Strava doesn't have any more (404) is given an empty
// description, so that it isn't asked for again. Runs on the job thread.
BacklogChunk fetch_backlog(Strava &strava, std::vector<Undescribed> &waiting, size_t count);

}
