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
#include <storage/Summaries.h>

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
    auto const filed = store.storage.process_inbox({ }, [&store](std::string_view, Storage::InboxFiling const &filing) {
        if (filing.filed()) {
            CHECK(group_new(store.storage, filing.start).has_value());
        }
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

void test_same_recording()
{
    // The second of each pair starts 10 s after the first.
    auto const with = [](u32 serial, u32 elapsed, float32 distance, i64 start = 1000) {
        auto ret = ride(start, elapsed);
        ret.serial = serial;
        ret.distance = distance;
        return ret;
    };
    auto const later = [&with](u32 serial, u32 elapsed, float32 distance) { return with(serial, elapsed, distance, 1010); };
    // By the serial number, when both have one, whatever the totals.
    CHECK(same_recording(with(7, 3600, 30000), later(7, 3000, 0)));
    CHECK(!same_recording(with(7, 3600, 30000), later(8, 3600, 30000)));
    // Else by the totals: Garmin Connect's copy of Zwift's file.
    CHECK(same_recording(with(0, 3600, 30000), later(7, 3605, 30150)));
    CHECK(!same_recording(with(0, 3600, 30000), later(0, 3606, 30000)));
    CHECK(!same_recording(with(0, 3600, 30000), later(0, 3600, 30151)));
    // A head unit indoors has no distance, and Zwift's file has.
    CHECK(!same_recording(with(0, 3600, 0), later(7, 3600, 30000)));
    CHECK(same_recording(with(0, 3600, 0), later(0, 3600, 0)));
    // A rewrite in the same second, which counts the pauses otherwise,
    // whatever the serials.
    CHECK(same_recording(with(0, 2094, 4862), with(7, 2252, 6175)));
    CHECK(same_recording(with(8, 2094, 4862), with(7, 2252, 6175)));
    CHECK(!same_recording(with(0, 2094, 4862), later(7, 2252, 6175)));
}

void test_recording_info_from_memory()
{
    Test::TempStore store { };
    auto const      info = recording_info(store.storage, std::string_view { Test::recording_fit({ .serial = 1234, .start = 60, .count = 900, .distance = 7500 }) });
    CHECK(info.has_value());
    if (!info) {
        return;
    }
    CHECK(info->fit.empty());
    CHECK_EQ(info->serial, 1234u);
    CHECK_EQ(info->elapsed, 900u);
    CHECK_EQ(info->distance, 7500.0f);
    CHECK_EQ(info->start, unix_start(60));
    CHECK(!recording_info(store.storage, std::string_view { "not a FIT file" }).has_value());
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
    // One chosen by the user, before the preferred devices and the longest.
    auto chosen = three;
    chosen[0].chosen = true;
    CHECK_EQ(main_recording(chosen, { manufacturer::garmin }), 0uz);
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
    // A stub it replaced (specs/50-stub-activities.md) goes along too.
    MUST(write_file_by_name(fs::path { fit_of(store, 0) }.replace_extension(".fit.stub").string(), std::string_view { "a summary" }));

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
    CHECK(fs::exists(folder / std::format("{}.fit.stub", unix_start(0))));
    CHECK(!fs::exists(fs::path { fit_of(store, 0) }.replace_extension(".fit.stub")));
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

// Asked to split an activity that a new recording has since made one of
// its others: it says so, and moves nothing (review finding 4).
void test_split_after_another_became_main()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Zwift, 30, 3500);
    file(store);
    auto const asked = fit_of(store, 0);
    CHECK_EQ(recordings_of(asked).size(), 1uz);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const split = split_recordings(store.storage, asked);
    CHECK(!split.has_value());
    CHECK_EQ(recordings_of(fit_of(store, 60)).size(), 2uz);
    CHECK_EQ(listed(store), 1uz);
}

// Split, then grouped again with Ctrl+J with the other one as the main
// one: it stays the main one when a preferred recording comes in.
void test_group_with_main()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    CHECK(split_recordings(store.storage, fit_of(store, 60)).has_value());
    CHECK_EQ(listed(store), 2uz);

    auto const wahoo = fit_of(store, 0);
    auto const to_group = recordings_to_group(store.storage, wahoo);
    CHECK(to_group.has_value() && to_group->size() == 1);
    auto const grouped = group_with_main(store.storage, wahoo);
    CHECK(grouped.has_value());
    if (grouped) {
        CHECK(grouped->main == wahoo);
        CHECK(grouped->hidden == std::vector { unix_start(60) });
    }
    CHECK_EQ(listed(store), 1uz);
    CHECK_EQ(sidecar_value(wahoo, "recordings"), "main");
    auto const garmin = recordings_dir(wahoo) / std::format("{}.fit", unix_start(60));
    CHECK(fs::exists(garmin));
    CHECK_EQ(sidecar_value(garmin, "recordings"), "");

    // A Garmin recording that comes in later joins without taking its
    // place.
    record(store, Garmin, 30, 3500);
    file(store);
    CHECK_EQ(listed(store), 1uz);
    CHECK(fs::exists(wahoo));
    CHECK_EQ(recordings_of(wahoo).size(), 2uz);

    // Nothing left to group: its own recordings are already with it.
    auto const again = recordings_to_group(store.storage, wahoo);
    CHECK(again.has_value() && again->size() == 2);
    CHECK(group_with_main(store.storage, wahoo).has_value());
    CHECK_EQ(recordings_of(wahoo).size(), 2uz);
    CHECK_EQ(listed(store), 1uz);
}

