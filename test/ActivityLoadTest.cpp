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

// An activity in a directory of its own, as `dummy.fit` and, if
// `sidecar` isn't empty, `dummy.json`, loaded as the store loads it.
struct ActivityInDir {
    fs::path dir { fs::temp_directory_path() / std::format("sweattrails-sidecar-{}", std::time(nullptr)) };
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    bool     loaded { false };

    explicit ActivityInDir(std::string_view sidecar)
    {
        fs::create_directories(dir);
        FITBuilder fit;
        add_workout(fit, "Tempo intervals");
        add_session(fit, 3600, 345);
        MUST(write_file_by_name((dir / "dummy.fit").string(), fit.file()));
        if (!sidecar.empty()) {
            MUST(write_file_by_name((dir / "dummy.json").string(), sidecar));
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
    test_title_from_the_sidecar();
    test_title_from_a_wahoo_sidecar();
    test_sidecar_without_a_title();
    test_own_title_wins();
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
