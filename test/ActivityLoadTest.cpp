/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests loading an activity from a FIT file, built byte by byte with
 * TestFit.h: there are no FIT files in the repository.
 */

#include <ctime>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <IO.h>

#include <storage/Activity.h>
#include <storage/Storage.h>

#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using namespace ST::Test;

namespace fs = std::filesystem;

namespace {

void add_lap(FITBuilder &fit, u32 start, u32 count)
{
    fit.definition(1, 19,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 2, .size = 4, .base_type = Uint32 },
            { .num = 7, .size = 4, .base_type = Uint32 },
            { .num = 8, .size = 4, .base_type = Uint32 },
        });
    fit.message(1);
    fit.u32_le(Start + start + count);
    fit.u32_le(Start + start);
    fit.u32_le(count * 1000);
    fit.u32_le(count * 1000);
}

void add_record(FITBuilder &fit, u32 at, float32 altitude, u8 cadence)
{
    fit.definition(2, 20,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 2, .size = 2, .base_type = Uint16 },
            { .num = 4, .size = 1, .base_type = Uint8 },
        });
    fit.message(2);
    fit.u32_le(Start + at);
    // Altitude is in 1/5 m, from -500 m.
    fit.u16_le(static_cast<u16>((altitude + 500.0f) * 5.0f));
    fit.byte(cadence);
}

struct LoadedActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    bool     loaded { false };

    explicit LoadedActivity(std::string const &file)
    {
        activity.depth = LoadingDepth::Deep;
        auto const res = activity.load_from_slice(file);
        loaded = res.has_value() && *res;
    }
};

void test_session_total_ascent_is_loaded()
{
    FITBuilder fit;
    add_session(fit, 3600, 345);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(t.activity.segment.total_ascent.has_value());
    CHECK_EQ(t.activity.segment.total_ascent.value_or(0), 345);
    CHECK_EQ(t.activity.segment.climb(), 345.0f);
    // The session's other fields still load, after the new one.
    CHECK_EQ(t.activity.segment.moving.elapsed, 3600u);
}

void test_session_without_total_ascent()
{
    FITBuilder fit;
    add_session(fit, 3600, 0xFFFF);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(!t.activity.segment.total_ascent.has_value());
}

void test_manufacturer_is_loaded()
{
    FITBuilder fit;
    add_file_id(fit, 260);
    add_session(fit, 3600, 345);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(t.activity.manufacturer == FIT::manufacturer::zwift);
}

void test_another_manufacturer()
{
    FITBuilder fit;
    add_file_id(fit, 1); // Garmin
    add_session(fit, 3600, 345);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(t.activity.manufacturer == FIT::manufacturer::garmin);
}

// 0xFFFF is FIT's invalid uint16: the file doesn't say who made it.
void test_invalid_manufacturer()
{
    FITBuilder fit;
    add_file_id(fit, 0xFFFF);
    add_session(fit, 3600, 345);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(!t.activity.manufacturer.has_value());
}

void test_without_a_file_id()
{
    FITBuilder fit;
    add_session(fit, 3600, 345);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(!t.activity.manufacturer.has_value());
}

void test_session_sub_sport_is_loaded()
{
    FITBuilder fit;
    add_session(fit, 3600, 345, 8); // Mountain
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(t.activity.sport == sport::Cycling);
    CHECK(t.activity.sub_sport == FIT::sub_sport::mountain);
}

void test_session_without_sub_sport()
{
    FITBuilder fit;
    add_session(fit, 3600, 345);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(!t.activity.sub_sport.has_value());
}

// A file with two sessions shows the sport of the last one, so the
// sub-sport has to come from that one too, even when it has none.
void test_sub_sport_is_from_the_last_session()
{
    FITBuilder fit;
    add_session(fit, 3600, 345, 8); // Mountain
    add_session(fit, 1800, 100);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(!t.activity.sub_sport.has_value());
}

// Generic is a sub-sport, not the lack of one.
void test_generic_sub_sport_is_kept()
{
    FITBuilder fit;
    add_session(fit, 3600, 345, 0); // Generic
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(t.activity.sub_sport == FIT::sub_sport::generic);
}

