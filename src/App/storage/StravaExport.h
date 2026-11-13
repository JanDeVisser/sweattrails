/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Fills in Strava's descriptions from the account export Strava makes on
 * request, instead of a request per description: see
 * specs/45-strava-export-import.md. And replaces the activities that are
 * only a summary with the original files in it: see
 * specs/50-stub-activities.md.
 */

#pragma once

#include <expected>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <storage/Storage.h>

namespace ST {

// Whether a file dropped on the window goes to the import: a CSV file,
// which the import then checks is Strava's.
bool is_csv_file(fs::path const &path);

// A line of the export's activities.csv.
struct ExportActivity {
    std::string description { };
    // The original file, relative to the CSV's directory, named by
    // Strava's upload ID: "activities/14666148179.fit.gz". Empty for an
    // activity entered by hand.
    std::string file { };
};

using ExportActivities = std::map<u64, ExportActivity>;

// The lines of the export's activities.csv, `csv`, by Strava's ID, or why
// it isn't Strava's list of activities. `file_name` is for the message.
// Without a Filename column, there are no files.
std::expected<ExportActivities, std::string> read_export(std::string_view csv, std::string_view file_name);

// What replacing the stubs with the export's files did.
struct StubImport {
    size_t                           replaced { 0 };
    size_t                           unreadable { 0 }; // Stubs whose file is GPX or TCX
    std::vector<std::pair<i64, i64>> moved { };        // The stubs' start times, and the files'
};

struct ExportImport {
    size_t           descriptions { 0 }; // Saved
    size_t           empty { 0 };        // Of those, the empty ones
    size_t           untitled { 0 };     // Activities without a Strava title
    std::vector<i64> changed { };        // The activities saved, by start time
    StubImport       stubs { };

    // What to tell the user when the import is done.
    [[nodiscard]] std::string message() const;
};

using ExportProgress = std::function<void(size_t index, size_t count)>;

// Gives each activity in the store with a Strava title but no description
// the one in `descriptions` for its Strava ID, with `progress` called
// before each. Names, descriptions already there, and the user's own notes
// stay as they are. Runs on the job thread.
StorageResult<ExportImport> import_descriptions(Storage const &storage, ExportActivities const &activities, ExportProgress const &progress = { });

// Replaces each activity in the store that is only a summary
// (storage/Stubs.h), and has a Strava ID, with the FIT file, gzipped or
// not, that the export's line for it names in `export_dir`, with
// `progress` called before each. Runs on the job thread.
StorageResult<StubImport> import_originals(Storage &storage, ExportActivities const &activities, fs::path const &export_dir, ExportProgress const &progress = { });

}