// Ctrl+J on an activity nothing else is a recording of, or on one in a
// folder.
void test_group_with_main_nothing_to_group()
{
    Test::TempStore store;
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 10, 3600, 1); // A run
    file(store);
    auto const to_group = recordings_to_group(store.storage, fit_of(store, 0));
    CHECK(to_group.has_value() && to_group->empty());
    auto const grouped = group_with_main(store.storage, fit_of(store, 0));
    CHECK(grouped.has_value() && grouped->hidden.empty());
    CHECK_EQ(sidecar_value(fit_of(store, 0), "recordings"), "");
    CHECK_EQ(listed(store), 2uz);

    record(store, Zwift, 30, 3500);
    file(store);
    auto const in_folder = recordings_dir(fit_of(store, 0)) / std::format("{}.fit", unix_start(30));
    CHECK(fs::exists(in_folder));
    CHECK(!recordings_to_group(store.storage, in_folder).has_value());
}

// Ctrl+J on a recording that was split off, with a group of two others
// listed next to it, whose main one was chosen with Ctrl+J: the group's
// main one and its folder's recording join this one's folder, the group's
// folder goes, its name moves, and it's no longer marked main.
void test_group_with_main_takes_a_group()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    CHECK(split_recordings(store.storage, fit_of(store, 60)).has_value());
    // Two more come in: they only match each other, as the first two are
    // separate.
    record(store, Zwift, 30, 3500);
    record(store, Wahoo, 45, 3400);
    file(store);
    CHECK_EQ(listed(store), 3uz);
    auto const group = fit_of(store, 30);
    CHECK_EQ(recordings_of(group).size(), 1uz);
    MUST(write_file_by_name(fs::path { group }.replace_extension(".json").string(), std::string_view { R"({ "title": "Comparing", "recordings": "main" })" }));

    auto const wahoo = fit_of(store, 0);
    auto const to_group = recordings_to_group(store.storage, wahoo);
    CHECK(to_group.has_value() && to_group->size() == 3);
    auto const grouped = group_with_main(store.storage, wahoo);
    CHECK(grouped.has_value());
    if (grouped) {
        CHECK(grouped->main == wahoo);
        CHECK(std::ranges::contains(grouped->hidden, unix_start(60)));
        CHECK(std::ranges::contains(grouped->hidden, unix_start(30)));
        CHECK(!std::ranges::contains(grouped->hidden, unix_start(45)));
    }
    CHECK_EQ(listed(store), 1uz);
    CHECK(!fs::exists(recordings_dir(group)));
    CHECK_EQ(recordings_of(wahoo).size(), 3uz);
    CHECK_EQ(sidecar_value(wahoo, "recordings"), "main");
    CHECK_EQ(sidecar_value(wahoo, "title"), "Comparing");
    for (auto const &recording : recordings_of(wahoo)) {
        CHECK_EQ(sidecar_value(recording, "recordings"), "");
    }
    Activity activity { store.storage, *activity_id_of(wahoo) };
    CHECK(activity.load(LoadingDepth::Deep).value_or(false));
    CHECK_EQ(activity.recordings, 4uz);
}