// Two laps of 20 records. Lap 1 jitters by 1 m and has a stop at record
// 5; lap 2 climbs from 100 m to 110 m in 1 m steps. The session has the
// device's climb, the laps work out their own, with the hysteresis.
std::string two_laps()
{
    FITBuilder fit;
    add_lap(fit, 0, 20);
    add_lap(fit, 20, 20);
    for (u32 ix = 0; ix < 40; ++ix) {
        auto altitude = (ix < 20) ? 100.0f + static_cast<float32>(ix % 2) : 100.0f + static_cast<float32>(std::min(ix - 20, 10u));
        add_record(fit, ix, altitude, (ix == 5) ? 0 : 90);
    }
    add_session(fit, 40, 345);
    return fit.file();
}

void test_laps_work_out_their_own_climb()
{
    LoadedActivity t { two_laps() };
    CHECK(t.loaded);
    CHECK_EQ(t.activity.records.size(), 40uz);
    CHECK_EQ(t.activity.laps.size(), 2uz);
    if (t.activity.laps.size() != 2) {
        return;
    }
    CHECK_EQ(t.activity.segment.climb(), 345.0f);
    auto const &lap1 = t.activity.laps[0].segment;
    auto const &lap2 = t.activity.laps[1].segment;
    CHECK(!lap1.total_ascent.has_value());
    CHECK_EQ(lap1.climb(), 0.0f);
    CHECK_EQ(lap2.climb(), 9.0f);
}

// The session's cadence leaves out the stop at record 5: the average is
// 90, and so is the minimum, over the 30 records after the stop. A lap's
// cadence should be worked out from the same, recorded, cadence: smoothed,
// the records after the stop would be pedaling at 60, and pull the
// average down. A lap of 20 records has no minimum.
void test_lap_cadence_is_from_the_recorded_cadence()
{
    LoadedActivity t { two_laps() };
    CHECK(t.loaded);
    auto const &session = t.activity.segment.cadence;
    CHECK(session.has_value());
    if (session) {
        CHECK_EQ(session->average, 90.0f);
        CHECK_EQ(session->min.value_or(0), 90.0f);
        CHECK_EQ(session->min_at, 35uz);
    }
    if (t.activity.laps.empty()) {
        CHECK(!t.activity.laps.empty());
        return;
    }
    auto const &lap = t.activity.laps[0].segment.cadence;
    CHECK(lap.has_value());
    if (lap) {
        CHECK_EQ(lap->average, 90.0f);
        CHECK(!lap->min.has_value());
    }
}

// The graph's records are smoothed; `recorded` keeps them as they were,
// for a range's stats. Record 5's cadence of 0 is smoothed away in
// `records`.
void test_records_as_recorded()
{
    LoadedActivity t { two_laps() };
    CHECK(t.loaded);
    CHECK_EQ(t.activity.recorded.size(), t.activity.records.size());
    if (t.activity.recorded.size() != 40) {
        return;
    }
    CHECK_EQ(t.activity.recorded[5].cadence.value_or(255), 0);
    CHECK(t.activity.records[5].cadence.value_or(0) != 0);
}

// A range over exactly a lap has the lap's stats, worked out from the
// same records as recorded.
void test_range_over_a_lap()
{
    LoadedActivity t { two_laps() };
    if (t.activity.laps.size() != 2) {
        CHECK_EQ(t.activity.laps.size(), 2uz);
        return;
    }
    auto const &lap = t.activity.laps[1];
    auto const  range = range_segment(t.activity, lap.first_record, lap.end_record - 1);
    CHECK(range.has_value());
    if (!range) {
        return;
    }
    CHECK_EQ(range->climb(), lap.segment.climb());
    CHECK_EQ(range->cadence->average, lap.segment.cadence->average);
    CHECK_EQ(range->computed_elevation_range.range->max, lap.segment.computed_elevation_range.range->max);
    // 20 records, a second apart.
    CHECK_EQ(range->elapsed.elapsed, 19u);
    CHECK_EQ(range->moving.elapsed, 19u);
    auto const first = range_segment(t.activity, 0, 19);
    CHECK(first.has_value() && first->cadence->average == t.activity.laps[0].segment.cadence->average);
}

