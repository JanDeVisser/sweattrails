/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <map>

#include <storage/Activity.h>
#include <storage/StravaBacklog.h>

namespace ST {

namespace {

constexpr long NotFound = 404;

// Saves `description` in the sidecars of `undescribed`. The sidecar is
// read again first: a run of Strava's titles may have given it another
// name since the backlog was read, or another ID, and those stay.
void save_description(Undescribed const &undescribed, std::string const &description, BacklogChunk &chunk)
{
    for (auto const &stored : undescribed.stored) {
        auto title = sidecar_titles(stored.sidecar).strava;
        if (!title || title->id != undescribed.id || title->description) {
            continue;
        }
        title->description = description;
        if (auto const saved = save_sidecar_strava(stored.sidecar, *title); !saved) {
            warning(Strava, "Could not save Strava's description in `{}`: {}", stored.sidecar.string(), saved.error().to_string());
            continue;
        }
        chunk.changed.push_back(stored.start);
    }
}

}

StorageResult<std::vector<Undescribed>> strava_backlog(Storage const &storage)
{
    std::map<u64, Undescribed> by_id;
    for (auto const &month : TRY_EVAL(storage.months_with_activities())) {
        for (auto const &stored : TRY_EVAL(storage.month_starts(month))) {
            auto const title = sidecar_titles(stored.sidecar).strava;
            if (!title || title->description) {
                continue;
            }
            auto &undescribed = by_id[title->id];
            undescribed.id = title->id;
            undescribed.stored.push_back(stored);
        }
    }
    std::vector<Undescribed> ret;
    for (auto &[id, undescribed] : by_id) {
        std::ranges::sort(undescribed.stored, std::ranges::greater { }, &StoredStart::start);
        ret.push_back(std::move(undescribed));
    }
    // The newest first: those are the ones the user looks at, and with
    // Strava's limits a long history's descriptions take days.
    std::ranges::sort(ret, std::ranges::greater { }, [](Undescribed const &u) { return u.stored.front().start; });
    return ret;
}

std::optional<i64> backlog_resume_at(std::optional<StravaUsage> const &usage, i64 now)
{
    if (!usage) {
        return { };
    }
    if (usage->day_used >= usage->day_limit - StravaSpareRequests) {
        return next_strava_day(now);
    }
    if (usage->quarter_used >= usage->quarter_limit - StravaSpareRequests) {
        return next_quarter_hour(now);
    }
    return { };
}

BacklogChunk fetch_backlog(Strava &strava, std::vector<Undescribed> &waiting, size_t count)
{
    BacklogChunk chunk;
    size_t       done { 0 };
    for (; done < std::min(count, waiting.size()); ++done) {
        if (chunk.resume_at = backlog_resume_at(strava.usage, strava.now()); chunk.resume_at) {
            break;
        }
        auto const &undescribed = waiting[done];
        auto const  description = strava.description(undescribed.id);
        if (!description && description.error().status == NotFound) {
            warning(Strava, "Strava doesn't have activity {} any more: it's left without a description", undescribed.id);
            save_description(undescribed, "", chunk);
            continue;
        }
        if (!description) {
            auto const &error = description.error();
            switch (error.kind) {
            case StravaError::Kind::RateLimited:
                chunk.resume_at = error.resets_at;
                break;
            case StravaError::Kind::NotLoggedIn:
                chunk.stopped = true;
                break;
            case StravaError::Kind::Failed:
                warning(Strava, "Strava's descriptions stopped: {}", error.to_string());
                chunk.resume_at = next_quarter_hour(strava.now());
                break;
            }
            break;
        }
        save_description(undescribed, *description, chunk);
        ++chunk.fetched;
    }
    waiting.erase(waiting.begin(), waiting.begin() + static_cast<std::ptrdiff_t>(done));
    std::ranges::sort(chunk.changed);
    return chunk;
}

}