// Ctrl+J on the one that was the main one before the split: grouped as
// it was, and now chosen.
void test_group_with_main_former_main()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const garmin = fit_of(store, 60);
    CHECK(split_recordings(store.storage, garmin).has_value());
    CHECK(group_with_main(store.storage, garmin).has_value());
    CHECK_EQ(listed(store), 1uz);
    CHECK_EQ(sidecar_value(garmin, "recordings"), "main");
    CHECK_EQ(sidecar_value(recordings_dir(garmin) / std::format("{}.fit", unix_start(0)), "recordings"), "");
}

// A group made with Ctrl+J split again: the chosen one is separate, not
// main, and a recording that comes in after isn't grouped with either.
void test_split_after_group_with_main()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    CHECK(split_recordings(store.storage, fit_of(store, 60)).has_value());
    auto const wahoo = fit_of(store, 0);
    CHECK(group_with_main(store.storage, wahoo).has_value());
    auto const split = split_recordings(store.storage, wahoo);
    CHECK(split.has_value() && *split == 1);
    CHECK_EQ(listed(store), 2uz);
    CHECK_EQ(sidecar_value(wahoo, "recordings"), "separate");
    CHECK_EQ(sidecar_value(fit_of(store, 60), "recordings"), "separate");
    record(store, Garmin, 30, 3500);
    file(store);
    CHECK_EQ(listed(store), 3uz);
}

// Review note Ctrl+J 1: Ctrl+J that fails, as the main one's sidecar
// can't be read, reports an error, but has cleared the other one's
// "separate" already, so the next recording to come in groups it again.
void test_group_with_main_fails_unchanged()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    CHECK(split_recordings(store.storage, fit_of(store, 60)).has_value());
    auto const wahoo = fit_of(store, 0);
    MUST(write_file_by_name(fs::path { wahoo }.replace_extension(".json").string(), std::string_view { "not JSON" }));
    CHECK(!group_with_main(store.storage, wahoo).has_value());
    CHECK_EQ(listed(store), 2uz);
    // Fails now: the Garmin's sidecar lost its mark.
    CHECK_EQ(sidecar_value(fit_of(store, 60), "recordings"), "separate");
}

ActivityID id_of(Test::TempStore const &store, u32 offset)
{
    return activity_id_of(fit_of(store, offset)).value_or(ActivityID { });
}

std::string file_contents(fs::path const &path)
{
    return read_file_by_name(path.string()).value_or("");
}

// Tab's order: the main one, then the others by start.
void test_recordings_in_order()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 30, 3600);
    record(store, Zwift, 0, 3500);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const order = recordings_in_order(store.storage, id_of(store, 60));
    CHECK_EQ(order.size(), 3uz);
    if (order.size() == 3) {
        CHECK(order[0] == id_of(store, 60));
        CHECK_EQ(order[1].recording, std::format("{}", unix_start(0)));
        CHECK_EQ(order[2].recording, std::format("{}", unix_start(30)));
        CHECK_EQ(order[2].name, id_of(store, 60).name);
        // The same from a recording.
        CHECK(recordings_in_order(store.storage, order[2]) == order);
    }

    Test::TempStore one;
    record(one, Wahoo, 0, 3600);
    file(one);
    CHECK(recordings_in_order(one.storage, id_of(one, 0)) == std::vector { id_of(one, 0) });
}

void test_main_of()
{
    CHECK(main_of("/a/2026/03/1773000000.fit") == fs::path { "/a/2026/03/1773000000.fit" });
    CHECK(main_of("/a/2026/03/1773000000.recordings/1773000060.fit") == fs::path { "/a/2026/03/1773000000.fit" });
}

