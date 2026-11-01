/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests recognising the recordings of one activity, choosing the main
 * one, and keeping them together in a store.
 */

#include <filesystem>
#include <format>

#include <IO.h>
#include <JSON.h>

#include <storage/Recordings.h>
#include <storage/Storage.h>

#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using FIT::manufacturer;
using FIT::sport;

namespace fs = std::filesystem;

namespace {

constexpr u16 Garmin = 1;
constexpr u16 Wahoo = 32;
constexpr u16 Zwift = 260;

i64 unix_start(u32 offset)
{
    return static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET + offset;
}

// A recording that starts `offset` seconds after Test::Start, in the inbox.
void record(Test::TempStore const &store, u16 device, u32 offset, u32 seconds, u8 sport = 2)
{
    Test::FITBuilder fit;
    Test::add_file_id(fit, device);
    Test::add_session(fit, seconds, 0, Test::NoSubSport, offset, sport);
    MUST(write_file_by_name((store.storage.inbox_dir / std::format("{}-{}.fit", device, offset)).string(), std::string_view { fit.file() }));
}

// The inbox, as the app runs it: grouping what it files.
void file(Test::TempStore &store)
{
    auto const filed = store.storage.process_inbox({ }, [&store](i64 start) {
        CHECK(group_new(store.storage, start).has_value());
    });
    CHECK(filed.has_value());
}

fs::path month_dir(Test::TempStore const &store, u32 offset = 0)
{
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start(offset)));
    return store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1);
}

fs::path fit_of(Test::TempStore const &store, u32 offset)
{
    return month_dir(store, offset) / std::format("{}.fit", unix_start(offset));
}

size_t listed(Test::TempStore const &store)
{
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start(0)));
    return store.storage.month_starts(Month { static_cast<size_t>(date.year), date.month }).value_or(std::vector<StoredStart> { }).size();
}

std::string sidecar_value(fs::path const &fit, std::string_view key)
{
    auto const contents = read_file_by_name(fs::path { fit }.replace_extension(".json").string());
    if (!contents) {
        return "";
    }
    auto const json = JSONValue::deserialize(*contents);
    return json ? json->try_get<std::string>(key).value_or("") : "";
}

RecordingInfo ride(i64 start, u32 elapsed, std::optional<manufacturer> device = { })
{
    return RecordingInfo { .start = start, .elapsed = elapsed, .sport = sport::Cycling, .manufacturer = device };
}

void test_manufacturer_by_name()
{
    CHECK(FIT::manufacturer_by_name("garmin") == manufacturer::garmin);
    CHECK(FIT::manufacturer_by_name("wahoo_fitness") == manufacturer::wahoo_fitness);
    CHECK(!FIT::manufacturer_by_name("Garmin"));
}

void test_same_activity()
{
    // A head unit started a minute late, and stopped with the other.
    CHECK(same_activity(ride(1000, 3600), ride(1060, 3540)));
    // A warm-up of 20 minutes before a race, recorded on its own.
    CHECK(!same_activity(ride(1000, 1200), ride(2300, 3600)));
    // Overlapping, but not enough: 70% of the shorter one.
    CHECK(!same_activity(ride(1000, 1000), ride(1300, 3600)));
    CHECK(same_activity(ride(1000, 1000), ride(1200, 3600)));
    auto run = ride(1000, 3600);
    run.sport = sport::Running;
    CHECK(!same_activity(run, ride(1000, 3600)));
    // A treadmill run is a run.
    auto treadmill = run;
    treadmill.sub_sport = FIT::sub_sport::treadmill;
    CHECK(same_activity(run, treadmill));
    // Sports the totals don't count, by their FIT sport.
    auto swim = ride(1000, 1800);
    swim.sport = sport::Swimming;
    CHECK(same_activity(swim, swim));
    auto hike = swim;
    hike.sport = sport::Hiking;
    CHECK(!same_activity(swim, hike));
    CHECK(!same_activity(ride(1000, 0), ride(1000, 3600)));
}

void test_main_recording()
{
    std::vector<RecordingInfo> const two { ride(1000, 3600, manufacturer::wahoo_fitness), ride(1010, 3500, manufacturer::garmin) };
    CHECK_EQ(main_recording(two, { manufacturer::garmin, manufacturer::wahoo_fitness }), 1uz);
    CHECK_EQ(main_recording(two, { manufacturer::wahoo_fitness }), 0uz);
    // Without a preferred one among them, the longest.
    CHECK_EQ(main_recording(two, { }), 0uz);
    CHECK_EQ(main_recording(two, { manufacturer::zwift }), 0uz);
    // Two from the preferred device: the longer of them.
    std::vector<RecordingInfo> const three { ride(1000, 4000, manufacturer::wahoo_fitness), ride(1000, 3000, manufacturer::garmin), ride(1000, 3500, manufacturer::garmin) };
    CHECK_EQ(main_recording(three, { manufacturer::garmin }), 2uz);
}