record record_at(u32 at, std::optional<f32> distance, std::optional<f32> speed)
{
    return record { .timestamp = DateTime { .timestamp = 1'000'000 + at }, .distance = distance, .speed = speed };
}

// The distance is from the first record to the last; the moving time
// leaves out a pause, a gap of more than 10 s, and records standing still.
void test_range_distance_and_moving_time()
{
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    activity.recorded = {
        record_at(0, 1000, 5),
        record_at(1, 1005, 5),
        record_at(2, 1010, 5),
        // Auto-paused for a minute.
        record_at(62, 1010, 5),
        record_at(63, 1015, 5),
        // Standing still at a light.
        record_at(64, 1015, 0),
        record_at(65, 1015, 0.2f),
        record_at(66, 1020, 5),
        record_at(76, 1070, 5),
    };
    auto const range = range_segment(activity, 0, 8);
    CHECK(range.has_value());
    if (!range) {
        return;
    }
    CHECK_EQ(range->distance, 70.0f);
    CHECK_EQ(range->elapsed.elapsed, 76u);
    // 1 + 1, then 1, then 1 at 66, and 10 at 76.
    CHECK_EQ(range->moving.elapsed, 14u);
    CHECK_EQ(range->start_time.timestamp, 1'000'000u);
    CHECK_EQ(range->end_time.timestamp, 1'000'076u);
    // Part of it.
    CHECK_EQ(range_segment(activity, 3, 4)->distance, 5.0f);
    // The clock set back during a range: no negative times.
    auto backwards = activity;
    backwards.recorded[4].timestamp.timestamp = 1'000'000;
    auto const back = range_segment(backwards, 3, 4);
    CHECK(back.has_value() && back->elapsed.elapsed == 0u && back->moving.elapsed == 0u);
    // Out of bounds.
    CHECK(!range_segment(activity, 0, 9).has_value());
    CHECK(!range_segment(activity, 4, 3).has_value());
}

// Without distance or speed, as indoors without a sensor: no distance, and
// every gap of 10 s or less moving.
void test_range_without_distance()
{
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    activity.recorded = { record_at(0, { }, { }), record_at(5, { }, { }), record_at(30, { }, { }), record_at(31, { }, { }) };
    auto const range = range_segment(activity, 0, 3);
    CHECK(range.has_value() && range->distance == 0.0f);
    CHECK(range.has_value() && range->moving.elapsed == 6u);
}

// A record with power and heart rate, which the graph smooths.
void add_power_record(FITBuilder &fit, u32 at, u16 power, u8 heart_rate)
{
    fit.definition(4, 20,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 3, .size = 1, .base_type = Uint8 },
            { .num = 7, .size = 2, .base_type = Uint16 },
        });
    fit.message(4);
    fit.u32_le(Start + at);
    fit.byte(heart_rate);
    fit.u16_le(power);
}

