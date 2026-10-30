/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The summary file: what the volume totals need of each activity in the
 * store, kept in `summaries.json` so that the store's FIT files are read
 * once, not each time the totals are shown.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Error.h>

#include <fit/Profile.h>
#include <storage/Types.h>

namespace ST {

namespace fs = std::filesystem;

struct Storage;

// A FIT file in the store, as it is now: a changed size or time means it
// has to be read again.
struct SummaryFile {
    std::string file { }; // Relative to the activity directory: "2025/03/1741000000.fit"
    u64         size { 0 };
    i64         modified { 0 }; // Seconds, only compared

    bool operator==(SummaryFile const &) const = default;
};

struct ActivitySummary {
    SummaryFile source { };
    i64         start { 0 }; // A Unix time
    // None for a file that can't be read, which is kept so that it isn't
    // read again until it changes.
    std::optional<FIT::sport>     sport { };
    std::optional<FIT::sub_sport> sub_sport { };
    float64                       distance { 0 }; // m
    u32                           moving { 0 };   // s
    u32                           ascent { 0 };   // m

    bool operator==(ActivitySummary const &) const = default;
};

using ActivitySummaries = std::vector<ActivitySummary>;

constexpr int SummariesVersion = 1;
// Read at a time by the job, which other jobs can run between.
constexpr size_t SummaryChunk = 50;

[[nodiscard]] fs::path summaries_path(Storage const &storage);

// The summaries in the file, earliest first. None if there's no file, or
// it can't be read, or it's of another version: it's built again.
[[nodiscard]] ActivitySummaries read_summaries(fs::path const &path);
std::expected<void, LibCError>  write_summaries(fs::path const &path, ActivitySummaries const &summaries);

// What an update of the summaries has to do.
struct SummaryWork {
    ActivitySummaries        kept { };    // Their files haven't changed
    std::vector<SummaryFile> to_read { }; // New, or changed
};

// Given the summaries and the FIT files there are now, which summaries
// stay and which files are to be read. Summaries of files that are gone
// drop out.
[[nodiscard]] SummaryWork plan_update(ActivitySummaries const &summaries, std::vector<SummaryFile> const &files);

// The store's FIT files, by start time, with their sizes and times.
StorageResult<std::vector<SummaryFile>> summary_files(Storage const &storage);

// Reads a FIT file's summary, shallowly. A file that can't be read gets
// one without a sport.
[[nodiscard]] ActivitySummary read_summary(Storage &storage, SummaryFile const &file);

// Adds `summaries` to `into`, keeping it in order of start time.
void merge_summaries(ActivitySummaries &into, ActivitySummaries const &summaries);

}
