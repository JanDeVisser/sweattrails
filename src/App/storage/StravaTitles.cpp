/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstdlib>
#include <format>
#include <map>

#include <Date.h>

#include <storage/StravaTitles.h>
#include <storage/Summaries.h>

namespace ST {

std::vector<StravaMatch> match_strava(std::span<StravaActivity const> strava, std::span<StoredStart const> starts)
{
    std::vector<StravaActivity> by_start { strava.begin(), strava.end() };
    std::ranges::sort(by_start, { }, &StravaActivity::start_time);
    std::vector<StravaMatch> ret;
    for (auto const &stored : starts) {
        auto const start = stored.start;
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
            ret.push_back(StravaMatch { .stored = stored, .strava = *nearest });
        }
    }
    return ret;
}

std::vector<StravaMatch> match_legs(std::span<StravaActivity const> strava, StoredLegs const &stored)
{
    auto const &legs = stored.legs;
    auto const  from_start = [&legs](StravaActivity const &activity, size_t leg) {
        return std::abs(activity.start_time - legs[leg].start);
    };
    auto const within = [&legs](StravaActivity const &activity, size_t leg) {
        return activity.start_time >= legs[leg].start - StravaMatchSeconds && activity.start_time < legs[leg].end;
    };
    std::vector<std::optional<StravaActivity>> nearest(legs.size());
    for (auto const &activity : strava) {
        std::optional<size_t> leg;
        for (size_t ix = 0; ix < legs.size(); ++ix) {
            if (within(activity, ix) && (!leg || from_start(activity, ix) < from_start(activity, *leg))) {
                leg = ix;
            }
        }
        if (!leg) {
            continue;
        }
        // Halfway between two, the earlier one wins, as for an activity.
        auto &taken = nearest[*leg];
        if (!taken || from_start(activity, *leg) < from_start(*taken, *leg)
            || (from_start(activity, *leg) == from_start(*taken, *leg) && activity.start_time < taken->start_time)) {
            taken = activity;
        }
    }
    std::vector<StravaMatch> ret;
    for (size_t ix = 0; ix < legs.size(); ++ix) {
        if (nearest[ix]) {
            ret.push_back(StravaMatch { .stored = stored.stored, .strava = *nearest[ix], .leg = ix, .transition = legs[ix].transition });
        }
    }
    return ret;
}

namespace {

// The legs in a FIT file's summaries: none for one session. The last
// leg's end is its start and its moving time, as the summary has no
// elapsed time, and at least a minute after its start.
std::vector<LegSpan> leg_spans(ActivitySummaries summaries)
{
    if (summaries.size() < 2 || !summaries.front().leg) {
        return { };
    }
    std::ranges::sort(summaries, { }, [](ActivitySummary const &summary) { return summary.leg.value_or(0); });
    std::vector<LegSpan> ret;
    for (size_t ix = 0; ix < summaries.size(); ++ix) {
        auto const &leg = summaries[ix];
        ret.push_back(LegSpan {
            .start = leg.start,
            .end = (ix + 1 < summaries.size()) ? summaries[ix + 1].start : leg.start + std::max<i64>(leg.moving, StravaMatchSeconds),
            .transition = leg.sport == FIT::sport::Transition,
        });
    }
    return ret;
}

}

std::vector<StoredLegs> stored_legs(Storage &storage, std::span<StoredStart const> starts)
{
    std::map<std::string, ActivitySummaries> by_file;
    for (auto &summary : read_summaries(summaries_path(storage))) {
        by_file[summary.source.file].push_back(std::move(summary));
    }
    std::vector<StoredLegs> ret;
    for (auto const &stored : starts) {
        StoredLegs legs { .stored = stored };
        auto const file = summary_file(storage, fs::path { stored.sidecar }.replace_extension(".fit"));
        if (!file) {
            ret.push_back(std::move(legs));
            continue;
        }
        auto const found = by_file.find(file->file);
        // New since the summary file was written, or changed.
        if (found == by_file.end() || found->second.front().source != *file) {
            legs.legs = leg_spans(read_file_summaries(storage, *file));
        } else {
            legs.legs = leg_spans(found->second);
        }
        ret.push_back(std::move(legs));
    }
    return ret;
}

namespace {

// Writes the match's name into its sidecar, or its leg's, unless it's
// there already. The description stays only if the name does: a new name
// may come with a new description. A transition's is saved empty, so
// that it isn't fetched: it's almost always empty.
void save_title(StravaMatch const &match, StravaUpdate &update)
{
    auto const &sidecar = match.stored.sidecar;
    auto const  titles = sidecar_titles(sidecar);
    auto const  old = match.leg ? (*match.leg < titles.legs.size() ? titles.legs[*match.leg].strava : std::nullopt) : titles.strava;
    if (old && old->id == match.strava.id && old->name == match.strava.name) {
        return;
    }
    // The export import found Strava's file isn't this activity's.
    if (std::ranges::contains(titles.strava_rejected, match.strava.id)) {
        return;
    }
    StravaTitle title { .id = match.strava.id, .name = match.strava.name };
    if (old && old->name == match.strava.name) {
        title.description = old->description;
    }
    if (match.transition) {
        title.description = "";
    }
    if (auto const saved = save_sidecar_strava(sidecar, title, match.leg); !saved) {
        warning(Strava, "Could not save Strava's name in `{}`: {}", sidecar.string(), saved.error().to_string());
        return;
    }
    ++update.titles;
    update.changed.push_back(match.stored.start);
}

}

StravaResult<StravaUpdate> update_strava_titles(Strava &strava, Storage &storage, i64 after, i64 before)
{
    auto const listed = TRY_EVAL(strava.list_activities(after, before));
    // A Strava activity near `after` or `before` can match one in the store
    // just outside of them.
    auto const starts = storage.activity_starts(after - StravaMatchSeconds, before + StravaMatchSeconds);
    if (!starts) {
        return std::unexpected(StravaError { .message = starts.error().to_string() });
    }

    StravaUpdate             update { };
    std::vector<StoredStart> single;
    for (auto const &stored : stored_legs(storage, *starts)) {
        if (stored.legs.empty()) {
            single.push_back(stored.stored);
            continue;
        }
        for (auto const &match : match_legs(listed.activities, stored)) {
            save_title(match, update);
        }
    }
    for (auto const &match : match_strava(listed.activities, single)) {
        save_title(match, update);
    }
    if (listed.stopped && listed.stopped->kind == StravaError::Kind::RateLimited) {
        update.error = std::format("Strava's limit stopped its list short. Try again after {}", clock_time(listed.stopped->resets_at));
    } else if (listed.stopped) {
        update.error = std::format("Strava's list stopped short: {}", listed.stopped->to_string());
    }

    std::ranges::sort(update.changed);
    auto const duplicates = std::ranges::unique(update.changed);
    update.changed.erase(duplicates.begin(), duplicates.end());
    return update;
}

}
