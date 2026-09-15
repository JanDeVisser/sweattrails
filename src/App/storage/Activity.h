/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/storage/activity.zig. See Types.h for the assumptions
 * made about the modules that have not been translated yet.
 */

#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <fit/FIT.h>
#include <fit/Profile.h>

#include <Date.h>
#include <Format.h>
#include <JSON.h>
#include <Map.h>
#include <Range.h>
#include <storage/Types.h>

namespace ST {

using namespace ST::FIT;

struct Storage;

constexpr float32 JANS_FTP = 275;

struct Segment {
    DateTime                 start_time { };
    DateTime                 end_time { };
    Duration                 elapsed { };
    Duration                 moving { };
    float32                  distance { 0 };
    float32                  computed_distance { 0 };
    Range<float32>           computed_speed_range { };
    SmoothedRange<u16, 3>    computed_power_range { };
    NormalizedRange<u16, 30> computed_normalized_range { };
    SmoothedRange<u8, 3>     computed_cadence_range { };
    SmoothedRange<u8, 3>     computed_hr_range { };
    Range<i16>               computed_elevation_range { };
    std::optional<float32>   intensity_factor { };
    std::optional<float32>   variability_index { };
    std::optional<float32>   tss { };
    std::optional<Box>       box { };

    void analyze(std::span<record> records, bool smooth);
};

struct Lap {
    // Points into Activity::records; only valid as long as that vector is not
    // resized (same aliasing contract as the zig slices).
    std::span<record> records { };
    std::string       title { };
    std::string       notes { };
    Segment           segment { };
};

struct Activity {
    Storage            *storage { nullptr };
    LoadingDepth        depth { LoadingDepth::None };
    ActivityID          id { };
    u32                 serial { 0 };
    u16                 number { 0 };
    ST::sport           sport { ST::sport::Running };
    std::string         title { };
    std::string         notes { };
    Segment             segment { };
    std::vector<record> records { };
    std::vector<Lap>    laps { };

    Activity(Storage &the_storage, ActivityID the_id)
        : storage(&the_storage)
        , id(std::move(the_id))
    {
    }

    static bool less_than(Activity const &lhs, Activity const &rhs)
    {
        return lhs.segment.start_time.less_than(rhs.segment.start_time);
    }

    StorageResult<bool> load(LoadingDepth depth);
    StorageResult<bool> load_from_dir(fs::path const &dir);
    StorageResult<bool> load_from_slice(std::string_view const &buffer);

private:
    Error<StorageError> analyze();
    bool                load_fit_file_filter(FITDataRecord const &rec);
    StorageResult<bool> load_fit_file(FITFile &fit_file);
    // StorageResult<bool> load_strava_file(fs::path const &dir);
};

struct Activities {
    Storage              *storage { nullptr };
    Month                 id { };
    std::vector<Activity> activities { };

    Activities(Storage &the_storage, Month const &month)
        : storage(&the_storage)
        , id(month)
    {
    }

    Error<StorageError> list();
};

template<>
JSONValue encode(Segment const &value);
template<>
JSONValue encode(Lap const &value);
template<>
JSONValue encode(Activity const &value);
template<>
JSONValue encode(Activities const &value);

}

inline std::ostream &operator<<(std::ostream &os, ST::Activity const &activity)
{
    os << activity.id;
    return os;
}

inline std::ostream &operator<<(std::ostream &os, ST::Activities const &activities)
{
    os << activities.id << " [" << activities.activities.size() << "]";
    return os;
}
