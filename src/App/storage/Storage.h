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
#include <set>
#include <string_view>
#include <vector>

#include <Error.h>
#include <Expected.h>

#include <storage/Activity.h>
#include <storage/Thresholds.h>
#include <storage/Types.h>

namespace ST {

namespace fs = std::filesystem;

// An activity in the store, as activity_starts() finds it: its start time,
// a Unix time, and its sidecar, next to its FIT file.
struct StoredStart {
    i64      start { 0 };
    fs::path sidecar { };

    bool operator==(StoredStart const &) const = default;
};

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
        Replaced,  // Took the place of a stub: see storage/Stubs.h.
        Duplicate, // The store already has an activity with this start time.
        Skipped,   // Not an activity; left in the inbox.
    };

    fs::path data_dir { };
    fs::path inbox_dir { };
    fs::path activity_dir { };
    fs::path tiles_dir { };
    fs::path overlays_dir { }; // The user's overlay layouts
    // From the settings, set once before any job runs: the scores of the
    // activities, read from any thread, as the paths are.
    ThresholdList thresholds { };
    // From the settings, set once as the thresholds are: the devices whose
    // recording of an activity is its main one, the first first.
    std::vector<FIT::manufacturer> prefer { };

    // Threading: the paths above don't change after init(), and can be used
    // from any thread. `years` (and `current`) are rebuilt by rescan(), so
    // they and every member function that calls rescan() must only be used
    // from jobs, which run one at a time on the AppBus job thread. There is
    // no locking.
    std::vector<Year>    years { };
    std::optional<Index> current { };

    static StorageResult<Storage> init(std::string_view const &app_name);

    Error<StorageError> rescan();
    // The months with an activity file, oldest first. Reads the
    // directories, not `years`, so it can be used from any thread.
    StorageResult<std::vector<Month>> months_with_activities() const;
    // The first month from `from`, going later (`step` +1) or earlier (-1),
    // that lists activities, `from` itself too if `inclusive`. A month
    // with only files that don't list, such as a broken FIT file, is
    // passed over, as it would show as an empty list.
    StorageResult<std::optional<Activities>> listed_month(Month const &from, int step, bool inclusive);
    // The activity before (`step` -1) or after (+1) `id` by start time,
    // in another month if need be. None at the first or the last.
    StorageResult<std::optional<ActivityID>> activity_next_to(ActivityID const &id, int step);
    std::optional<Year>                      find_year(size_t year) const;
    StorageResult<Years>                     get_years();
    StorageResult<std::optional<Activities>> find_current_folder();
    StorageResult<std::optional<Activity>>   find_newest();
    StorageResult<Activities>                set_current_folder(Month const &month);
    StorageResult<std::optional<Activity>>   load_activity(ActivityID const &id);
    // What the inbox did with a file, and the start time it filed it by.
    struct InboxFiling {
        InboxOutcome outcome { InboxOutcome::Skipped };
        i64          start { 0 };
        i64          stub_start { 0 }; // For Replaced: the stub's

        // Whether the file is in the store now: organized, or replacing a
        // stub.
        [[nodiscard]] bool filed() const { return outcome == InboxOutcome::Organized || outcome == InboxOutcome::Replaced; }
    };
    // Called with the name of each inbox file after it's processed, and
    // what the inbox did with it, whatever that was. Not for a file that
    // couldn't be read.
    using InboxFiled = std::function<void(std::string_view file_name, InboxFiling const &filing)>;

    // Files what's in the inbox. The number filed: organized, or
    // replacing a stub.
    StorageResult<u32>         process_inbox(InboxProgress const &on_file = { }, InboxFiled const &on_filed = { });
    StorageResult<InboxFiling> organize_fit_file(std::string_view const &file_name);
    // Whether the store has an activity that starts at `start_time`, a Unix
    // time: the inbox files activities by their start time. Also one that's
    // a recording of another (specs/31-multiple-recordings.md), in the
    // folders. Both in its month and the months around it, as one filed in
    // another time zone can be.
    [[nodiscard]] bool has_activity(i64 start_time) const;
    // The FIT file has_activity() finds, if there is one.
    [[nodiscard]] std::optional<fs::path> fit_at(i64 start_time) const;
    // The sidecar of the activity that starts at `start_time`, as the inbox
    // would file it now, whether it's there or not. An activity filed in
    // another time zone may be in the month before or after: activity_starts()
    // finds it where it is.
    [[nodiscard]] fs::path sidecar_of(i64 start_time) const;
    // The activities that start from `from` to `to`, Unix times, earliest
    // first. From the FIT files' names: none is read, and one that isn't
    // named by its start time isn't there.
    StorageResult<std::vector<StoredStart>> activity_starts(i64 from, i64 to) const;
    // The start time of the store's oldest activity, as activity_starts()
    // sees them.
    StorageResult<std::optional<i64>> oldest_start() const;

    // The IDs of the activities from `source`, such as "garmin", that the
    // inbox set aside as duplicates: `<source>-<id>.fit`, or
    // `<source>-<id>-<n>.fit` for later copies. A download doesn't fetch
    // them again, as the store has them, or a stub that only a recording
    // replaces (storage/Stubs.h).
    [[nodiscard]] std::set<u64> set_aside_as_duplicates(std::string_view source) const;

    // The activities filed in `month`'s directory, in no order.
    StorageResult<std::vector<StoredStart>> month_starts(Month const &month) const;

    StorageResult<bool> replace_from_inbox(std::string_view const &file_name, StoredStart const &stub, i64 start) const;
    Error<StorageError> move_with_sidecar(std::string_view const &file_name, fs::path const &dest_dir, std::string_view const &dest_stem) const;
};

}