// Two laps of 40 records with spiky power and heart rate: smoothing for
// the graph moves the maxima, so a range over a lap only has the lap's
// maxima, NP and averages if it's worked out from the records as recorded.
void test_range_over_a_lap_has_its_power_and_heart_rate()
{
    FITBuilder fit;
    add_lap(fit, 0, 40);
    add_lap(fit, 40, 40);
    for (u32 ix = 0; ix < 80; ++ix) {
        auto const power = static_cast<u16>((ix % 7 == 3) ? 600 : 150 + 10 * (ix % 5));
        auto const hr = static_cast<u8>((ix == 57) ? 185 : 130 + ix % 11);
        add_power_record(fit, ix, power, hr);
    }
    add_session(fit, 80, 0xFFFF);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    if (t.activity.laps.size() != 2 || t.activity.recorded.size() != 80) {
        CHECK_EQ(t.activity.laps.size(), 2uz);
        CHECK_EQ(t.activity.recorded.size(), 80uz);
        return;
    }
    auto const &lap = t.activity.laps[1];
    auto const  range = range_segment(t.activity, lap.first_record, lap.end_record - 1);
    if (!range) {
        CHECK(range.has_value());
        return;
    }
    auto const &lp = lap.segment.computed_power_range.range;
    auto const &rp = range->computed_power_range.range;
    CHECK(lp.has_value() && rp.has_value());
    if (lp && rp) {
        CHECK_EQ(rp->max, lp->max);
        CHECK_EQ(rp->max_at, lp->max_at);
        CHECK_EQ(rp->average, lp->average);
    }
    auto const &lnp = lap.segment.computed_normalized_range.range;
    auto const &rnp = range->computed_normalized_range.range;
    CHECK(lnp.has_value() && rnp.has_value());
    if (lnp && rnp) {
        CHECK_EQ(rnp->normalized, lnp->normalized);
    }
    auto const &lhr = lap.segment.computed_hr_range.range;
    auto const &rhr = range->computed_hr_range.range;
    CHECK(lhr.has_value() && rhr.has_value());
    if (lhr && rhr) {
        CHECK_EQ(rhr->max, lhr->max);
        CHECK_EQ(rhr->max_at, lhr->max_at);
        CHECK_EQ(rhr->average, lhr->average);
    }
}

// TSS is over the moving time: a range with a pause in it has the TSS of
// the same records without one.
void test_range_tss_is_over_the_moving_time()
{
    Storage  storage { };
    Activity steady { storage, ActivityID::dummy(ActivityFile::Fit) };
    Activity paused { storage, ActivityID::dummy(ActivityFile::Fit) };
    for (u32 ix = 0; ix < 120; ++ix) {
        auto const power = static_cast<u16>(200 + 20 * (ix % 3));
        steady.recorded.push_back(record { .timestamp = DateTime { .timestamp = 1'000'000 + ix }, .power = power });
        paused.recorded.push_back(record { .timestamp = DateTime { .timestamp = 1'000'000 + ix + ((ix >= 60) ? 600 : 0) }, .power = power });
    }
    auto const a = range_segment(steady, 0, 119);
    auto const b = range_segment(paused, 0, 119);
    if (!a || !b) {
        CHECK(a.has_value() && b.has_value());
        return;
    }
    CHECK_EQ(a->moving.elapsed, 119u);
    CHECK_EQ(b->moving.elapsed, 118u);
    CHECK_EQ(b->elapsed.elapsed, 719u);
    CHECK(a->tss.has_value() && *a->tss > 0);
    CHECK(b->tss.has_value() && a->tss.has_value() && *b->tss <= *a->tss);
}

// An activity in a directory of its own, as `dummy.fit` and, if
// `sidecar` isn't empty, `dummy.json`, loaded as the store loads it. The
// counter keeps the directories of a test's activities apart.
int activities_in_dirs { 0 };

struct ActivityInDir {
    fs::path dir { fs::temp_directory_path() / std::format("sweattrails-sidecar-{}-{}", std::time(nullptr), ++activities_in_dirs) };
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    bool     loaded { false };

    explicit ActivityInDir(std::string_view sidecar, std::string_view strava_file = "")
    {
        fs::create_directories(dir);
        FITBuilder fit;
        add_workout(fit, "Tempo intervals");
        add_session(fit, 3600, 345);
        MUST(write_file_by_name((dir / "dummy.fit").string(), fit.file()));
        if (!sidecar.empty()) {
            MUST(write_file_by_name((dir / "dummy.json").string(), sidecar));
        }
        if (!strava_file.empty()) {
            MUST(write_file_by_name((dir / "dummy.strava").string(), strava_file));
        }
        activity.depth = LoadingDepth::Deep;
        auto const res = activity.load_from_dir(dir);
        loaded = res.has_value() && *res;
    }

    ~ActivityInDir()
    {
        std::error_code ec { };
        fs::remove_all(dir, ec);
    }
};