// A recording loads from the folder, with its own sidecar for its laps,
// and the activity's title, which F2 changes in the main one's sidecar.
void test_load_a_recording()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const main = fit_of(store, 60);
    MUST(write_file_by_name(fs::path { main }.replace_extension(".json").string(), std::string_view { R"({ "title": "Comparing" })" }));
    auto id = id_of(store, 60);
    id.recording = std::format("{}", unix_start(0));
    auto const loaded = id.load(store.storage, LoadingDepth::Deep);
    CHECK(loaded.has_value() && loaded->has_value());
    if (!loaded || !*loaded) {
        return;
    }
    auto       activity = **loaded;
    auto const recording = recordings_dir(main) / std::format("{}.fit", unix_start(0));
    CHECK(activity.sidecar == fs::path { recording }.replace_extension(".json"));
    CHECK(activity.activity_sidecar == fs::path { main }.replace_extension(".json"));
    CHECK_EQ(activity.title, "Comparing");
    CHECK(activity.manufacturer == manufacturer::wahoo_fitness);
    CHECK_EQ(activity.segment.start_time.timestamp, unix_start(0));
    CHECK_EQ(activity.recordings, 2uz);
    CHECK_EQ(activity.recording_number, 2uz);

    CHECK(activity.set_lap_starts(std::vector<i64> { unix_start(600) }).has_value());
    CHECK(file_contents(activity.sidecar).contains("laps"));
    CHECK(!file_contents(activity.activity_sidecar).contains("laps"));
    CHECK(activity.rename("Two power meters").has_value());
    CHECK_EQ(sidecar_value(main, "title"), "Two power meters");
    CHECK_EQ(sidecar_value(recording, "title"), "");

    auto const listed = id_of(store, 60).load(store.storage, LoadingDepth::Deep);
    CHECK(listed.has_value() && listed->has_value() && (*listed)->recording_number == 1);
}

// Ctrl+J on the Wahoo's recording of a group the Garmin's is the main one
// of: the Wahoo's is listed and stays the main one, with the activity's
// title, and the Garmin's and the Zwift's are in its folder.
void test_make_main()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Zwift, 30, 3500);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const garmin = fit_of(store, 60);
    MUST(write_file_by_name(fs::path { garmin }.replace_extension(".json").string(), std::string_view { R"({ "title": "Comparing" })" }));
    auto const recording = recordings_dir(garmin) / std::format("{}.fit", unix_start(0));
    CHECK(!make_main(store.storage, garmin).has_value());

    auto const made = make_main(store.storage, recording);
    CHECK(made.has_value());
    auto const wahoo = fit_of(store, 0);
    if (made) {
        CHECK(made->main == wahoo);
        CHECK(made->hidden == std::vector { unix_start(60) });
    }
    CHECK_EQ(listed(store), 1uz);
    CHECK(fs::exists(wahoo));
    CHECK(!fs::exists(recordings_dir(garmin)));
    auto const others = recordings_of(wahoo);
    CHECK(others == (std::vector { recordings_dir(wahoo) / std::format("{}.fit", unix_start(30)), recordings_dir(wahoo) / std::format("{}.fit", unix_start(60)) }));
    CHECK_EQ(sidecar_value(wahoo, "recordings"), "main");
    CHECK_EQ(sidecar_value(wahoo, "title"), "Comparing");

    // A preferred recording coming in after it joins without taking its
    // place.
    record(store, Garmin, 45, 3500);
    file(store);
    CHECK_EQ(listed(store), 1uz);
    CHECK(fs::exists(wahoo));
    CHECK_EQ(recordings_of(wahoo).size(), 3uz);
}

// Ctrl+Shift+R with the Wahoo's recording on display splits the group its
// main one heads, and the Wahoo's is then listed where the inbox files it.
void test_split_from_a_recording()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const recording = recordings_dir(fit_of(store, 60)) / std::format("{}.fit", unix_start(0));
    auto const split = split_recordings(store.storage, main_of(recording));
    CHECK(split.has_value() && *split == 1);
    CHECK_EQ(listed(store), 2uz);
    auto const found = store.storage.activity_starts(unix_start(0), unix_start(0));
    CHECK(found.has_value() && found->size() == 1);
    if (found && found->size() == 1) {
        CHECK(fs::path { found->front().sidecar }.replace_extension(".fit") == fit_of(store, 0));
    }
}

// The summary file, brought up to date as the app does after grouping or
// splitting.
ActivitySummaries totals(Test::TempStore &store)
{
    while (true) {
        auto done = update_summaries(store.storage);
        CHECK(done.error.empty());
        if (done.left == 0 || !done.error.empty()) {
            return done.summaries;
        }
    }
}

