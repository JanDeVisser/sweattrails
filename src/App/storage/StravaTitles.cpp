/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstdlib>

#include <storage/StravaTitles.h>

namespace ST {

std::vector<StravaMatch> match_strava(std::span<StravaActivity const> strava, std::span<i64 const> starts)
{
    std::vector<StravaActivity> by_start { strava.begin(), strava.end() };
    std::ranges::sort(by_start, { }, &StravaActivity::start_time);
    std::vector<StravaMatch> ret;
    for (auto const start : starts) {
        // The nearest is the first that starts at or after `start`, or the
        // one before it.
        auto const after = std::ranges::lower_bound(by_start, start, { }, &StravaActivity::start_time);
        auto       nearest = after;
        if (after != by_start.begin()) {
            auto const before = std::prev(after);
            if (after == by_start.end() || start - before->start_time <= after->start_time - start) {
                nearest = before;
            }
        }
        if (nearest != by_start.end() && std::abs(nearest->start_time - start) <= StravaMatchSeconds) {
            ret.push_back(StravaMatch { .start = start, .strava = *nearest });
        }
    }
    return ret;
}

namespace {

struct Described {
    i64         start { 0 };
    StravaTitle title { };
};

// Writes the match's name into its sidecar, unless it's there already. The
// description stays only if the name does: a new name may come with a new
// description. Returns what the sidecar has now, or nothing if it can't be
// written.
std::optional<StravaTitle> save_title(Storage const &storage, StravaMatch const &match, StravaUpdate &update)
{
    auto const sidecar = storage.sidecar_of(match.start);
    auto const old = sidecar_titles(sidecar).strava;
    if (old && old->id == match.strava.id && old->name == match.strava.name) {
        return old;
    }
    StravaTitle title { .id = match.strava.id, .name = match.strava.name };
    if (old && old->name == match.strava.name) {
        title.description = old->description;
    }
    if (auto const saved = save_sidecar_strava(sidecar, title); !saved) {
        warning(Strava, "Could not save Strava's name in `{}`: {}", sidecar.string(), saved.error().to_string());
        return { };
    }
    ++update.titles;
    update.changed.push_back(match.start);
    return title;
}

}

StravaResult<StravaUpdate> update_strava_titles(Strava &strava, Storage const &storage, i64 after, i64 before, StravaTitlesProgress const &progress)
{
    auto const listed = TRY_EVAL(strava.list_activities(after, before));
    // A Strava activity near `after` or `before` can match one in the store
    // just outside of them.
    auto const starts = storage.activity_starts(after - StravaMatchSeconds, before + StravaMatchSeconds);
    if (!starts) {
        return std::unexpected(StravaError { .message = starts.error().to_string() });
    }

    StravaUpdate           update { };
    std::vector<Described> undescribed;
    for (auto const &match : match_strava(listed, *starts)) {
        if (auto const title = save_title(storage, match, update); title && !title->description) {
            undescribed.push_back(Described { .start = match.start, .title = *title });
        }
    }

    for (size_t ix = 0; ix < undescribed.size(); ++ix) {
        auto &[start, title] = undescribed[ix];
        if (progress) {
            progress(ix, undescribed.size());
        }
        auto const description = strava.description(title.id);
        if (!description) {
            update.waiting = undescribed.size() - ix;
            if (description.error().kind == StravaError::Kind::RateLimited) {
                update.resets_at = description.error().resets_at;
            } else {
                warning(Strava, "Could not fetch a description from Strava: {}", description.error().to_string());
            }
            break;
        }
        title.description = *description;
        auto const sidecar = storage.sidecar_of(start);
        if (auto const saved = save_sidecar_strava(sidecar, title); !saved) {
            warning(Strava, "Could not save Strava's description in `{}`: {}", sidecar.string(), saved.error().to_string());
            continue;
        }
        ++update.descriptions;
        update.changed.push_back(start);
    }

    std::ranges::sort(update.changed);
    auto const duplicates = std::ranges::unique(update.changed);
    update.changed.erase(duplicates.begin(), duplicates.end());
    return update;
}

}