// The name on Garmin Connect wins over the FIT file's workout name.
void test_title_from_the_sidecar()
{
    ActivityInDir t { R"({"activityId": 24790584050, "activityName": "Zwift - Pacer Group Ride with Maria"})" };
    CHECK(t.loaded);
    CHECK_EQ(t.activity.title, "Zwift - Pacer Group Ride with Maria");
}

// Wahoo calls it `name`.
void test_title_from_a_wahoo_sidecar()
{
    ActivityInDir t { R"({"id": 2, "starts": "2026-10-20T12:00:00.000Z", "name": "Morning ride"})" };
    CHECK(t.loaded);
    CHECK_EQ(t.activity.title, "Morning ride");
}

// Without a name in the sidecar, such as the Rungap webhook's, or without
// a valid one, the title is the workout's name.
void test_sidecar_without_a_title()
{
    for (auto const *sidecar : { "", R"({"start_time": 1767536377})", "not JSON", R"({"activityName": ""})", R"({"name": ""})", R"({"name": 12})", "[1, 2]" }) {
        ActivityInDir t { sidecar };
        CHECK(t.loaded);
        CHECK_EQ(t.activity.title, "Tempo intervals");
    }
}

std::string read_json(fs::path const &file)
{
    return read_file_by_name(file.string()).value_or("<none>");
}

// The user's name wins over the service's, which stays as it came.
void test_own_title_wins()
{
    ActivityInDir t { R"({"activityName": "Morning Ride", "title": "Ride to Niagara"})" };
    CHECK(t.loaded);
    CHECK_EQ(t.activity.title, "Ride to Niagara");
    CHECK_EQ(t.activity.original_title, "Morning Ride");
    ActivityInDir empty { R"({"name": "Morning Ride", "title": ""})" };
    CHECK_EQ(empty.activity.title, "Morning Ride");
}

void test_save_keeps_the_rest_of_the_sidecar()
{
    ActivityInDir t { R"({"activityId": 24790584050, "activityName": "Morning Ride", "distance": 30123.5})" };
    auto const    sidecar = t.dir / "dummy.json";
    CHECK(save_sidecar_title(sidecar, "Café ride").has_value());
    auto const json = JSONValue::deserialize(read_json(sidecar));
    CHECK(json.has_value());
    if (json) {
        CHECK_EQ(json->try_get<std::string>("title").value_or(""), "Café ride");
        CHECK_EQ(json->try_get<std::string>("activityName").value_or(""), "Morning Ride");
        CHECK_EQ(json->try_get<double>("distance").value_or(0), 30123.5);
        CHECK_EQ(json->size(), 4uz);
    }
    CHECK_EQ(sidecar_own_title(sidecar).value_or(""), "Café ride");
    // An empty name takes the user's out again.
    CHECK(save_sidecar_title(sidecar, "").has_value());
    CHECK(!sidecar_own_title(sidecar).has_value());
    CHECK_EQ(sidecar_title(sidecar).value_or(""), "Morning Ride");
}

// A FIT file copied in by hand has no sidecar: renaming makes one, and
// taking the name out again removes it.
void test_save_without_a_sidecar()
{
    ActivityInDir t { "" };
    auto const    sidecar = t.dir / "dummy.json";
    CHECK(save_sidecar_title(sidecar, "Ålesund loop").has_value());
    auto const json = JSONValue::deserialize(read_json(sidecar));
    CHECK(json.has_value() && json->size() == 1);
    CHECK_EQ(sidecar_own_title(sidecar).value_or(""), "Ålesund loop");
    CHECK(save_sidecar_title(sidecar, "").has_value());
    CHECK(!fs::exists(sidecar));
    // Nothing to take out, and nothing there: still nothing.
    CHECK(save_sidecar_title(sidecar, "").has_value());
    CHECK(!fs::exists(sidecar));
}

void test_save_refuses_a_sidecar_that_isnt_an_object()
{
    for (auto const *contents : { "not JSON", "[1, 2]", "12" }) {
        ActivityInDir t { contents };
        auto const    sidecar = t.dir / "dummy.json";
        CHECK(!save_sidecar_title(sidecar, "Ride").has_value());
        CHECK_EQ(read_json(sidecar), contents);
    }
}