std::vector<i64> starts(ActivitySummaries const &summaries)
{
    std::vector<i64> ret;
    for (auto const &summary : summaries) {
        ret.push_back(summary.start);
    }
    return ret;
}

// The totals count an activity recorded twice once, by its main
// recording, whichever that is, and twice again once it's split.
void test_totals_follow_the_groups()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    file(store);
    CHECK(starts(totals(store)) == std::vector { unix_start(0) });

    record(store, Garmin, 60, 3540);
    file(store);
    CHECK(starts(totals(store)) == std::vector { unix_start(60) });

    // Ctrl+J on the Wahoo's recording.
    auto const wahoo = recordings_dir(fit_of(store, 60)) / std::format("{}.fit", unix_start(0));
    CHECK(make_main(store.storage, wahoo).has_value());
    CHECK(starts(totals(store)) == std::vector { unix_start(0) });

    // Ctrl+Shift+R.
    CHECK(split_recordings(store.storage, fit_of(store, 0)).has_value());
    CHECK(starts(totals(store)) == (std::vector { unix_start(0), unix_start(60) }));
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

// An activity filed in the next month, in another time zone, is found:
// it isn't downloaded and filed again (review finding 6).
void test_has_activity_filed_elsewhere()
{
    Test::TempStore store;
    auto const      date = DateTime::from_timestamp(static_cast<i32>(unix_start(0)));
    auto const      next = store.storage.activity_dir / Month { static_cast<size_t>(date.year), date.month }.plus(1).dir_name();
    fs::create_directories(next);
    MUST(write_file_by_name((next / std::format("{}.fit", unix_start(0))).string(), std::string_view { "an activity" }));
    CHECK(store.storage.has_activity(unix_start(0)));
    CHECK(!store.storage.has_activity(unix_start(1)));
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
    auto const           filed = store.storage.process_inbox({ }, [&store, &groups](std::string_view, Storage::InboxFiling const &filing) {
        if (!filing.filed()) {
            return;
        }
        if (auto grouped = group_new(store.storage, filing.start); grouped && *grouped) {
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

// A recording made with a workout on the device, which names it, in the
// inbox.
void record_workout(Test::TempStore const &store, u16 device, u32 offset, u32 seconds, std::string_view workout)
{
    Test::FITBuilder fit;
    Test::add_file_id(fit, device);
    Test::add_workout(fit, workout);
    Test::add_session(fit, seconds, 0, Test::NoSubSport, offset);
    MUST(write_file_by_name((store.storage.inbox_dir / std::format("{}-{}.fit", device, offset)).string(), std::string_view { fit.file() }));
}

std::optional<Activity> load_deep(Test::TempStore &store, ActivityID const &id)
{
    auto const loaded = id.load(store.storage, LoadingDepth::Deep);
    if (!loaded || !*loaded) {
        return { };
    }
    return **loaded;
}

ActivityID recording_id(Test::TempStore const &store, u32 main_offset, u32 offset)
{
    auto ret = id_of(store, main_offset);
    ret.recording = std::format("{}", unix_start(offset));
    return ret;
}

// Review note 1: the title is the main one's on every recording (spec 32,
// "Which recording is shown"). Without a name from the user or a service,
// it's the FIT file's, and the recording's FIT file is read for it: a
// workout's name on one device and not the other changes the title on
// Tab. Fails now.
void test_recording_has_the_activitys_title()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record_workout(store, Wahoo, 0, 3600, "Sweet Spot");
    record(store, Garmin, 60, 3540);
    file(store);
    auto const main = load_deep(store, id_of(store, 60));
    auto const wahoo = load_deep(store, recording_id(store, 60, 0));
    CHECK(main.has_value() && wahoo.has_value());
    if (main && wahoo) {
        CHECK_EQ(wahoo->title, main->title);
    }

    // And the other way around: the main one's workout names the activity.
    Test::TempStore other;
    other.storage.prefer = { manufacturer::garmin };
    record(other, Wahoo, 0, 3600);
    record_workout(other, Garmin, 60, 3540, "Sweet Spot");
    file(other);
    auto const named = load_deep(other, recording_id(other, 60, 0));
    CHECK(named.has_value());
    if (named) {
        CHECK_EQ(named->title, "Sweet Spot");
    }
}

// The number the summary line says is the place in Tab's order, for each
// of three recordings.
void test_recording_numbers_follow_tabs_order()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 30, 3600);
    record(store, Zwift, 0, 3500);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const order = recordings_in_order(store.storage, id_of(store, 60));
    CHECK_EQ(order.size(), 3uz);
    for (auto ix = 0uz; ix < order.size(); ++ix) {
        auto const loaded = load_deep(store, order[ix]);
        CHECK(loaded.has_value());
        if (loaded) {
            CHECK_EQ(loaded->recording_number, ix + 1);
            CHECK_EQ(loaded->recordings, 3uz);
            CHECK(loaded->id == order[ix]);
        }
    }
}

// A recording that isn't there any more, as after a split: it doesn't load,
// and doesn't load as the main one either.
void test_load_a_gone_recording()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const gone = recording_id(store, 60, 0);
    CHECK(split_recordings(store.storage, fit_of(store, 60)).has_value());
    auto const loaded = gone.load(store.storage, LoadingDepth::Deep);
    CHECK(!loaded.has_value() || !loaded->has_value());
    CHECK(recordings_in_order(store.storage, gone) == std::vector { id_of(store, 60) });
}

