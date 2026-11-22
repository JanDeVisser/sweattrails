/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Uploads an activity's FIT file to Strava: see specs/66-strava-upload.md.
 * Strava's API can't delete or replace an activity, so a fixed file goes
 * up only once the user has deleted the old one on strava.com.
 */

#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <Error.h>

#include <storage/Activity.h>
#include <storage/Strava.h>

namespace ST {

namespace fs = std::filesystem;

// What to upload: the recording on display, as it is in the store.
struct UploadRequest {
    fs::path                   fit { };
    fs::path                   sidecar { }; // The recording's
    std::string                name { };    // The activity's title, as SweatTrails shows it
    std::optional<std::string> description { };
    std::vector<fs::path>      others { }; // The sidecars of the activity's other recordings
};

// How long to wait for Strava to process an upload: `polls` times,
// `pause` apart. Without `wait`, nothing waits, as in the tests.
struct UploadPacing {
    int                                       polls { 60 };
    std::chrono::seconds                      pause { 1 };
    std::function<void(std::chrono::seconds)> wait { };
};

struct UploadOutcome {
    enum class Kind {
        Uploaded,        // As `activity_id`, a new activity
        StillOnStrava,   // The sidecar's activity, `activity_id`, is still there: nothing was sent
        Duplicate,       // Strava has the file already, as `activity_id`
        Rejected,        // Strava didn't take the file: `message` is why
        StillProcessing, // Strava wasn't done in time: the activity comes later
    };

    Kind        kind { Kind::Uploaded };
    u64         activity_id { 0 };
    std::string message { }; // For Rejected, Strava's error; for Uploaded, a sidecar that couldn't be saved
};

// Checks that Strava no longer has the activity in the sidecar, uploads
// the file, waits for Strava to process it, and saves the new activity
// in the sidecars. Only an upload changes the sidecars. Runs on the job
// thread.
StravaResult<UploadOutcome> upload_to_strava(Strava &strava, UploadRequest const &request, UploadPacing const &pacing);

// Saves `uploaded` as the Strava activity of `request.sidecar`, and of
// each of `request.others` whose Strava activity was `old_id`, keeping
// the rest of each sidecar.
std::expected<void, LibCError> save_uploaded(UploadRequest const &request, std::optional<u64> old_id, StravaTitle const &uploaded);

}
