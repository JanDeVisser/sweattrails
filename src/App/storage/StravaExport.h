/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Fills in Strava's descriptions from the account export Strava makes on
 * request, instead of a request per description: see
 * specs/45-strava-export-import.md.
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

// The descriptions in the export's activities.csv, `csv`, by Strava's ID,
// or why it isn't Strava's list of activities. `file_name` is for the
// message.
std::expected<std::map<u64, std::string>, std::string> export_descriptions(std::string_view csv, std::string_view file_name);

struct ExportImport {
    size_t           descriptions { 0 }; // Saved
    size_t           empty { 0 };        // Of those, the empty ones
    size_t           untitled { 0 };     // Activities without a Strava title
    std::vector<i64> changed { };        // The activities saved, by start time

    // What to tell the user when the import is done.
    [[nodiscard]] std::string message() const;
};

using ExportProgress = std::function<void(size_t index, size_t count)>;

// Gives each activity in the store with a Strava title but no description
// the one in `descriptions` for its Strava ID, with `progress` called
// before each. Names, descriptions already there, and the user's own notes
// stay as they are. Runs on the job thread.
StorageResult<ExportImport> import_descriptions(Storage const &storage, std::map<u64, std::string> const &descriptions, ExportProgress const &progress = { });

}