// Ctrl+J on the Garmin's earlier, then on the Wahoo's recording: the
// Garmin's "main" goes, the Wahoo's laps go with it, and each loads with
// its new number.
void test_make_main_after_ctrl_j()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const garmin = fit_of(store, 60);
    CHECK(group_with_main(store.storage, garmin).has_value());
    CHECK_EQ(sidecar_value(garmin, "recordings"), "main");
    auto before = load_deep(store, recording_id(store, 60, 0));
    CHECK(before.has_value());
    if (before) {
        CHECK(before->set_lap_starts(std::vector<i64> { unix_start(600) }).has_value());
    }

    CHECK(make_main(store.storage, recordings_dir(garmin) / std::format("{}.fit", unix_start(0))).has_value());
    auto const wahoo = fit_of(store, 0);
    auto const moved = recordings_dir(wahoo) / std::format("{}.fit", unix_start(60));
    CHECK(fs::exists(moved));
    CHECK_EQ(sidecar_value(moved, "recordings"), "");
    CHECK_EQ(sidecar_value(wahoo, "recordings"), "main");
    auto const main = load_deep(store, id_of(store, 0));
    CHECK(main.has_value());
    if (main) {
        CHECK_EQ(main->recording_number, 1uz);
        CHECK_EQ(main->recordings, 2uz);
        CHECK(main->user_lap_starts == std::optional { std::vector<i64> { unix_start(600) } });
    }
    auto const former = load_deep(store, recording_id(store, 0, 60));
    CHECK(former.has_value() && former->recording_number == 2);
}