void test_rename()
{
    ActivityInDir t { R"({"name": "Morning ride"})" };
    CHECK(t.activity.rename("  Rückweg  ").has_value());
    CHECK_EQ(t.activity.title, "Rückweg");
    CHECK_EQ(sidecar_own_title(t.activity.sidecar).value_or(""), "Rückweg");
    // Loaded again, it has the name.
    Activity again { t.storage, ActivityID::dummy(ActivityFile::Fit) };
    again.depth = LoadingDepth::Deep;
    CHECK(again.load_from_dir(t.dir).value_or(false));
    CHECK_EQ(again.title, "Rückweg");
    // Only spaces: the original name again.
    CHECK(t.activity.rename("   ").has_value());
    CHECK_EQ(t.activity.title, "Morning ride");
    CHECK(!sidecar_own_title(t.activity.sidecar).has_value());
}

// Without a sidecar or a service's name, the original is the workout's.
void test_rename_back_to_the_workouts_name()
{
    ActivityInDir t { "" };
    CHECK(t.activity.rename("Hill repeats").has_value());
    // The rename made a sidecar, and the name is there after a reload.
    CHECK(fs::exists(t.dir / "dummy.json"));
    Activity again { t.storage, ActivityID::dummy(ActivityFile::Fit) };
    again.depth = LoadingDepth::Deep;
    CHECK(again.load_from_dir(t.dir).value_or(false));
    CHECK_EQ(again.title, "Hill repeats");
    CHECK(t.activity.rename("").has_value());
    CHECK_EQ(t.activity.title, "Tempo intervals");
    CHECK(!fs::exists(t.activity.sidecar));
}

// A sidecar that can't be written leaves the title as it was.
void test_rename_that_fails()
{
    ActivityInDir t { "not JSON" };
    CHECK(!t.activity.rename("Ride").has_value());
    CHECK_EQ(t.activity.title, "Tempo intervals");
}
// A name that's the same as before, also with spaces around it, isn't
// saved: no sidecar is made for it.
void test_rename_to_the_same_name()
{
    ActivityInDir t { "" };
    CHECK(t.activity.rename("Tempo intervals").has_value());
    CHECK(t.activity.rename("  Tempo intervals ").has_value());
    CHECK(t.activity.rename("").has_value());
    CHECK(!fs::exists(t.activity.sidecar));
}
// Strava's name wins over the service's, and the user's over Strava's.
// The original title is Strava's, and the notes are its description.
void test_strava_title_from_the_sidecar()
{
    ActivityInDir t { R"({"activityName": "Morning Ride", "strava": {"id": 17, "name": "Ride to Niagara", "description": "With Anna"}})" };
    CHECK(t.loaded);
    CHECK_EQ(t.activity.title, "Ride to Niagara");
    CHECK_EQ(t.activity.original_title, "Ride to Niagara");
    CHECK_EQ(t.activity.notes, "With Anna");
    ActivityInDir own { R"({"activityName": "Morning Ride", "title": "Niagara", "strava": {"id": 17, "name": "Ride to Niagara"}})" };
    CHECK_EQ(own.activity.title, "Niagara");
    CHECK_EQ(own.activity.original_title, "Ride to Niagara");
    CHECK_EQ(own.activity.notes, "");
}

// A `"strava"` that can't be read, or has an empty name, leaves the
// service's name.
void test_strava_in_the_sidecar_that_isnt_valid()
{
    for (auto const *sidecar : { R"({"name": "Morning Ride", "strava": 12})", R"({"name": "Morning Ride", "strava": {"name": "Ride"}})",
             R"({"name": "Morning Ride", "strava": {"id": 17, "name": ""}})" }) {
        ActivityInDir t { sidecar };
        CHECK(t.loaded);
        CHECK_EQ(t.activity.title, "Morning Ride");
    }
}

