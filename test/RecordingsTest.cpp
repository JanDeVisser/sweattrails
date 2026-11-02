/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests recognising the recordings of one activity, choosing the main
 * one, and keeping them together in a store.
 */

#include <algorithm>
#include <ctime>
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

void test_decode_prefer()
{
    CHECK(decode_prefer({ }).value_or(std::vector<manufacturer> { manufacturer::zwift }).empty());
    auto const prefer = decode_prefer(JSONValue::deserialize(R"({ "prefer": ["garmin", "wahoo_fitness"] })").value());
    CHECK(prefer.has_value() && *prefer == (std::vector { manufacturer::garmin, manufacturer::wahoo_fitness }));
    auto const unknown = decode_prefer(JSONValue::deserialize(R"({ "prefer": ["garmin", "acme"] })").value());
    CHECK(!unknown.has_value() && unknown.error().contains("acme"));
    CHECK(!decode_prefer(JSONValue::deserialize(R"({ "prefer": "garmin" })").value()).has_value());
    CHECK(!decode_prefer(JSONValue::deserialize(R"(["garmin"])").value()).has_value());
}

void test_activity_id_of()
{
    auto const id = activity_id_of("/store/activity/2026/03/1773000000.fit");
    CHECK(id.has_value());
    if (id) {
        CHECK_EQ(id->month.year, 2026uz);
        CHECK(id->month.month == DateTime::Month::March);
        CHECK_EQ(id->name, "1773000000");
    }
    CHECK(!activity_id_of("/store/activity/2026/13/1773000000.fit"));
    // A recording in a folder isn't listed.
    CHECK(!activity_id_of("/store/activity/2026/03/1773000000.recordings/1773000060.fit"));
    CHECK(!activity_id_of("/store/activity/2026/03/1773000000.json"));
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

// Reviewer's tests, below.

// A recording written straight into `dir`, as a store filed before this
// feature, or in another time zone, has it.
fs::path put(fs::path const &dir, u16 device, u32 offset, u32 seconds)
{
    Test::FITBuilder fit;
    Test::add_file_id(fit, device);
    Test::add_session(fit, seconds, 0, Test::NoSubSport, offset);
    fs::create_directories(dir);
    auto const path = dir / std::format("{}.fit", unix_start(offset));
    MUST(write_file_by_name(path.string(), std::string_view { fit.file() }));
    return path;
}

Month month_of(u32 offset)
{
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start(offset)));
    return Month { static_cast<size_t>(date.year), date.month };
}

// The offset from Test::Start of a local time.
u32 local_offset(int year, int month, int day, int hour, int minute)
{
    std::tm tm { };
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_isdst = -1;
    return static_cast<u32>(static_cast<i64>(std::mktime(&tm)) - unix_start(0));
}

// Review note 1: two recordings filed in another time zone, in the month
// after the one the inbox would file them in now. The once over the store
// should group them.
void test_group_month_filed_elsewhere()
{
    Test::TempStore store;
    auto const      dir = store.storage.activity_dir / month_of(0).plus(1).dir_name();
    put(dir, Wahoo, 0, 3600);
    put(dir, Garmin, 60, 3540);
    auto const groups = group_month(store.storage, month_of(0).plus(1));
    CHECK(groups.has_value());
    CHECK_EQ(groups.value_or(std::vector<Grouped> { }).size(), 1uz);
    CHECK_EQ(store.storage.month_starts(month_of(0).plus(1)).value_or(std::vector<StoredStart> { }).size(), 1uz);
}

// A recording from the inbox whose candidate was filed in another time
// zone, in the month after: it joins it there.
void test_inbox_candidate_filed_elsewhere()
{
    Test::TempStore store;
    auto const      dir = store.storage.activity_dir / month_of(0).plus(1).dir_name();
    auto const      main = put(dir, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    CHECK(fs::exists(main));
    CHECK(fs::exists(recordings_dir(main) / std::format("{}.fit", unix_start(60))));
    CHECK_EQ(listed(store), 0uz);
}

// A preferred recording comes in for a group that has one already: the
// former main one and its folder's recordings go into the new one's
// folder, the former's folder goes, and its name moves.
void test_former_main_with_folder()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Zwift, 30, 3500);
    file(store);
    auto const former = fit_of(store, 0);
    CHECK_EQ(recordings_of(former).size(), 1uz);
    MUST(write_file_by_name(fs::path { former }.replace_extension(".json").string(), std::string_view { R"({ "title": "Comparing" })" }));

    record(store, Garmin, 60, 3540);
    std::vector<Grouped> groups;
    auto const           filed = store.storage.process_inbox({ }, [&store, &groups](i64 start) {
        if (auto grouped = group_new(store.storage, start); grouped && *grouped) {
            groups.push_back(**grouped);
        }
    });
    CHECK(filed.has_value());
    auto const main = fit_of(store, 60);
    CHECK_EQ(listed(store), 1uz);
    CHECK(fs::exists(main));
    CHECK(!fs::exists(recordings_dir(former)));
    CHECK_EQ(recordings_of(main).size(), 2uz);
    CHECK(fs::exists(recordings_dir(main) / std::format("{}.fit", unix_start(0))));
    CHECK(fs::exists(recordings_dir(main) / std::format("{}.fit", unix_start(30))));
    CHECK_EQ(sidecar_value(main, "title"), "Comparing");
    CHECK_EQ(groups.size(), 1uz);
    if (!groups.empty()) {
        CHECK_EQ(groups[0].main.string(), main.string());
        // The Wahoo's was listed, and the Garmin's for a moment; not the Zwift's.
        CHECK(std::ranges::contains(groups[0].hidden, unix_start(0)));
        CHECK(!std::ranges::contains(groups[0].hidden, unix_start(30)));
        CHECK(!std::ranges::contains(groups[0].hidden, unix_start(60)));
    }
}