// A recording with a title of its own keeps it when it becomes the main
// one (spec 31, "The name goes with the activity").
void test_make_main_keeps_its_own_title()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    file(store);
    MUST(write_file_by_name(fs::path { fit_of(store, 0) }.replace_extension(".json").string(), std::string_view { R"({ "title": "Wahoo's" })" }));
    record(store, Garmin, 60, 3540);
    MUST(write_file_by_name((store.storage.inbox_dir / std::format("{}-{}.json", Garmin, 60)).string(), std::string_view { R"({ "title": "Garmin's" })" }));
    file(store);
    CHECK(make_main(store.storage, recordings_dir(fit_of(store, 60)) / std::format("{}.fit", unix_start(0))).has_value());
    CHECK_EQ(sidecar_value(fit_of(store, 0), "title"), "Wahoo's");
}

// Ctrl+J on a recording whose sidecar can't be marked: an error, and
// nothing has changed.
void test_make_main_unmarkable_unchanged()
{
    Test::TempStore store;
    store.storage.prefer = { manufacturer::garmin };
    record(store, Wahoo, 0, 3600);
    record(store, Garmin, 60, 3540);
    file(store);
    auto const garmin = fit_of(store, 60);
    auto const recording = recordings_dir(garmin) / std::format("{}.fit", unix_start(0));
    MUST(write_file_by_name(fs::path { recording }.replace_extension(".json").string(), std::string_view { "not JSON" }));
    CHECK(!make_main(store.storage, recording).has_value());
    CHECK(fs::exists(garmin));
    CHECK(fs::exists(recording));
    CHECK_EQ(listed(store), 1uz);
}

// A recording from just after midnight at the end of the month becomes the
// main one: it's listed in the month after, where the job's ID for it
// finds it.
void test_make_main_across_month_end()
{
    Test::TempStore store;
    auto const      before = local_offset(2021, 9, 30, 23, 50);
    auto const      after = local_offset(2021, 10, 1, 0, 1);
    record(store, Wahoo, before, 3600);
    record(store, Garmin, after, 3000);
    file(store);
    auto const main = fit_of(store, before);
    auto const made = make_main(store.storage, recordings_dir(main) / std::format("{}.fit", unix_start(after)));
    CHECK(made.has_value());
    CHECK(fs::exists(fit_of(store, after)));
    CHECK(!fs::exists(main));
    CHECK(fs::exists(recordings_dir(fit_of(store, after)) / std::format("{}.fit", unix_start(before))));
    if (made) {
        auto const id = activity_id_of(made->main);
        CHECK(id.has_value() && id->month == month_of(after));
        if (id) {
            auto const loaded = load_deep(store, *id);
            CHECK(loaded.has_value() && loaded->recording_number == 1 && loaded->recordings == 2);
        }
    }
}

// The FIT file's product name, which the summary line names the device
// by, from its file_id.
void test_product_name()
{
    Test::TempStore  store;
    Test::FITBuilder fit;
    std::string_view product = "ELEMNT ROAM";
    fit.definition(4, 0,
        {
            { .num = 0, .size = 1, .base_type = Test::Enum },
            { .num = 1, .size = 2, .base_type = Test::Uint16 },
            { .num = 4, .size = 4, .base_type = Test::Uint32 },
            { .num = 8, .size = static_cast<u8>(product.size() + 1), .base_type = Test::String },
        });
    fit.message(4);
    fit.byte(4);
    fit.u16_le(Wahoo);
    fit.u32_le(Test::Start);
    fit.data += product;
    fit.byte(0);
    Test::add_session(fit, 3600, 0);
    MUST(write_file_by_name((store.storage.inbox_dir / "wahoo.fit").string(), std::string_view { fit.file() }));
    file(store);
    auto const loaded = load_deep(store, id_of(store, 0));
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK_EQ(loaded->product_name, "ELEMNT ROAM");
        CHECK(loaded->manufacturer == manufacturer::wahoo_fitness);
    }
}

}

int main()
{
    test_manufacturer_by_name();
    test_same_activity();
    test_same_recording();
    test_recording_info_from_memory();
    test_main_recording();
    test_filing_groups();
    test_filing_by_the_longest();
    test_split();
    test_split_after_another_became_main();
    test_group_with_main();
    test_group_with_main_fails_unchanged();
    test_group_with_main_nothing_to_group();
    test_group_with_main_takes_a_group();
    test_group_with_main_former_main();
    test_split_after_group_with_main();
    test_group_month();
    test_has_activity_across_months();
    test_has_activity_filed_elsewhere();
    test_decode_prefer();
    test_activity_id_of();
    test_recordings_in_order();
    test_main_of();
    test_load_a_recording();
    test_recording_has_the_activitys_title();
    test_make_main();
    test_split_from_a_recording();
    test_totals_follow_the_groups();
    test_group_month_filed_elsewhere();
    test_inbox_candidate_filed_elsewhere();
    test_former_main_with_folder();
    test_main_from_folder();
    test_title_of_its_own();
    test_strava_file_moves_along();
    test_across_month_end();
    test_split_place_taken();
    test_group_month_twice();
    test_recording_numbers_follow_tabs_order();
    test_load_a_gone_recording();
    test_make_main_after_ctrl_j();
    test_make_main_keeps_its_own_title();
    test_make_main_unmarkable_unchanged();
    test_make_main_across_month_end();
    test_product_name();
    return Test::test_result();
}