// The old app's `.strava` file wins over the service's name, and the
// sidecar's Strava over it. Its description is the notes until Strava's
// description has been fetched.
void test_title_from_a_strava_file()
{
    auto const    strava_file = R"({"id": 3, "name": "Old ride", "description": "From the old app"})";
    ActivityInDir t { R"({"activityName": "Morning Ride"})", strava_file };
    CHECK_EQ(t.activity.title, "Old ride");
    CHECK_EQ(t.activity.notes, "From the old app");
    ActivityInDir newer { R"({"strava": {"id": 3, "name": "New ride"}})", strava_file };
    CHECK_EQ(newer.activity.title, "New ride");
    CHECK_EQ(newer.activity.notes, "From the old app");
    ActivityInDir fetched { R"({"strava": {"id": 3, "name": "New ride", "description": ""}})", strava_file };
    CHECK_EQ(fetched.activity.notes, "");
    ActivityInDir broken { "", "not JSON" };
    CHECK(broken.loaded);
    CHECK_EQ(broken.activity.title, "Tempo intervals");
}

// Strava's name saved after the activity was loaded shows when its names
// are read again, and goes when it does.
void test_read_names_again()
{
    ActivityInDir t { R"({"activityName": "Morning Ride"})" };
    CHECK(save_sidecar_strava(t.activity.sidecar, StravaTitle { .id = 17, .name = "Ride to Niagara", .description = "With Anna" }).has_value());
    t.activity.read_names();
    CHECK_EQ(t.activity.title, "Ride to Niagara");
    CHECK_EQ(t.activity.original_title, "Ride to Niagara");
    CHECK_EQ(t.activity.notes, "With Anna");
    MUST(write_file_by_name(t.activity.sidecar.string(), std::string_view { "{}" }));
    t.activity.read_names();
    CHECK_EQ(t.activity.title, "Tempo intervals");
    CHECK_EQ(t.activity.notes, "");
}

// Saving Strava's name keeps the user's and the service's, and replaces
// what Strava had before.
void test_save_sidecar_strava()
{
    ActivityInDir t { R"({"activityName": "Morning Ride", "title": "Niagara"})" };
    auto const    sidecar = t.dir / "dummy.json";
    CHECK(save_sidecar_strava(sidecar, StravaTitle { .id = 17, .name = "Ride to Niagara" }).has_value());
    StravaTitle const described { .id = 17, .name = "Ride to Niagara", .description = "With Anna" };
    CHECK(save_sidecar_strava(sidecar, described).has_value());
    auto const names = sidecar_titles(sidecar);
    CHECK_EQ(names.service.value_or(""), "Morning Ride");
    CHECK_EQ(names.own.value_or(""), "Niagara");
    CHECK(names.strava == described);
    ActivityInDir none { "" };
    CHECK(save_sidecar_strava(none.dir / "dummy.json", described).has_value());
    CHECK(sidecar_titles(none.dir / "dummy.json").strava == described);
    ActivityInDir array { "[1, 2]" };
    CHECK(!save_sidecar_strava(array.dir / "dummy.json", described).has_value());
    CHECK_EQ(read_json(array.dir / "dummy.json"), "[1, 2]");
}

// An activity that wasn't loaded from a directory has nowhere to save its
// name, and keeps the one it has.
void test_rename_without_a_sidecar_path()
{
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    activity.title = "Morning Ride";
    activity.original_title = "Morning Ride";
    CHECK(!activity.rename("Evening Ride").has_value());
    CHECK_EQ(activity.title, "Morning Ride");
}

// The sidecar is written through a .part file, which doesn't stay behind.
void test_save_leaves_no_part_file()
{
    ActivityInDir t { R"({"activityName": "Morning Ride"})" };
    CHECK(t.activity.rename("Ride to Niagara with Anna").has_value());
    CHECK(fs::exists(t.activity.sidecar));
    CHECK(!fs::exists(t.dir / "dummy.json.part"));
}

