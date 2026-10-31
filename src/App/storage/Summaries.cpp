/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <chrono>
#include <limits>
#include <map>

#include <IO.h>
#include <JSON.h>
#include <Logging.h>
#include <StringUtil.h>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <storage/Summaries.h>

namespace ST {

namespace {

JSONValue encode(ActivitySummary const &summary)
{
    auto json = JSONValue::object();
    json.set("file", JSONValue { summary.source.file });
    json.set("size", JSONValue { summary.source.size });
    json.set("modified", JSONValue { summary.source.modified });
    json.set("start", JSONValue { summary.start });
    if (summary.sport) {
        json.set("sport", JSONValue { static_cast<int>(*summary.sport) });
    }
    if (summary.sub_sport) {
        json.set("sub_sport", JSONValue { static_cast<int>(*summary.sub_sport) });
    }
    json.set("distance", JSONValue { summary.distance });
    json.set("moving", JSONValue { summary.moving });
    json.set("ascent", JSONValue { summary.ascent });
    if (summary.np) {
        json.set("np", JSONValue { *summary.np });
    }
    if (summary.hr_load) {
        json.set("hr_load", JSONValue { *summary.hr_load });
    }
    return json;
}

Decoded<ActivitySummary> decode(JSONValue const &json)
{
    ActivitySummary ret {
        .source = SummaryFile {
            .file = TRY_EVAL(json.try_get<std::string>("file")),
            .size = TRY_EVAL(json.try_get<u64>("size")),
            .modified = TRY_EVAL(json.try_get<i64>("modified")),
        },
        .start = TRY_EVAL(json.try_get<i64>("start")),
        .distance = TRY_EVAL(json.try_get<float64>("distance")),
        .moving = TRY_EVAL(json.try_get<u32>("moving")),
        .ascent = TRY_EVAL(json.try_get<u32>("ascent")),
    };
    // FIT's own numbers for them, as there are no names to read back.
    if (json.has("sport")) {
        ret.sport = static_cast<FIT::sport>(TRY_EVAL(json.try_get<int>("sport")));
    }
    if (json.has("sub_sport")) {
        ret.sub_sport = static_cast<FIT::sub_sport>(TRY_EVAL(json.try_get<int>("sub_sport")));
    }
    if (json.has("np")) {
        ret.np = TRY_EVAL(json.try_get<u16>("np"));
    }
    if (json.has("hr_load")) {
        ret.hr_load = TRY_EVAL(json.try_get<float64>("hr_load"));
    }
    return ret;
}

}

fs::path summaries_path(Storage const &storage)
{
    return storage.data_dir / "summaries.json";
}

ActivitySummaries read_summaries(fs::path const &path)
{
    std::error_code ec { };
    if (!fs::exists(path, ec)) {
        return { };
    }
    // Not JSONValue::read_file(), which ends the app on a syntax error.
    auto const contents = read_file_by_name(path.string());
    if (!contents) {
        warning(Storage, "Building the summaries again: `{}` can't be read: {}", path.string(), contents.error().to_string());
        return { };
    }
    auto const json = JSONValue::deserialize(*contents);
    if (!json || !json->is_object()) {
        warning(Storage, "Building the summaries again: `{}` can't be read", path.string());
        return { };
    }
    if (auto const version = json->try_get<int>("version"); !version || *version != SummariesVersion) {
        info(Storage, "Building the summaries again: `{}` is of another version", path.string());
        return { };
    }
    auto const activities = json->get("activities");
    if (!activities || !activities->is_array()) {
        return { };
    }
    ActivitySummaries ret;
    for (auto ix = 0u; ix < activities->size(); ++ix) {
        auto summary = decode((*activities)[ix]);
        if (!summary) {
            warning(Storage, "Building the summaries again: `{}` has a bad activity: {}", path.string(), summary.error().to_string());
            return { };
        }
        ret.push_back(std::move(*summary));
    }
    std::ranges::sort(ret, { }, &ActivitySummary::start);
    return ret;
}

std::expected<void, LibCError> write_summaries(fs::path const &path, ActivitySummaries const &summaries)
{
    auto json = JSONValue::object();
    json.set("version", JSONValue { SummariesVersion });
    auto activities = JSONValue::array();
    for (auto const &summary : summaries) {
        activities.append(encode(summary));
    }
    json.set("activities", activities);
    return write_file_atomically(path, json.serialize());
}

SummaryWork plan_update(ActivitySummaries const &summaries, std::vector<SummaryFile> const &files)
{
    std::map<std::string, ActivitySummary const *> by_file;
    for (auto const &summary : summaries) {
        by_file[summary.source.file] = &summary;
    }
    SummaryWork ret;
    for (auto const &file : files) {
        if (auto const known = by_file.find(file.file); known != by_file.end() && known->second->source == file) {
            ret.kept.push_back(*known->second);
        } else {
            ret.to_read.push_back(file);
        }
    }
    return ret;
}

StorageResult<std::vector<SummaryFile>> summary_files(Storage const &storage)
{
    std::vector<SummaryFile> ret;
    for (auto const &stored : TRY_EVAL(storage.activity_starts(0, std::numeric_limits<i32>::max()))) {
        auto fit = stored.sidecar;
        fit.replace_extension(".fit");
        std::error_code ec { };
        auto const      size = fs::file_size(fit, ec);
        if (ec) {
            return std::unexpected(LibCError { ec });
        }
        auto const modified = fs::last_write_time(fit, ec);
        if (ec) {
            return std::unexpected(LibCError { ec });
        }
        ret.push_back(SummaryFile {
            .file = fs::relative(fit, storage.activity_dir).generic_string(),
            .size = size,
            .modified = std::chrono::duration_cast<std::chrono::seconds>(modified.time_since_epoch()).count(),
        });
    }
    return ret;
}

ActivitySummary read_summary(Storage &storage, SummaryFile const &file)
{
    auto const path = storage.activity_dir / file.file;
    Activity   activity {
        storage,
        ActivityID {
            Month { 2020, DateTime::Month::January }, // Not used: the file is loaded by its path
            path.stem().string(),
            FileTypes::one(ActivityFile::Fit),
        },
    };
    activity.depth = LoadingDepth::Deep;
    ActivitySummary ret { .source = file };
    if (auto const start = string_to_integer<i64>(path.stem().string()); start) {
        ret.start = *start;
    }
    auto const loaded = activity.load_from_file(path);
    if (!loaded || !*loaded) {
        warning(Storage, "No summary of `{}`: {}", path.string(), loaded ? "it isn't an activity" : loaded.error().to_string());
        return ret;
    }
    ret.start = activity.segment.start_time.timestamp;
    ret.sport = activity.sport;
    ret.sub_sport = activity.sub_sport;
    ret.distance = activity.segment.distance;
    ret.moving = activity.segment.moving.elapsed;
    ret.ascent = activity.segment.total_ascent.value_or(0);
    if (auto const &np = activity.segment.computed_normalized_range.range; np && np->normalized > 0) {
        ret.np = np->normalized;
    }
    ret.hr_load = activity.segment.hr_load;
    return ret;
}

std::optional<ActivityID> summary_activity(ActivitySummary const &summary)
{
    auto const file = fs::path { summary.source.file };
    auto const month_dir = file.parent_path();
    auto const year = string_to_integer<size_t>(month_dir.parent_path().string(), 10);
    auto const month = string_to_integer<int>(month_dir.filename().string(), 10);
    if (!year || !month || *month < 1 || *month > 12 || month_dir.parent_path().has_parent_path()) {
        return { };
    }
    return ActivityID {
        Month { *year, static_cast<DateTime::Month>(*month - 1) },
        file.stem().string(),
        FileTypes::one(ActivityFile::Fit),
    };
}

void merge_summaries(ActivitySummaries &into, ActivitySummaries const &summaries)
{
    into.append_range(summaries);
    std::ranges::stable_sort(into, { }, &ActivitySummary::start);
}

SummaryChunkDone update_summaries(Storage &storage, size_t chunk)
{
    auto const path = summaries_path(storage);
    auto const summaries = read_summaries(path);
    auto const files = summary_files(storage);
    if (!files) {
        return SummaryChunkDone { .summaries = summaries, .error = std::format("Couldn't list the activities for the totals: {}", files.error().to_string()) };
    }
    auto              work = plan_update(summaries, *files);
    auto const        count = std::min(chunk, work.to_read.size());
    ActivitySummaries read;
    for (auto ix = 0uz; ix < count; ++ix) {
        read.push_back(read_summary(storage, work.to_read[ix]));
    }
    merge_summaries(work.kept, read);
    SummaryChunkDone ret { .read = count, .left = work.to_read.size() - count };
    if (work.kept != summaries) {
        if (auto const written = write_summaries(path, work.kept); !written) {
            // The next chunk would read the same files again.
            ret.error = std::format("Couldn't save the totals: {}", written.error().to_string());
        }
    }
    ret.summaries = std::move(work.kept);
    return ret;
}

}
