/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/storage.zig. See storage/Types.h for the assumptions
 * made about the modules that have not been translated yet.
 *
 * The zig version carries an `io`, a gpa and an arena around; the C++ version
 * uses std::filesystem paths and owning containers instead, so those members
 * are gone. Directories are identified by path rather than by an open handle,
 * which also removes all the `defer dir.close(io)` bookkeeping.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <Error.h>
#include <Expected.h>

#include <storage/Activity.h>
// #include <storage/Rungap.h>
// #include <storage/Strava.h>
#include <storage/Types.h>

namespace ST {

namespace fs = std::filesystem;

struct SweatTrails;

struct Storage {
    using Index = ST::Index;
    using Month = ST::Month;
    using Year = ST::Year;
    using Years = ST::Years;
    using ActivityFile = ST::ActivityFile;
    using FileTypes = ST::FileTypes;
    using ActivityID = ST::ActivityID;
    using Activity = ST::Activity;
    using Activities = ST::Activities;

    fs::path data_dir { };
    fs::path inbox_dir { };
    fs::path activity_dir { };
    fs::path tiles_dir { };
    // std::optional<Strava> strava { };
    // std::optional<Rungap> rungap { };
    std::vector<Year>    years { };
    std::optional<Index> current { };

    static StorageResult<Storage> init();

    Error<StorageError>                      rescan();
    std::optional<Year>                      find_year(size_t year) const;
    StorageResult<Years>                     get_years();
    StorageResult<std::optional<Activities>> find_current_folder();
    StorageResult<std::optional<Activity>>   find_newest();
    StorageResult<Activities>                set_current_folder(Month const &month);
    StorageResult<std::optional<Activity>>   load_activity(ActivityID const &id);
    Error<StorageError>                      process_inbox(SweatTrails &app);
    Error<StorageError>                      organize_fit_file(SweatTrails &app, std::string_view const &file_name);
};

}
