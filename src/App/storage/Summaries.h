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
#include <map>
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
    // What the training stress is made of, which doesn't depend on the
    // thresholds: power's NP, and Σ HR² × seconds over the moving records.
    std::optional<u16>     np { };      // W
    std::optional<float64> hr_load { }; // bpm² s
    // The best average power for each of PowerDurations it lasts: empty
    // without power.
    std::vector<u16> power { }; // W
    // A run's best time for each of PaceDistances it covered: empty for
    // anything else.
    std::vector<u32> best_times { }; // s
    // A leg of a multisport activity is summarized as an activity of its
    // own: its index among the activity's legs, and its name, "Bike".
    std::optional<u32> leg { };
    std::string        leg_name { };

    bool operator==(ActivitySummary const &) const = default;
};

using ActivitySummaries = std::vector<ActivitySummary>;

constexpr int SummariesVersion = 5;
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
// drop out. All of a multisport file's legs' summaries stay or go.
[[nodiscard]] SummaryWork plan_update(ActivitySummaries const &summaries, std::vector<SummaryFile> const &files);

// The store's FIT files, by start time, with their sizes and times.
StorageResult<std::vector<SummaryFile>> summary_files(Storage const &storage);
// One of them.
StorageResult<SummaryFile> summary_file(Storage const &storage, fs::path const &fit);

// Reads a FIT file's summary, with its records, for the NP and the heart
// rate: one, or one per leg of a multisport file. A file that can't be
// read gets one without a sport.
[[nodiscard]] ActivitySummaries read_file_summaries(Storage &storage, SummaryFile const &file);

// The activity in `file`, relative to the activity directory,
// "2025/03/1741000000.fit": none if the file isn't in a month's directory.
[[nodiscard]] std::optional<ActivityID> file_activity(std::string_view file);
// The activity a summary is of, from its file.
[[nodiscard]] std::optional<ActivityID> summary_activity(ActivitySummary const &summary);

// The number of activities each month lists, for the year and month lists
// (specs/60-year-month-navigation.md). Only months with any.
using MonthCounts = std::map<Month, size_t>;

// The activities of each month, from the summaries, leaving out files that
// can't be read and counting a multisport file once, as the activity list
// does. And from `files`, the store's FIT files as summary_files() names
// them, those without a summary yet: they most likely load. Listing the
// months to count would read every FIT file in the store.
[[nodiscard]] MonthCounts month_counts(ActivitySummaries const &summaries, std::vector<std::string> const &files);
// The same, with the store's FIT files from its directories: none is read.
StorageResult<MonthCounts> month_counts(Storage const &storage, ActivitySummaries const &summaries);

// The year before (`step` -1) or after (+1) `year` with activities. None
// at either end.
[[nodiscard]] std::optional<size_t> year_next_to(MonthCounts const &counts, size_t year, int step);
// The month to select in the month list of `year`: `shown`'s, if it's in
// that year and has activities, else the year's latest. None for a year
// without activities.
[[nodiscard]] std::optional<Month> month_to_select(MonthCounts const &counts, size_t year, std::optional<Month> const &shown);

// The status bar's summary of the store: "1,234 activities, the newest on
// Thu 12 Nov 2026", or "No activities yet". It counts files, not legs.
// Files that can't be read aren't counted, as the activity list leaves
// them out.
[[nodiscard]] std::string store_summary(ActivitySummaries const &summaries);

// Adds `summaries` to `into`, keeping it in order of start time.
void merge_summaries(ActivitySummaries &into, ActivitySummaries const &summaries);

// What a chunk of an update did.
struct SummaryChunkDone {
    ActivitySummaries summaries { }; // All of them, as written
    size_t            read { 0 };    // Files read in this chunk
    size_t            left { 0 };    // Files still to read
    std::string       error { };     // Why the update can't go on, if it can't
};

// Reads the next `chunk` files that are new or changed since the summary
// file was written, and writes it. Each chunk plans afresh, so that files
// the inbox filed since the chunk before are read too.
[[nodiscard]] SummaryChunkDone update_summaries(Storage &storage, size_t chunk = SummaryChunk);

}
