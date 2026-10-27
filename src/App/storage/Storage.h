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
#include <functional>
#include <optional>
#include <vector>

#include <Error.h>
#include <Expected.h>

#include <storage/Activity.h>
// #include <storage/Strava.h>
#include <storage/Types.h>

namespace ST {

namespace fs = std::filesystem;

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
    // Called with the name of each inbox file before it's processed, its index
    // and the number of files.
    using InboxProgress = std::function<void(std::string_view const &file_name, size_t index, size_t count)>;

    enum class InboxOutcome {
        Organized, // Moved into the activity store.
        Duplicate, // The store already has an activity with this start time.
        Skipped,   // Not an activity; left in the inbox.
    };

    fs::path data_dir { };
    fs::path inbox_dir { };
    fs::path activity_dir { };
    fs::path tiles_dir { };
    fs::path overlays_dir { }; // The user's overlay layouts
    // std::optional<Strava> strava { };

    // Threading: the paths above don't change after init(), and can be used
    // from any thread. `years` (and `current`) are rebuilt by rescan(), so
    // they and every member function that calls rescan() must only be used
    // from jobs, which run one at a time on the AppBus job thread. There is
    // no locking.
    std::vector<Year>    years { };
    std::optional<Index> current { };

    static StorageResult<Storage> init(std::string_view const &app_name);

    Error<StorageError>                      rescan();
    std::optional<Year>                      find_year(size_t year) const;
    StorageResult<Years>                     get_years();
    StorageResult<std::optional<Activities>> find_current_folder();
    StorageResult<std::optional<Activity>>   find_newest();
    StorageResult<Activities>                set_current_folder(Month const &month);
    StorageResult<std::optional<Activity>>   load_activity(ActivityID const &id);
    StorageResult<u32>                       process_inbox(InboxProgress const &on_file = { });
    StorageResult<InboxOutcome>              organize_fit_file(std::string_view const &file_name);
    // Whether the store has an activity that starts at `start_time`, a Unix
    // time: the inbox files activities by their start time.
    [[nodiscard]] bool  has_activity(i64 start_time) const;
    Error<StorageError> move_with_sidecar(std::string_view const &file_name, fs::path const &dest_dir, std::string_view const &dest_stem) const;
};

}