// The new main one comes from a former main one's folder: it's listed
// again, where the inbox would file it, with its sidecar.
void test_main_from_folder()
{
    Test::TempStore store;
    record(store, Wahoo, 0, 3600);
    record(store, Zwift, 30, 3500);
    file(store);
    auto const former = fit_of(store, 0);
    MUST(write_file_by_name((recordings_dir(former) / std::format("{}.json", unix_start(30))).string(), std::string_view { R"({ "laps": [] })" }));
    store.storage.prefer = { manufacturer::zwift };
    record(store, Garmin, 60, 3540);
    file(store);
    auto const main = fit_of(store, 30);
    CHECK_EQ(listed(store), 1uz);
    CHECK(fs::exists(main));
    CHECK(fs::exists(fs::path { main }.replace_extension(".json")));
    CHECK(!fs::exists(recordings_dir(former)));
    CHECK_EQ(recordings_of(main).size(), 2uz);
    Activity activity { store.storage, *activity_id_of(main) };
    CHECK(activity.load(LoadingDepth::Deep).value_or(false));
    CHECK_EQ(activity.recordings, 3uz);
}

// A new main one with a name of its own keeps it.
void test_title_of_its_own()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    file(store);
    MUST(write_file_by_name(fs::path { fit_of(store, 0) }.replace_extension(".json").string(), std::string_view { R"({ "title": "Wahoo's" })" }));
    record(store, Garmin, 60, 3540);
    MUST(write_file_by_name((store.storage.inbox_dir / std::format("{}-{}.json", Garmin, 60)).string(), std::string_view { R"({ "title": "Garmin's" })" }));
    file(store);
    CHECK_EQ(sidecar_value(fit_of(store, 60), "title"), "Garmin's");
    CHECK_EQ(sidecar_value(recordings_dir(fit_of(store, 60)) / std::format("{}.fit", unix_start(0)), "title"), "Wahoo's");
}

// Review note 2: the old app's `.strava` file is the recording's too, and
// goes with it.
void test_strava_file_moves_along()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    file(store);
    MUST(write_file_by_name(fs::path { fit_of(store, 0) }.replace_extension(".strava").string(), std::string_view { R"({ "name": "Old app's" })" }));
    record(store, Garmin, 60, 3540);
    file(store);
    CHECK(!fs::exists(fs::path { fit_of(store, 0) }.replace_extension(".strava")));
    CHECK(fs::exists(recordings_dir(fit_of(store, 60)) / std::format("{}.strava", unix_start(0))));
}

// One recording starts just before midnight at the end of a month, the
// other just after: grouped in the first month, found there from the
// second, a duplicate in the inbox, and split back into the second.
void test_across_month_end()
{
    Test::TempStore store;
    auto const      before = local_offset(2021, 9, 30, 23, 50);
    auto const      after = local_offset(2021, 10, 1, 0, 1);
    CHECK(month_of(before) != month_of(after));
    record(store, Wahoo, before, 3600);
    record(store, Garmin, after, 3000);
    file(store);
    auto const main = fit_of(store, before);
    CHECK(fs::exists(main));
    CHECK(fs::exists(recordings_dir(main) / std::format("{}.fit", unix_start(after))));
    CHECK(!fs::exists(fit_of(store, after)));
    CHECK(store.storage.has_activity(unix_start(after)));

    record(store, Garmin, after, 3000);
    file(store);
    CHECK(fs::exists(store.storage.inbox_dir / "duplicates"));
    CHECK(!fs::exists(fit_of(store, after)));

    auto const split = split_recordings(store.storage, main);
    CHECK(split.has_value() && *split == 1);
    CHECK(fs::exists(fit_of(store, after)));
    CHECK_EQ(sidecar_value(fit_of(store, after), "recordings"), "separate");
    CHECK(store.storage.has_activity(unix_start(after)));
}

// Splitting a recording whose place is taken fails, and doesn't overwrite
// what's there; the recording stays in the folder.
void test_split_place_taken()
{
    Test::TempStore store;
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const main = fit_of(store, 0);
    MUST(write_file_by_name(fit_of(store, 60).string(), std::string_view { "in the way" }));
    auto const split = split_recordings(store.storage, main);
    CHECK(!split.has_value());
    CHECK_EQ(read_file_by_name(fit_of(store, 60).string()).value_or(""), "in the way");
    CHECK(fs::exists(recordings_dir(main) / std::format("{}.fit", unix_start(60))));
}

// The once over the store, run again, groups nothing more.
void test_group_month_twice()
{
    Test::TempStore store;
    put(month_dir(store), Wahoo, 0, 3600);
    put(month_dir(store), Garmin, 60, 3540);
    put(month_dir(store), Zwift, 30, 3500);
    auto const first = group_month(store.storage, month_of(0));
    CHECK_EQ(first.value_or(std::vector<Grouped> { }).size(), 1uz);
    CHECK_EQ(listed(store), 1uz);
    CHECK_EQ(recordings_of(fit_of(store, 0)).size(), 2uz);
    auto const second = group_month(store.storage, month_of(0));
    CHECK(second.has_value());
    CHECK_EQ(second.value_or(std::vector<Grouped> { { } }).size(), 0uz);
    CHECK_EQ(recordings_of(fit_of(store, 0)).size(), 2uz);
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
    test_decode_prefer();
    test_activity_id_of();
    test_inbox_candidate_filed_elsewhere();
    test_former_main_with_folder();
    test_main_from_folder();
    test_title_of_its_own();
    test_across_month_end();
    test_split_place_taken();
    test_group_month_twice();
    return Test::test_result();
}
