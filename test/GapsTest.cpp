/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests finding the gaps where the watch was stopped, matching another
 * activity to them, and filling them in a store's FIT file.
 */

#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>

#include <IO.h>

#include <storage/Activity.h>
#include <storage/Gaps.h>
#include <storage/Storage.h>

#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>
#include <TestRun.h>

using namespace ST;

namespace fs = std::filesystem;

namespace {

i64 unix_time(u32 at)
{
    return static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET + at;
}

record at(u32 seconds, double north, std::optional<double> distance, double east = 0)
{
    return record {
        .timestamp = DateTime::from_timestamp(static_cast<i32>(unix_time(seconds))),
        .position = Coordinates {
            .lat = static_cast<float32>(Test::HomeLat + north / Test::MetresPerDegree),
            .lon = static_cast<float32>(Test::HomeLon + east / (Test::MetresPerDegree * std::cos(Test::HomeLat * std::numbers::pi / 180))),
        },
        .distance = distance.transform([](double d) { return static_cast<float32>(d); }),
    };
}

// The run of TestRun.h, as the app loads it.
std::vector<record> watch()
{
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    activity.depth = LoadingDepth::Deep;
    auto const bytes = Test::run();
    MUST(activity.load_from_slice(bytes));
    return activity.recorded;
}

// The friend ran the whole way, from `from` s on, their distance 50 m
// ahead of the watch's, `east` metres to the side.
std::vector<record> friend_run(u32 from = 0, double east = 0)
{
    std::vector<record> ret;
    for (auto seconds = from; seconds < 500; ++seconds) {
        ret.push_back(at(seconds, 3.0 * seconds, 3.0 * seconds + 50, east));
    }
    return ret;
}

void test_distance_between()
{
    auto const amsterdam = Coordinates { .lat = 52.3676f, .lon = 4.9041f };
    auto const paris = Coordinates { .lat = 48.8566f, .lon = 2.3522f };
    CHECK(std::abs(distance_between(amsterdam, paris) - 430'000) < 2'000);
    CHECK(std::abs(distance_between(Coordinates { .lat = 1, .lon = 1 }, Coordinates { .lat = 2, .lon = 1 }) - 111'195) < 10);
    CHECK_EQ(distance_between(paris, paris), 0.0);
}

void test_find_gaps()
{
    auto const records = watch();
    auto const gaps = find_gaps(records);
    CHECK_EQ(gaps.size(), 1uz);
    if (gaps.size() == 1) {
        CHECK_EQ(gaps[0].before, 99uz);
        CHECK_EQ(gaps[0].stop.timestamp, static_cast<u32>(unix_time(99)));
        CHECK_EQ(gaps[0].restart.timestamp, static_cast<u32>(unix_time(400)));
    }
}

// A long wait that didn't move, and a short stop that did, aren't gaps.
void test_not_gaps()
{
    std::vector<record> const waited { at(0, 0, 0), at(300, 10, 10), at(301, 13, 13) };
    CHECK(find_gaps(waited).empty());
    std::vector<record> const short_stop { at(0, 0, 0), at(60, 300, 3), at(61, 303, 6) };
    CHECK(find_gaps(short_stop).empty());
    std::vector<record> two { at(0, 0, 0), at(200, 600, 3), at(201, 603, 6), at(400, 1200, 9) };
    CHECK_EQ(find_gaps(two).size(), 2uz);
    // Without positions, indoors, there are none.
    auto indoors = two;
    for (auto &rec : indoors) {
        rec.position.reset();
    }
    CHECK(find_gaps(indoors).empty());
}

void test_plan_fills()
{
    auto const records = watch();
    auto const fills = plan_fills(records, find_gaps(records), friend_run());
    CHECK_EQ(fills.size(), 1uz);
    if (fills.size() != 1) {
        return;
    }
    auto const &fill = fills[0];
    CHECK_EQ(fill.stop, Test::Start + 99);
    CHECK_EQ(fill.restart, Test::Start + 400);
    CHECK(std::abs(fill.distance - 903) < 0.01);
    CHECK_EQ(fill.records.size(), 300uz);
    if (fill.records.size() == 300) {
        CHECK_EQ(fill.records.front().timestamp, Test::Start + 100);
        CHECK(fill.records.front().distance && std::abs(*fill.records.front().distance - 300) < 0.01);
        CHECK(fill.records.back().distance && std::abs(*fill.records.back().distance - 1197) < 0.01);
        CHECK(fill.records.front().lat.has_value());
    }
}

void test_plan_fills_not_covered()
{
    auto const records = watch();
    auto const gaps = find_gaps(records);
    // Started after the watch stopped.
    CHECK(plan_fills(records, gaps, friend_run(150)).empty());
    // Across town.
    CHECK(plan_fills(records, gaps, friend_run(0, 500)).empty());
    // Close enough.
    CHECK_EQ(plan_fills(records, gaps, friend_run(0, 150)).size(), 1uz);
}

// The run with two gaps, filled at once, as one drop does: the second
// gap's records carry on from the distance after the first, and the
// distance never goes back.
void test_fill_two_gaps()
{
    Storage    storage { };
    Activity   watched { storage, ActivityID::dummy(ActivityFile::Fit) };
    auto const bytes = Test::run_two_gaps();
    watched.depth = LoadingDepth::Deep;
    MUST(watched.load_from_slice(bytes));
    auto const gaps = find_gaps(watched.recorded);
    CHECK_EQ(gaps.size(), 2uz);
    auto const fills = plan_fills(watched.recorded, gaps, friend_run());
    CHECK_EQ(fills.size(), 2uz);
    if (fills.size() != 2) {
        return;
    }
    CHECK(std::abs(fills[0].distance - 303) < 0.01 && std::abs(fills[1].distance - 303) < 0.01);
    auto const patched = FIT::patch_fit(bytes, fills);
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    Activity filled { storage, ActivityID::dummy(ActivityFile::Fit) };
    filled.depth = LoadingDepth::Deep;
    MUST(filled.load_from_slice(*patched));
    auto const &records = filled.recorded;
    CHECK_EQ(records.size(), 500uz);
    if (records.size() != 500) {
        return;
    }
    for (auto ix = 1uz; ix < records.size(); ++ix) {
        if (records[ix].distance && records[ix - 1].distance && *records[ix].distance < *records[ix - 1].distance) {
            CHECK_EQ(std::format("{} m at {} s", *records[ix].distance, ix), std::format("{} m at {} s", *records[ix - 1].distance, ix - 1));
            break;
        }
    }
    CHECK(records[199].distance && std::abs(*records[199].distance - 597.0f) < 0.01f);
    CHECK(records[200].distance && std::abs(*records[200].distance - 600.0f) < 0.01f);
    CHECK(records[299].distance && std::abs(*records[299].distance - 897.0f) < 0.01f);
    CHECK(records[300].distance && std::abs(*records[300].distance - 900.0f) < 0.01f); // The second gap's first
    CHECK(records[399].distance && std::abs(*records[399].distance - 1197.0f) < 0.01f);
    CHECK(records[400].distance && std::abs(*records[400].distance - 1200.0f) < 0.01f);
    CHECK(records[499].distance && std::abs(*records[499].distance - 1497.0f) < 0.01f);
    CHECK(std::abs(filled.segment.distance - 1497.0f) < 0.01f);
}

// The friend's records in the gap without a position or a distance: they
// go in without them, and the file reads.
void test_fill_with_records_missing_values()
{
    auto const records = watch();
    auto       other = friend_run();
    other[200].position.reset();
    other[201].distance.reset();
    auto const fills = plan_fills(records, find_gaps(records), other);
    CHECK_EQ(fills.size(), 1uz);
    if (fills.size() != 1) {
        return;
    }
    auto const &inserted = fills[0].records;
    CHECK_EQ(inserted.size(), 300uz);
    if (inserted.size() != 300) {
        return;
    }
    CHECK(!inserted[100].lat && !inserted[100].lon && inserted[100].distance);
    CHECK(inserted[101].lat && !inserted[101].distance);
    auto const patched = FIT::patch_fit(Test::run(), fills);
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    Storage  storage { };
    Activity filled { storage, ActivityID::dummy(ActivityFile::Fit) };
    filled.depth = LoadingDepth::Deep;
    MUST(filled.load_from_slice(*patched));
    CHECK_EQ(filled.recorded.size(), 500uz);
    if (filled.recorded.size() == 500) {
        CHECK(!filled.recorded[200].position || !filled.recorded[200].position->has_fix());
        CHECK(!filled.recorded[201].distance);
        CHECK(filled.recorded[202].distance && std::abs(*filled.recorded[202].distance - 606.0f) < 0.01f);
    }
}

// A watch whose records have no distance: the friend's file still covers
// the gap, in time and place, so that the user hears why it can't be
// filled ("its records have no distance", from patch_fit()), rather than
// "This activity wasn't where you were when the watch stopped."
void test_plan_fills_watch_without_distance()
{
    Storage    storage { };
    Activity   watched { storage, ActivityID::dummy(ActivityFile::Fit) };
    auto const bytes = Test::run({ .with_distance = false });
    watched.depth = LoadingDepth::Deep;
    MUST(watched.load_from_slice(bytes));
    auto const gaps = find_gaps(watched.recorded);
    CHECK_EQ(gaps.size(), 1uz);
    auto const fills = plan_fills(watched.recorded, gaps, friend_run());
    CHECK_EQ(fills.size(), 1uz);
    auto const patched = FIT::patch_fit(bytes, fills);
    CHECK(!patched && patched.error() == "its records have no distance");
}

void test_fill_question()
{
    auto const         start = DateTime::from_timestamp(static_cast<i32>(unix_time(0)));
    FIT::FITFill const first { .stop = Test::Start + 99, .restart = Test::Start + 400, .distance = 1430 };
    FIT::FITFill const second { .stop = Test::Start + 2465, .restart = Test::Start + 2900, .distance = 1610 };
    CHECK_EQ(fill_question({ first }, start), "Fill the gap: 1.4 km at 0:01:39?");
    CHECK_EQ(fill_question({ first, second }, start), "Fill 2 gaps: 1.4 km at 0:01:39, 1.6 km at 0:41:05?");
}

void test_drop_problem()
{
    CHECK(!drop_problem({ "/tmp/friend.fit" }, true));
    CHECK(drop_problem({ "/tmp/friend.fit" }, false) == "Show the activity to fill, and drop the file on it.");
    CHECK(drop_problem({ "/tmp/a.fit", "/tmp/b.fit" }, true) == "Drop one file at a time.");
}

// The run in a store, filled, filled again, and put back.
void test_fill_in_the_store()
{
    Test::TempStore store;
    auto const      start = DateTime::from_timestamp(static_cast<i32>(unix_time(0)));
    auto const      dir = store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(start.year), static_cast<u8>(start.month) + 1);
    fs::create_directories(dir);
    auto const watch_bytes = Test::run();
    MUST(write_file_by_name((dir / std::format("{}.fit", unix_time(0))).string(), std::string_view { watch_bytes }));
    ActivityID const id { Month { static_cast<size_t>(start.year), start.month }, std::format("{}", unix_time(0)), FileTypes::one(ActivityFile::Fit) };

    Activity watched { store.storage, id };
    CHECK(watched.load(LoadingDepth::Deep).value_or(false));
    auto const fills = plan_fills(watched.recorded, find_gaps(watched.recorded), friend_run());
    CHECK(fill_gaps(watched, fills).has_value());
    CHECK(read_file_by_name(original_path(watched).string()).value_or("") == watch_bytes);

    Activity filled { store.storage, id };
    CHECK(filled.load(LoadingDepth::Deep).value_or(false));
    CHECK_EQ(filled.recorded.size(), 500uz);
    CHECK(std::abs(filled.segment.distance - 1503.0f) < 0.01f);
    CHECK(find_gaps(filled.recorded).empty());

    // A second fill starts from the filled file, and keeps the watch's.
    CHECK(fill_gaps(filled, { }).has_value());
    CHECK(read_file_by_name(original_path(watched).string()).value_or("") == watch_bytes);

    CHECK(remove_fills(filled).has_value());
    CHECK(read_file_by_name(fit_path(watched).string()).value_or("") == watch_bytes);
    CHECK(!fs::exists(original_path(watched)));
    auto const again = remove_fills(filled);
    CHECK(!again && again.error() == "This activity has no fills to remove.");
}

void test_fill_refused()
{
    Test::TempStore  store;
    Test::FITBuilder fit;
    Test::add_file_id(fit, 1);
    Test::add_session(fit, 60, 0); // No records
    auto const start = DateTime::from_timestamp(static_cast<i32>(unix_time(0)));
    auto const dir = store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(start.year), static_cast<u8>(start.month) + 1);
    fs::create_directories(dir);
    MUST(write_file_by_name((dir / std::format("{}.fit", unix_time(0))).string(), std::string_view { fit.file() }));
    Activity activity { store.storage, ActivityID { Month { static_cast<size_t>(start.year), start.month }, std::format("{}", unix_time(0)), FileTypes::one(ActivityFile::Fit) } };
    CHECK(activity.load(LoadingDepth::Deep).value_or(false));
    auto const refused = fill_gaps(activity, { FIT::FITFill { .stop = Test::Start, .restart = Test::Start + 100 } });
    CHECK(!refused && refused.error() == "it can't be filled: its records have no distance");
    CHECK(!fs::exists(original_path(activity)));
}

}

int main()
{
    test_distance_between();
    test_find_gaps();
    test_not_gaps();
    test_plan_fills();
    test_fill_two_gaps();
    test_plan_fills_not_covered();
    test_fill_with_records_missing_values();
    test_fill_question();
    test_drop_problem();
    test_fill_in_the_store();
    test_fill_refused();
    return Test::test_result();
}