// The Wahoo's recording comes in first, the Garmin's a minute later
// starting: the Garmin's is the main one, and the Wahoo's goes into its
// folder, with its name, sidecar and original.
void test_filing_groups()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    file(store);
    CHECK_EQ(listed(store), 1uz);
    MUST(write_file_by_name(fs::path { fit_of(store, 0) }.replace_extension(".json").string(), std::string_view { R"({ "title": "Comparing" })" }));
    MUST(write_file_by_name(fs::path { fit_of(store, 0) }.replace_extension(".fit.original").string(), std::string_view { "the watch's" }));

    record(store, Garmin, 60, 3540);
    file(store);
    CHECK_EQ(listed(store), 1uz);
    auto const main = fit_of(store, 60);
    CHECK(fs::exists(main));
    CHECK(!fs::exists(fit_of(store, 0)));
    auto const folder = recordings_dir(main);
    CHECK(fs::exists(folder / std::format("{}.fit", unix_start(0))));
    CHECK(fs::exists(folder / std::format("{}.json", unix_start(0))));
    CHECK(fs::exists(folder / std::format("{}.fit.original", unix_start(0))));
    CHECK_EQ(sidecar_value(main, "title"), "Comparing");
    CHECK(store.storage.has_activity(unix_start(0)));
    CHECK(store.storage.has_activity(unix_start(60)));
    CHECK(!store.storage.has_activity(unix_start(30)));

    // A third joins them; the Wahoo's again is a duplicate.
    record(store, Zwift, 30, 3500);
    record(store, Wahoo, 0, 3600);
    file(store);
    CHECK_EQ(listed(store), 1uz);
    CHECK_EQ(recordings_of(main).size(), 2uz);
    CHECK(fs::exists(store.storage.inbox_dir / "duplicates"));

    // The activity counts its recordings.
    Activity activity { store.storage, ActivityID { Month { static_cast<size_t>(DateTime::from_timestamp(static_cast<i32>(unix_start(60))).year), DateTime::from_timestamp(static_cast<i32>(unix_start(60))).month }, std::format("{}", unix_start(60)), FileTypes::one(ActivityFile::Fit) } };
    CHECK(activity.load(LoadingDepth::Deep).value_or(false));
    CHECK_EQ(activity.recordings, 3uz);
}

// Without a preferred device, the longest is the main one; a run at the
// same time isn't a recording of the ride.
void test_filing_by_the_longest()
{
    Test::TempStore store;
    record(store, Garmin, 60, 3000);
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 10, 3600, 1); // A run
    file(store);
    CHECK_EQ(listed(store), 2uz);
    CHECK(fs::exists(fit_of(store, 0)));
    CHECK_EQ(recordings_of(fit_of(store, 0)).size(), 1uz);
}

void test_split()
{
    Test::TempStore store;
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const main = fit_of(store, 0);
    CHECK_EQ(listed(store), 1uz);
    auto const split = split_recordings(store.storage, main);
    CHECK(split.has_value() && *split == 1);
    CHECK_EQ(listed(store), 2uz);
    CHECK(!fs::exists(recordings_dir(main)));
    CHECK_EQ(sidecar_value(main, "recordings"), "separate");
    CHECK_EQ(sidecar_value(fit_of(store, 60), "recordings"), "separate");
    // One more that overlaps both isn't grouped with them.
    record(store, Zwift, 30, 3500);
    file(store);
    CHECK_EQ(listed(store), 3uz);
    // Splitting an activity of one recording moves nothing.
    auto const none = split_recordings(store.storage, fit_of(store, 30));
    CHECK(none.has_value() && *none == 0);
}

// A store filed before there were groups, once over.
void test_group_month()
{
    Test::TempStore store;
    for (auto const [device, offset, seconds] : { std::tuple { Wahoo, 0u, 3600u }, std::tuple { Garmin, 60u, 3540u }, std::tuple { Garmin, 86400u, 1800u } }) {
        Test::FITBuilder fit;
        Test::add_file_id(fit, device);
        Test::add_session(fit, seconds, 0, Test::NoSubSport, offset);
        fs::create_directories(month_dir(store, offset));
        MUST(write_file_by_name(fit_of(store, offset).string(), std::string_view { fit.file() }));
    }
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start(0)));
    auto const groups = group_month(store.storage, Month { static_cast<size_t>(date.year), date.month });
    CHECK(groups.has_value());
    if (groups) {
        CHECK_EQ(groups->size(), 1uz);
        if (!groups->empty()) {
            CHECK_EQ((*groups)[0].hidden.size(), 1uz);
        }
    }
    CHECK_EQ(listed(store), 2uz);
}

// A recording whose main one is filed in the next month.
void test_has_activity_across_months()
{
    Test::TempStore store;
    auto const      date = DateTime::from_timestamp(static_cast<i32>(unix_start(0)));
    auto const      next = Month { static_cast<size_t>(date.year), date.month }.plus(1);
    auto const      folder = store.storage.activity_dir / next.dir_name() / "1700000000.recordings";
    fs::create_directories(folder);
    MUST(write_file_by_name((folder / std::format("{}.fit", unix_start(0))).string(), std::string_view { "a recording" }));
    CHECK(store.storage.has_activity(unix_start(0)));
}

}

int main()
{
    test_manufacturer_by_name();
    test_same_activity();
    test_main_recording();
    test_filing_groups();
    test_filing_by_the_longest();
    test_split();
    test_group_month();
    test_has_activity_across_months();
    return Test::test_result();
}
