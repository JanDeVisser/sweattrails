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

}

int main()
{
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
    return ST::Test::test_result();
}