// In a directory that can't be written, the name isn't saved, the title
// stays, and the sidecar is as it was.
void test_rename_in_a_read_only_directory()
{
    ActivityInDir   t { R"({"activityName": "Morning Ride"})" };
    std::error_code ec { };
    fs::permissions(t.dir, fs::perms::owner_read | fs::perms::owner_exec, ec);
    CHECK(!ec);
    // Root can write anyway: nothing to check then.
    if (!write_file_by_name((t.dir / "probe").string(), std::string_view { "x" })) {
        CHECK(!t.activity.rename("Rückweg").has_value());
        CHECK_EQ(t.activity.title, "Morning Ride");
        CHECK_EQ(read_json(t.activity.sidecar), R"({"activityName": "Morning Ride"})");
        CHECK(!fs::exists(t.dir / "dummy.json.part"));
    }
    fs::permissions(t.dir, fs::perms::owner_all, ec);
}

// The user's name, renamed again to the service's: that is a name of the
// user's too, and loading it again gives the same title.
void test_rename_back_to_the_services_name_by_typing_it()
{
    ActivityInDir t { R"({"activityName": "Morning Ride", "title": "Ride to Niagara"})" };
    CHECK(t.activity.rename("Morning Ride").has_value());
    CHECK_EQ(t.activity.title, "Morning Ride");
    Activity again { t.storage, ActivityID::dummy(ActivityFile::Fit) };
    CHECK(again.load_from_dir(t.dir).value_or(false));
    CHECK_EQ(again.title, "Morning Ride");
}
}

// Added by the Reviewer: a rename wins over Strava's name, and taking it
// out brings Strava's back, also when Strava's came after the rename.
void test_rename_over_stravas_title()
{
    ActivityInDir t { R"({"activityName": "Morning Ride", "strava": {"id": 17, "name": "Ride to Niagara"}})" };
    CHECK(t.activity.rename("Niagara").has_value());
    CHECK_EQ(t.activity.title, "Niagara");
    CHECK(t.activity.rename("").has_value());
    CHECK_EQ(t.activity.title, "Ride to Niagara");
    CHECK(!sidecar_own_title(t.activity.sidecar).has_value());
    CHECK(sidecar_titles(t.activity.sidecar).strava.has_value());

    ActivityInDir later { R"({"activityName": "Morning Ride"})" };
    CHECK(later.activity.rename("Niagara").has_value());
    CHECK(save_sidecar_strava(later.activity.sidecar, StravaTitle { .id = 17, .name = "Ride to Niagara" }).has_value());
    later.activity.read_names();
    CHECK_EQ(later.activity.title, "Niagara");
    CHECK(later.activity.rename("").has_value());
    CHECK_EQ(later.activity.title, "Ride to Niagara");
}

int main()
{
    test_manufacturer_is_loaded();
    test_another_manufacturer();
    test_invalid_manufacturer();
    test_without_a_file_id();
    test_session_total_ascent_is_loaded();
    test_session_without_total_ascent();
    test_session_sub_sport_is_loaded();
    test_session_without_sub_sport();
    test_sub_sport_is_from_the_last_session();
    test_generic_sub_sport_is_kept();
    test_laps_work_out_their_own_climb();
    test_lap_cadence_is_from_the_recorded_cadence();
    test_records_as_recorded();
    test_range_over_a_lap();
    test_range_distance_and_moving_time();
    test_range_without_distance();
    test_range_over_a_lap_has_its_power_and_heart_rate();
    test_range_tss_is_over_the_moving_time();
    test_title_from_the_sidecar();
    test_title_from_a_wahoo_sidecar();
    test_sidecar_without_a_title();
    test_own_title_wins();
    test_strava_title_from_the_sidecar();
    test_strava_in_the_sidecar_that_isnt_valid();
    test_title_from_a_strava_file();
    test_read_names_again();
    test_save_sidecar_strava();
    test_rename_over_stravas_title();
    test_save_keeps_the_rest_of_the_sidecar();
    test_save_without_a_sidecar();
    test_save_refuses_a_sidecar_that_isnt_an_object();
    test_rename();
    test_rename_back_to_the_workouts_name();
    test_rename_that_fails();
    test_rename_to_the_same_name();
    test_rename_without_a_sidecar_path();
    test_save_leaves_no_part_file();
    test_rename_in_a_read_only_directory();
    test_rename_back_to_the_services_name_by_typing_it();
    return ST::Test::test_result();
}
