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

#include <Date.h>
#include <Format.h>
#include <JSON.h>

#include <Map.h>
#include <Range.h>
#include <fit/FIT.h>
#include <fit/Profile.h>
#include <storage/Types.h>

namespace ST {

using namespace ST::FIT;

struct Storage;

constexpr float32 JANS_FTP = 275;

// A rise in altitude only counts as climbing once it's this many meters,
// so that noise in the altitude doesn't add to the climb.
constexpr float32 ClimbHysteresis = 3.0f;

// A record counts as pedaling, or running, with at least this cadence, as
// in the FIT file, and, if it has power, at least this power. Coasting
// doesn't just read as zero: the cadence decays over a few seconds, is
// held at its last reading, or lags behind when pedaling again.
constexpr u8  PedalingCadence = 20;
constexpr u16 PedalingPower = 20;

// Min Cadence is the lowest average over this many consecutive records,
// all pedaling.
constexpr size_t MinCadenceRecords = 30;

struct CadenceSample {
    size_t             at { 0 };      // The record's index
    u8                 cadence { 0 }; // As in the FIT file: rpm, or strides per minute
    std::optional<u16> power { };     // The record's power, if it has one
};

struct CadenceStats {
    float32                average { 0 }; // While pedaling
    std::optional<float32> min { };       // Over MinCadenceRecords of pedaling
    size_t                 min_at { 0 };
};

// The average cadence while pedaling, and the lowest average over
// MinCadenceRecords consecutive records that are all pedaling. A record
// without a sample breaks the run. min_at is the last record of the
// lowest run. Nothing when no sample is pedaling.
std::optional<CadenceStats> cadence_stats(std::span<CadenceSample const> samples);

// The activity's name in its sidecar, `<name>.json` next to its FIT file,
// which the Garmin Connect and Wahoo downloads write. Nothing if there's no sidecar,
// or it has no name, such as those of the Rungap webhook SweatTrails used
// before.
std::optional<std::string> sidecar_title(fs::path const &sidecar);

// The name the user gave the activity, under SweatTrails' own key in the
// sidecar, `"title"`. It wins over the service's name.
std::optional<std::string> sidecar_own_title(fs::path const &sidecar);

// Both names, from one read of the sidecar.
struct SidecarTitles {
    std::optional<std::string> service { }; // sidecar_title()
    std::optional<std::string> own { };     // sidecar_own_title()
};

SidecarTitles sidecar_titles(fs::path const &sidecar);

// Saves the user's name for the activity in its sidecar, keeping the rest
// of it, or removes it for an empty `title`. Makes the sidecar if there
// isn't one, and removes it if nothing else is left in it. A sidecar that
// isn't a JSON object is left as it is, and is an error.
std::expected<void, LibCError> save_sidecar_title(fs::path const &sidecar, std::string_view title);

struct Segment {
    DateTime                    start_time { };
    DateTime                    end_time { };
    Duration                    elapsed { };
    Duration                    moving { };
    float32                     distance { 0 };
    float32                     computed_distance { 0 };
    Range<float32>              computed_speed_range { };
    SmoothedRange<u16, 3>       computed_power_range { };
    NormalizedRange<u16, 30>    computed_normalized_range { };
    SmoothedRange<u8, 3>        computed_cadence_range { };
    std::optional<CadenceStats> cadence { };
    SmoothedRange<u8, 3>        computed_hr_range { };
    Range<i16>                  computed_elevation_range { };
    float32                     computed_climb { 0 }; // Meters climbed; descents don't count
    std::optional<u16>          total_ascent { };     // Meters climbed, as the device worked it out
    std::optional<float32>      intensity_factor { };
    std::optional<float32>      variability_index { };
    std::optional<float32>      tss { };
    std::optional<Box>          box { };

    void analyze(std::span<record> records, bool smooth);

    // The device's climb if it has one, else the one analyze() worked out.
    [[nodiscard]] float32 climb() const
    {
        return total_ascent ? static_cast<float32>(*total_ascent) : computed_climb;
    }
};

struct Lap {
    // The lap's records are Activity::records[first_record, end_record). These
    // are indices rather than a span, so that they stay valid when the
    // Activity is copied or moved.
    size_t      first_record { 0 };
    size_t      end_record { 0 };
    std::string title { };
    std::string notes { };
    Segment     segment { };
};

struct Activity {
    Storage                      *storage { nullptr };
    LoadingDepth                  depth { LoadingDepth::None };
    ActivityID                    id { };
    u32                           serial { 0 };
    u16                           number { 0 };
    ST::sport                     sport { ST::sport::Running };
    std::optional<FIT::sub_sport> sub_sport { };
    std::string                   title { };
    std::string                   original_title { }; // Without the name the user gave it
    std::string                   notes { };
    Segment                       segment { };
    std::vector<record>           records { };
    std::vector<Lap>              laps { };

    // Local time minus UTC in seconds, from the FIT activity message. Empty
    // if the file doesn't record the local time.
    std::optional<i32> utc_offset { };

    // `<name>.json` next to the FIT file, which load_from_dir() reads the
    // names from, whether it's there or not. Empty for an activity loaded
    // otherwise, which can't be renamed.
    fs::path sidecar { };

    Activity(Storage &the_storage, ActivityID the_id)
        : storage(&the_storage)
        , id(std::move(the_id))
    {
    }

    static bool less_than(Activity const &lhs, Activity const &rhs)
    {
        return lhs.segment.start_time.less_than(rhs.segment.start_time);
    }

    // TODO: subspan() doesn't check bounds, so a Lap that is out of date with
    // `records` reads out of bounds. Consider asserting that
    // first_record <= end_record <= records.size().
    [[nodiscard]] std::span<record const> lap_records(Lap const &lap) const
    {
        return std::span { records }.subspan(lap.first_record, lap.end_record - lap.first_record);
    }

    StorageResult<bool> load(LoadingDepth depth);
    StorageResult<bool> load_from_dir(fs::path const &dir);
    // Loads `file`, whatever its name, rather than `<id.name>.fit`.
    StorageResult<bool> load_from_file(fs::path const &file);
    StorageResult<bool> load_from_slice(std::string_view const &buffer);

    // Gives the activity the user's name, trimmed, and saves it in the
    // sidecar. An empty name goes back to the original title.
    std::expected<void, LibCError> rename(std::string_view name);

private:
    Error<StorageError> analyze();
    bool                load_fit_file_filter(FITDataRecord const &rec);
    StorageResult<bool> load_fit_file(FITFile &fit_file);
    StorageResult<bool> load_fit_file_by_name(fs::path const &file);
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
