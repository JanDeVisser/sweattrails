/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>

#include <Date.h>
#include <FileIO.h>

#include <storage/StravaUpload.h>

namespace ST {

namespace {

std::optional<u64> strava_id(fs::path const &sidecar)
{
    return sidecar_titles(sidecar).strava.transform([](StravaTitle const &title) { return title.id; });
}

// What Strava's error says, for an upload it didn't take.
UploadOutcome not_taken(std::string const &error)
{
    if (auto const duplicate = duplicate_of(error); duplicate) {
        return UploadOutcome { .kind = UploadOutcome::Kind::Duplicate, .activity_id = *duplicate };
    }
    return UploadOutcome { .kind = UploadOutcome::Kind::Rejected, .message = error };
}

}

StravaResult<UploadOutcome> upload_to_strava(Strava &strava, UploadRequest const &request, UploadPacing const &pacing)
{
    auto const old_id = strava_id(request.sidecar);
    if (old_id) {
        auto const there = TRY_EVAL(strava.has_activity(*old_id));
        if (there) {
            return UploadOutcome { .kind = UploadOutcome::Kind::StillOnStrava, .activity_id = *old_id };
        }
    }
    auto const fit = read_file_by_name<char>(request.fit.string());
    if (!fit) {
        return std::unexpected(StravaError { .message = std::format("Could not read `{}`: {}", request.fit.string(), fit.error().to_string()) });
    }
    auto upload = TRY_EVAL(strava.upload(*fit, request.fit.filename().string(), request.name, request.description));
    for (auto poll = 0; !upload.activity_id && upload.error.empty(); ++poll) {
        if (poll == pacing.polls) {
            return UploadOutcome { .kind = UploadOutcome::Kind::StillProcessing };
        }
        if (pacing.wait) {
            pacing.wait(pacing.pause);
        }
        upload = TRY_EVAL(strava.upload_status(upload.id));
    }
    if (!upload.error.empty()) {
        return not_taken(upload.error);
    }
    UploadOutcome ret { .kind = UploadOutcome::Kind::Uploaded, .activity_id = *upload.activity_id };
    auto const    uploaded = StravaTitle { .id = *upload.activity_id, .name = request.name, .description = request.description };
    if (auto const saved = save_uploaded(request, old_id, uploaded); !saved) {
        warning(Strava, "Could not save Strava's new activity {}: {}", uploaded.id, saved.error().to_string());
        ret.message = saved.error().to_string();
    }
    return ret;
}

UploadReport upload_report(StravaResult<UploadOutcome> const &outcome)
{
    if (!outcome) {
        auto const &error = outcome.error();
        switch (error.kind) {
        case StravaError::Kind::NotLoggedIn:
            return UploadReport { .text = error.to_string(), .question = "Log in to Strava now?", .log_in = true };
        case StravaError::Kind::Forbidden:
            return UploadReport { .text = error.to_string(), .question = "Log in to Strava again to upload?", .log_in = true };
        case StravaError::Kind::RateLimited:
            return UploadReport { .text = std::format("Strava: its limit is reached for now. Try again after {}.", clock_time(error.resets_at)) };
        case StravaError::Kind::Failed:
            return UploadReport { .text = std::format("Couldn't upload to Strava: {}", error.to_string()) };
        }
    }
    switch (outcome->kind) {
    case UploadOutcome::Kind::Uploaded:
        if (!outcome->message.empty()) {
            return UploadReport { .text = std::format("Uploaded to Strava, but its new activity couldn't be saved with this one: {}", outcome->message) };
        }
        return UploadReport { .text = "Uploaded to Strava." };
    case UploadOutcome::Kind::StillOnStrava:
        return UploadReport { .text = "Strava still has this activity.", .question = "Open it to delete it?", .open = outcome->activity_id };
    case UploadOutcome::Kind::Duplicate:
        return UploadReport {
            .text = std::format("Strava already has this activity, as activity {}.", outcome->activity_id),
            .question = "Open it to delete it?",
            .open = outcome->activity_id,
        };
    case UploadOutcome::Kind::Rejected:
        return UploadReport { .text = std::format("Strava didn't take the file: {}", outcome->message) };
    case UploadOutcome::Kind::StillProcessing:
        return UploadReport { .text = "Strava is still processing the upload. The activity will show up on Strava once it's done." };
    }
    return { };
}

std::expected<void, LibCError> save_uploaded(UploadRequest const &request, std::optional<u64> old_id, StravaTitle const &uploaded)
{
    TRY(save_sidecar_strava(request.sidecar, uploaded));
    if (!old_id) {
        return { };
    }
    // A titles run gives a ride's recordings the same Strava activity:
    // none of them should keep the deleted one.
    for (auto const &other : request.others) {
        if (strava_id(other) == old_id) {
            TRY(save_sidecar_strava(other, uploaded));
        }
    }
    return { };
}

}
