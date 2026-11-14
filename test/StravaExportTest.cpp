/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests reading the descriptions in Strava's account export, and filling
 * in the store's missing ones from it. The fixture is a few lines of a
 * real export, with made-up names and descriptions.
 */

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <IO.h>

#include <storage/Activity.h>
#include <storage/Recordings.h>
#include <storage/StravaExport.h>

#include <TempDir.h>
#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>
#include <TestZip.h>

using namespace ST;
using namespace ST::Test;

namespace {

namespace fs = std::filesystem;

fs::path const Fixture = fs::path { SWEATTRAILS_SOURCE_DIR } / "test" / "data" / "strava-activities.csv";

// IDs in the fixture.
constexpr u64 LineBreaks = 20660687213;
constexpr u64 Quotes = 17363429866;
constexpr u64 Commas = 20843696969;
constexpr u64 Empty = 20934191161;
constexpr u64 AlsoEmpty = 1875601905;

constexpr i64 Ride = 1793102700; // 2026-10-27T12:05:00Z
constexpr i64 Run = 1793189100;  // 2026-10-28T12:05:00Z
constexpr i64 Swim = 1793257500; // 2026-10-29T07:05:00Z
constexpr i64 Walk = 1793289600; // 2026-10-29T16:00:00Z
constexpr i64 Hike = 1793376000; // 2026-10-30T16:00:00Z

std::string fixture()
{
    return MUST_EVAL(read_file_by_name(Fixture.string()));
}

void test_fixture()
{
    auto const descriptions = read_export(fixture(), "activities.csv");
    CHECK(descriptions.has_value());
    if (!descriptions) {
        return;
    }
    CHECK_EQ(descriptions->size(), 5uz);
    CHECK_EQ(descriptions->at(LineBreaks).description, "Big Loop, twice.\r\n\r\nLegs felt \"fine\" until the second climb.");
    CHECK_EQ(descriptions->at(Quotes).description, "The \"Tempus Fugit\" route: flat and fast");
    CHECK_EQ(descriptions->at(Commas).description, "Rain, wind, and a flat tyre");
    CHECK_EQ(descriptions->at(Empty).description, "");
    CHECK_EQ(descriptions->at(AlsoEmpty).description, "");
}

void test_not_strava()
{
    auto const no_description = read_export("Activity ID,Activity Name\n1,Ride\n", "activities.csv");
    CHECK(!no_description.has_value());
    if (!no_description) {
        CHECK_EQ(no_description.error(), "activities.csv isn't Strava's list of activities: it has no Activity Description column.");
    }
    auto const no_id = read_export("Activity Name,Activity Description\nRide,Fun\n", "rides.csv");
    CHECK(!no_id.has_value());
    if (!no_id) {
        CHECK_EQ(no_id.error(), "rides.csv isn't Strava's list of activities: it has no Activity ID column.");
    }
    auto const invalid = read_export("Activity ID,Activity Description\n1,\"Fun\n", "activities.csv");
    CHECK(!invalid.has_value());
    if (!invalid) {
        CHECK_EQ(invalid.error(), "activities.csv isn't Strava's list of activities: The quote that opens a field on line 2 isn't closed");
    }
}

void test_bad_lines_are_skipped()
{
    auto const descriptions = read_export("Activity ID,Activity Name,Activity Description\n"
                                          "1,Ride,Fun\n"
                                          "2,Run\n"
                                          "x3,Swim,Wet\n"
                                          ",Walk,Slow\n"
                                          "4,Hike,\n",
        "activities.csv");
    CHECK(descriptions.has_value());
    if (descriptions) {
        CHECK_EQ(descriptions->size(), 2uz);
        CHECK_EQ(descriptions->at(1).description, "Fun");
        CHECK_EQ(descriptions->at(4).description, "");
        CHECK_EQ(descriptions->at(1).name, "Ride");
        CHECK_EQ(descriptions->at(4).name, "Hike");
    }
}

void test_is_csv_file()
{
    CHECK(is_csv_file("/Users/jan/Downloads/export_8345975/activities.csv"));
    CHECK(is_csv_file("ACTIVITIES.CSV"));
    CHECK(!is_csv_file("/tmp/24790584050_ACTIVITY.fit"));
    CHECK(!is_csv_file("export_8345975.zip"));
    CHECK(!is_csv_file("csv"));
}

void save_strava(TempStore const &store, i64 start, StravaTitle const &title)
{
    MUST(save_sidecar_strava(store.storage.sidecar_of(start), title));
}

std::optional<StravaTitle> strava_of(TempStore const &store, i64 start)
{
    return sidecar_titles(store.storage.sidecar_of(start)).strava;
}

void test_import()
{
    TempStore store { };
    for (auto const start : { Ride, Run, Swim, Walk, Hike }) {
        store.add_activity(start);
    }
    // Ride and Run wait for their descriptions; Run's user wrote notes.
    save_strava(store, Ride, { .id = LineBreaks, .name = "Morning Ride" });
    save_strava(store, Run, { .id = Empty, .name = "Lunch Run" });
    MUST(update_sidecar(store.storage.sidecar_of(Run), [](JSONValue &json) { json.set("notes", JSONValue { "My own" }); }));
    // Swim's description was fetched already, and is newer than the export's.
    save_strava(store, Swim, { .id = Commas, .name = "Swim", .description = "Rain and wind" });
    // Walk isn't in the export; Hike has no Strava title.
    save_strava(store, Walk, { .id = 42, .name = "Walk" });

    std::vector<std::pair<size_t, size_t>> progress;
    auto const                             read = read_export(fixture(), "activities.csv");
    CHECK(read.has_value());
    auto const descriptions = read.value_or(ExportActivities { });
    auto const imported = import_descriptions(store.storage, descriptions, [&progress](size_t index, size_t count) {
        progress.emplace_back(index, count);
    });
    CHECK(imported.has_value());
    if (!imported) {
        return;
    }
    CHECK_EQ(imported->descriptions, 2uz);
    CHECK_EQ(imported->empty, 1uz);
    CHECK_EQ(imported->untitled, 1uz);
    CHECK(imported->changed == (std::vector<i64> { Ride, Run }));
    CHECK(progress == (std::vector<std::pair<size_t, size_t>> { { 0, 2 }, { 1, 2 } }));

    CHECK(strava_of(store, Ride) == (StravaTitle { .id = LineBreaks, .name = "Morning Ride", .description = descriptions.at(LineBreaks).description }));
    CHECK(strava_of(store, Run) == (StravaTitle { .id = Empty, .name = "Lunch Run", .description = "" }));
    CHECK_EQ(sidecar_titles(store.storage.sidecar_of(Run)).own_notes.value_or("none"), "My own");
    CHECK(strava_of(store, Swim) == (StravaTitle { .id = Commas, .name = "Swim", .description = "Rain and wind" }));
    CHECK(strava_of(store, Walk) == (StravaTitle { .id = 42, .name = "Walk" }));
    CHECK(!strava_of(store, Hike).has_value());

    // A second import has nothing left to fill in.
    auto const again = import_descriptions(store.storage, descriptions);
    CHECK(again.has_value());
    if (again) {
        CHECK_EQ(again->descriptions, 0uz);
        CHECK(again->changed.empty());
    }
}

void test_filename()
{
    auto const read = read_export("Activity ID,Activity Description,Filename\n"
                                  "1,Fun,activities/11.fit.gz\n"
                                  "2,,\n"
                                  "3,Short\n",
        "activities.csv");
    CHECK(read.has_value());
    if (read) {
        CHECK_EQ(read->at(1).file, "activities/11.fit.gz");
        CHECK_EQ(read->at(2).file, "");
        CHECK_EQ(read->at(3).file, "");
    }
    // Without the column, the descriptions are still there.
    auto const without = read_export("Activity ID,Activity Description\n1,Fun\n", "activities.csv");
    CHECK(without.has_value());
    if (without) {
        CHECK_EQ(without->at(1).description, "Fun");
        CHECK_EQ(without->at(1).file, "");
        CHECK_EQ(without->at(1).name, "");
    }
}

// Unix times of FIT test files' starts, `offset` seconds after Test::Start.
i64 fit_start(i64 offset)
{
    return static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET + offset;
}

void put(TempStore const &store, i64 offset, std::string_view fit, u64 strava_id)
{
    auto const sidecar = store.storage.sidecar_of(fit_start(offset));
    fs::create_directories(sidecar.parent_path());
    MUST(write_file_by_name(fs::path { sidecar }.replace_extension(".fit").string(), fit));
    MUST(save_sidecar_strava(sidecar, { .id = strava_id, .name = "Run", .description = "" }));
}

std::string stored_fit(TempStore const &store, i64 offset, std::string_view suffix = "")
{
    auto const fit = fs::path { store.storage.sidecar_of(fit_start(offset)) }.replace_extension(".fit");
    return read_file_by_name(fit.string() + std::string { suffix }).value_or("");
}

void test_import_originals()
{
    TempStore  store { };
    TempDir    export_dir { "strava-export" };
    auto const files = export_dir.path / "activities";
    fs::create_directories(files);
    // Stubs whose originals are gzipped, plain, with another start, GPX,
    // missing, and a stub too. A real activity, and a stub without a file.
    put(store, 0, summary_fit(0), 1);
    put(store, 1000, summary_fit(1000), 2);
    put(store, 2000, summary_fit(2000), 3);
    put(store, 3000, summary_fit(3000), 4);
    put(store, 4000, summary_fit(4000), 5);
    put(store, 5000, summary_fit(5000), 6);
    put(store, 6000, recorded_fit(6000), 7);
    put(store, 7000, summary_fit(7000), 8);
    MUST(write_file_by_name((files / "101.fit.gz").string(), std::string_view { gzip(recorded_fit(0)) }));
    MUST(write_file_by_name((files / "102.fit").string(), std::string_view { recorded_fit(1000) }));
    MUST(write_file_by_name((files / "103.FIT.GZ").string(), std::string_view { gzip(recorded_fit(2005)) }));
    MUST(write_file_by_name((files / "104.gpx.gz").string(), std::string_view { "<gpx/>" }));
    MUST(write_file_by_name((files / "106.fit").string(), std::string_view { summary_fit(5000) }));
    MUST(write_file_by_name((files / "107.fit").string(), std::string_view { recorded_fit(6000) }));
    auto const read = read_export("Activity ID,Activity Description,Filename\n"
                                  "1,,activities/101.fit.gz\n"
                                  "2,,activities/102.fit\n"
                                  "3,,activities/103.FIT.GZ\n"
                                  "4,,activities/104.gpx.gz\n"
                                  "5,,activities/105.fit.gz\n"
                                  "6,,activities/106.fit\n"
                                  "7,,activities/107.fit\n"
                                  "8,,\n",
        "activities.csv");
    CHECK(read.has_value());

    std::vector<std::pair<size_t, size_t>> progress;
    auto const                             imported = import_originals(store.storage, read.value_or(ExportActivities { }), export_dir.path, [&progress](size_t index, size_t count) {
        progress.emplace_back(index, count);
    });
    CHECK(imported.has_value());
    if (!imported) {
        return;
    }
    CHECK_EQ(imported->replaced, 3uz);
    CHECK_EQ(imported->unreadable, 1uz);
    CHECK((imported->moved == std::vector<std::pair<i64, i64>> { { fit_start(0), fit_start(0) }, { fit_start(1000), fit_start(1000) }, { fit_start(2000), fit_start(2005) } }));
    CHECK_EQ(progress.size(), 5uz);
    CHECK((progress.front() == std::pair<size_t, size_t> { 0, 5 }));

    CHECK_EQ(stored_fit(store, 0), recorded_fit(0));
    CHECK_EQ(stored_fit(store, 0, ".stub"), summary_fit(0));
    CHECK_EQ(stored_fit(store, 1000), recorded_fit(1000));
    CHECK_EQ(stored_fit(store, 2005), recorded_fit(2005));
    CHECK_EQ(stored_fit(store, 2000), "");
    CHECK_EQ(sidecar_titles(store.storage.sidecar_of(fit_start(2005))).strava.value_or(StravaTitle { }).id, 3u);
    for (auto const offset : { 3000, 4000, 5000, 7000 }) {
        CHECK_EQ(stored_fit(store, offset), summary_fit(static_cast<u32>(offset)));
        CHECK_EQ(stored_fit(store, offset, ".stub"), "");
    }
    CHECK_EQ(stored_fit(store, 6000, ".stub"), "");

    // Once replaced, they're no stubs any more.
    auto const again = import_originals(store.storage, read.value_or(ExportActivities { }), export_dir.path);
    CHECK_EQ(again.value_or(StubImport { .replaced = 99 }).replaced, 0uz);
}

// What the inbox holds as strava-<id>.<extension>.
std::string in_inbox(TempStore const &store, u64 id, std::string_view extension)
{
    return read_file_by_name((store.storage.inbox_dir / std::format("strava-{}.{}", id, extension)).string()).value_or("");
}

// The activities listed in the store, by start time.
std::vector<i64> listed(TempStore const &store)
{
    std::vector<i64> ret;
    for (auto const &month : MUST_EVAL(store.storage.months_with_activities())) {
        for (auto const &stored : MUST_EVAL(store.storage.month_starts(month))) {
            ret.push_back(stored.start);
        }
    }
    std::ranges::sort(ret);
    return ret;
}

void test_queue_missing()
{
    TempStore  store { };
    TempDir    export_dir { "strava-export" };
    auto const files = export_dir.path / "activities";
    fs::create_directories(files);
    // In the store: 1 listed, 3 a recording in 2's folder, and 4 set aside
    // by the inbox before.
    put(store, 0, recorded_fit(0), 1);
    put(store, 1000, recorded_fit(1000), 2);
    auto const folder = recordings_dir(fs::path { store.storage.sidecar_of(fit_start(1000)) }.replace_extension(".fit"));
    fs::create_directories(folder);
    MUST(write_file_by_name((folder / std::format("{}.fit", fit_start(1010))).string(), std::string_view { recorded_fit(1010) }));
    MUST(save_sidecar_strava(folder / std::format("{}.json", fit_start(1010)), { .id = 3, .name = "Zwift" }));
    fs::create_directories(store.storage.inbox_dir / "duplicates");
    MUST(write_file_by_name((store.storage.inbox_dir / "duplicates" / "strava-4.fit").string(), std::string_view { recorded_fit(2000) }));

    for (auto const id : { 1, 3, 4 }) {
        MUST(write_file_by_name((files / std::format("{}.fit", 100 + id)).string(), std::string_view { recorded_fit(3000) }));
    }
    MUST(write_file_by_name((files / "105.fit.gz").string(), std::string_view { gzip(recorded_fit(5000)) }));
    MUST(write_file_by_name((files / "106.fit").string(), std::string_view { recorded_fit(6000) }));
    MUST(write_file_by_name((files / "107.gpx").string(), std::string_view { "<gpx/>" }));
    MUST(write_file_by_name((files / "109.fit.gz").string(), std::string_view { "not gzip" }));
    auto const read = read_export("Activity ID,Activity Name,Activity Description,Filename\n"
                                  "1,Listed,,activities/101.fit\n"
                                  "3,Recording,,activities/103.fit\n"
                                  "4,Set aside,,activities/104.fit\n"
                                  "5,Morning Ride,Fun,activities/105.fit.gz\n"
                                  "6,Zwift,,activities/106.fit\n"
                                  "7,GPX,,activities/107.gpx\n"
                                  "8,Missing,,activities/108.fit.gz\n"
                                  "9,Corrupt,,activities/109.fit.gz\n"
                                  "10,By hand,,\n",
        "activities.csv");
    CHECK(read.has_value());

    std::vector<std::pair<size_t, size_t>> progress;
    auto const                             queued = queue_missing(store.storage, read.value_or(ExportActivities { }), export_dir.path, [&progress](size_t index, size_t count) {
        progress.emplace_back(index, count);
    });
    CHECK(queued.has_value());
    if (!queued) {
        return;
    }
    CHECK_EQ(queued->queued, 2uz);
    CHECK_EQ(queued->unreadable, 1uz);
    // 1 and 3 are checked against the store's files too: they're the same
    // recording, by their totals.
    CHECK_EQ(progress.size(), 6uz);
    CHECK((progress.back() == std::pair<size_t, size_t> { 5, 6 }));
    CHECK_EQ(queued->titles_moved, 0uz);
    CHECK((store.inbox() == std::vector<std::string> { "duplicates", "strava-5.fit", "strava-5.json", "strava-6.fit", "strava-6.json" }));
    CHECK_EQ(in_inbox(store, 5, "fit"), recorded_fit(5000));
    CHECK_EQ(in_inbox(store, 6, "fit"), recorded_fit(6000));
    auto const title = sidecar_titles(store.storage.inbox_dir / "strava-5.json").strava;
    CHECK((title == StravaTitle { .id = 5, .name = "Morning Ride", .description = "Fun" }));
}

void test_count()
{
    using Outcome = Storage::InboxOutcome;
    MissingImport missing;
    missing.count("strava-1.fit", { .outcome = Outcome::Organized });
    missing.count("strava-2.fit", { .outcome = Outcome::Replaced });
    missing.count("strava-3.fit", { .outcome = Outcome::Duplicate });
    missing.count("strava-4.fit", { .outcome = Outcome::Skipped });
    missing.count("garmin-5.fit", { .outcome = Outcome::Organized });
    missing.count("wahoo-6.fit", { .outcome = Outcome::Duplicate });
    CHECK_EQ(missing.added, 2uz);
    CHECK_EQ(missing.set_aside, 1uz);
}

// The export's missing activities through the real inbox, as the job runs
// it: filed, set aside, and grouped with their activity.
void test_missing_through_inbox()
{
    TempStore  store { };
    TempDir    export_dir { "strava-export" };
    auto const files = export_dir.path / "activities";
    fs::create_directories(files);
    put(store, 10000, recorded_fit(10000), 20);
    put(store, 20000, recorded_fit(20000), 21);
    MUST(write_file_by_name((store.storage.inbox_dir / "garmin-1.fit").string(), std::string_view { recorded_fit(40000) }));
    MUST(write_file_by_name((files / "130.fit").string(), std::string_view { recorded_fit(30000) }));
    MUST(write_file_by_name((files / "131.fit").string(), std::string_view { recorded_fit(10005) }));
    MUST(write_file_by_name((files / "132.fit").string(), std::string_view { recorded_fit(20000) }));
    auto const activities = read_export("Activity ID,Activity Name,Activity Description,Filename\n"
                                        "30,Missing,,activities/130.fit\n"
                                        "31,Another recording,,activities/131.fit\n"
                                        "32,The same,,activities/132.fit\n",
        "activities.csv")
                                .value_or(ExportActivities { });

    auto missing = queue_missing(store.storage, activities, export_dir.path).value_or(MissingImport { });
    CHECK_EQ(missing.queued, 3uz);
    auto const filed = store.storage.process_inbox({ }, [&store, &missing](std::string_view file_name, Storage::InboxFiling const &filing) {
        missing.count(file_name, filing);
        if (filing.filed()) {
            CHECK(group_new(store.storage, filing.start).has_value());
        }
    });
    CHECK_EQ(filed.value_or(0), 3u);
    CHECK_EQ(missing.added, 2uz);
    CHECK_EQ(missing.set_aside, 1uz);
    CHECK_EQ(strava_of(store, fit_start(30000)).value_or(StravaTitle { }).name, "Missing");
    // 10005 is a recording of 10000, in one's folder or the other's.
    CHECK((listed(store) == std::vector { fit_start(10000), fit_start(20000), fit_start(30000), fit_start(40000) })
        || (listed(store) == std::vector { fit_start(10005), fit_start(20000), fit_start(30000), fit_start(40000) }));
    CHECK(store.storage.has_activity(fit_start(10005)));
    CHECK(fs::exists(store.storage.inbox_dir / "duplicates" / "strava-32.fit"));

    // Dropped again, the export has nothing more.
    auto const again = queue_missing(store.storage, activities, export_dir.path);
    CHECK_EQ(again.value_or(MissingImport { .queued = 99 }).queued, 0uz);
    CHECK((store.inbox() == std::vector<std::string> { "duplicates" }));
}

// Two lines of the export with the same start, such as an activity uploaded
// twice: the inbox files the first and sets the second aside, and a second
// import queues neither.
void test_same_start_twice_in_export()
{
    TempStore  store { };
    TempDir    export_dir { "strava-export" };
    auto const files = export_dir.path / "activities";
    fs::create_directories(files);
    MUST(write_file_by_name((files / "201.fit").string(), std::string_view { recorded_fit(50000) }));
    MUST(write_file_by_name((files / "202.fit").string(), std::string_view { recorded_fit(50000) }));
    auto const activities = read_export("Activity ID,Activity Name,Activity Description,Filename\n"
                                        "41,First,,activities/201.fit\n"
                                        "42,Twice,,activities/202.fit\n",
        "activities.csv")
                                .value_or(ExportActivities { });
    auto       missing = queue_missing(store.storage, activities, export_dir.path).value_or(MissingImport { });
    CHECK_EQ(missing.queued, 2uz);
    auto const filed = store.storage.process_inbox({ }, [&missing](std::string_view file_name, Storage::InboxFiling const &filing) {
        missing.count(file_name, filing);
    });
    CHECK_EQ(filed.value_or(0), 1u);
    CHECK_EQ(missing.added, 1uz);
    CHECK_EQ(missing.set_aside, 1uz);
    CHECK_EQ(listed(store).size(), 1uz);
    CHECK_EQ(queue_missing(store.storage, activities, export_dir.path).value_or(MissingImport { .queued = 99 }).queued, 0uz);
}

// A head unit's ride indoors, 3600 s from 1000, and what Strava's file
// for it may be.
RecordingInfo indoors(i64 start, u32 serial, float32 distance, u32 elapsed = 3600)
{
    return RecordingInfo { .start = start, .elapsed = elapsed, .sport = sport::Cycling, .serial = serial, .distance = distance };
}

void test_classify_export_file()
{
    auto const                      head_unit = indoors(1000, 77, 0);
    auto const                      zwift = indoors(984, 0, 30000);
    std::vector<StoredWithID> const stored { { .info = head_unit, .recordings = { head_unit } } };
    CHECK(classify_export_file(zwift, stored) == ExportFile::OtherRecording);
    // Garmin Connect's copy of the head unit's file: no serial, same totals.
    CHECK(classify_export_file(indoors(1003, 0, 0, 3602), stored) == ExportFile::Same);
    // Zwift's file is in the group already, rewritten, and the ID landed
    // on the head unit's.
    std::vector<StoredWithID> const grouped { { .info = head_unit, .recordings = { head_unit, indoors(990, 0, 30010, 3601) } } };
    CHECK(classify_export_file(zwift, grouped) == ExportFile::Same);
    // Strava's file is another activity: the ID is on the wrong one.
    CHECK(classify_export_file(indoors(9000, 0, 30000), stored) == ExportFile::OtherActivity);
    // The ID on two activities, the second of which it overlaps.
    std::vector<StoredWithID> const two { stored[0], { .info = indoors(9010, 88, 0), .recordings = { indoors(9010, 88, 0) } } };
    CHECK(classify_export_file(indoors(9000, 0, 30000), two) == ExportFile::OtherRecording);
}

// Strava's files for the IDs the store has, through the real inbox:
// Zwift's file next to the head unit's is grouped with it, a rewrite and
// the same file are skipped, and a file that's another activity is filed
// as one, and takes the ID along. A second import adds nothing.
void test_other_recordings_through_inbox()
{
    TempStore  store { };
    TempDir    export_dir { "strava-export" };
    auto const files = export_dir.path / "activities";
    fs::create_directories(files);
    auto const export_file = [&files](std::string_view name, std::string_view fit) {
        MUST(write_file_by_name((files / name).string(), fit));
    };
    put(store, 100, recording_fit({ .manufacturer = 1, .serial = 77, .start = 100, .count = 3600 }), 51);
    export_file("151.fit", recording_fit({ .manufacturer = 260, .start = 84, .count = 3600, .distance = 30000, .positions = true }));
    put(store, 10000, recording_fit({ .start = 10000, .count = 1200, .distance = 10000 }), 52);
    export_file("152.fit", recording_fit({ .manufacturer = 255, .start = 10003, .count = 1202, .distance = 10020 }));
    put(store, 20000, recorded_fit(20000), 53);
    export_file("153.fit", recorded_fit(20000));
    put(store, 30000, recording_fit({ .manufacturer = 1, .serial = 88, .start = 30000 }), 54);
    MUST(save_sidecar_title(store.storage.sidecar_of(fit_start(30000)), "Mine"));
    export_file("154.fit", recording_fit({ .serial = 99, .start = 40000, .count = 3000, .distance = 20000 }));
    auto const activities = read_export("Activity ID,Activity Name,Activity Description,Filename\n"
                                        "51,Zwift - Watopia,,activities/151.fit\n"
                                        "52,Rewritten,,activities/152.fit\n"
                                        "53,The same,,activities/153.fit\n"
                                        "54,Elsewhere,,activities/154.fit\n",
        "activities.csv")
                                .value_or(ExportActivities { });

    auto missing = queue_missing(store.storage, activities, export_dir.path).value_or(MissingImport { });
    CHECK_EQ(missing.queued, 2uz);
    CHECK_EQ(missing.titles_moved, 1uz);
    CHECK((missing.retitled == std::vector { fit_start(30000) }));
    CHECK((store.inbox() == std::vector<std::string> { "strava-51.fit", "strava-51.json", "strava-54.fit", "strava-54.json" }));
    auto const wrong = sidecar_titles(store.storage.sidecar_of(fit_start(30000)));
    CHECK(!wrong.strava.has_value());
    CHECK((wrong.strava_rejected == std::vector<u64> { 54 }));
    CHECK_EQ(wrong.own.value_or(""), "Mine");

    auto const filed = store.storage.process_inbox({ }, [&store, &missing](std::string_view file_name, Storage::InboxFiling const &filing) {
        missing.count(file_name, filing);
        if (filing.filed()) {
            CHECK(group_new(store.storage, filing.start).has_value());
        }
    });
    CHECK_EQ(filed.value_or(0), 2u);
    CHECK_EQ(missing.added, 2uz);
    // Zwift's file is a recording of the head unit's, in one's folder or
    // the other's; the other activity is listed, with Strava's title.
    auto const listed_now = listed(store);
    CHECK_EQ(listed_now.size(), 5uz);
    CHECK(std::ranges::contains(listed_now, fit_start(100)) != std::ranges::contains(listed_now, fit_start(84)));
    CHECK(store.storage.has_activity(fit_start(84)));
    CHECK((strava_of(store, fit_start(40000)) == StravaTitle { .id = 54, .name = "Elsewhere", .description = "" }));

    auto const again = queue_missing(store.storage, activities, export_dir.path).value_or(MissingImport { .queued = 99 });
    CHECK_EQ(again.queued, 0uz);
    CHECK_EQ(again.titles_moved, 0uz);
    CHECK(store.inbox().empty());
}

// Strava's file is in the store already, at its own start, without the
// ID, which is on another activity: a multisport file filed by its last
// session, say. The title moves to it, unless it has a Strava title of
// its own, and nothing goes to the inbox. Nor does another recording
// that's in the store as an activity of its own.
void test_strava_file_already_here()
{
    TempStore  store { };
    TempDir    export_dir { "strava-export" };
    auto const files = export_dir.path / "activities";
    fs::create_directories(files);
    auto const export_file = [&files](std::string_view name, std::string_view fit) {
        MUST(write_file_by_name((files / name).string(), fit));
    };
    // 61 is on 100, but Strava's file is 5000, here without a title.
    put(store, 100, recording_fit({ .manufacturer = 1, .serial = 77, .start = 100 }), 61);
    auto const here = recording_fit({ .manufacturer = 1, .serial = 78, .start = 5000 });
    store.add_activity(fit_start(5000));
    MUST(write_file_by_name(fs::path { store.storage.sidecar_of(fit_start(5000)) }.replace_extension(".fit").string(), std::string_view { here }));
    export_file("161.fit", recording_fit({ .manufacturer = 255, .start = 5000, .count = 590 }));
    // 62 is on 10000, but Strava's file is 15000, which has a title.
    put(store, 10000, recording_fit({ .manufacturer = 1, .serial = 79, .start = 10000 }), 62);
    put(store, 15000, recording_fit({ .manufacturer = 1, .serial = 80, .start = 15000 }), 65);
    export_file("162.fit", recording_fit({ .manufacturer = 255, .start = 15000, .count = 590 }));
    // 63 is on 20000, and Strava's file is another recording of it, which
    // is here as an activity of its own.
    put(store, 20000, recording_fit({ .manufacturer = 1, .serial = 81, .start = 20000 }), 63);
    store.add_activity(fit_start(20010));
    MUST(write_file_by_name(fs::path { store.storage.sidecar_of(fit_start(20010)) }.replace_extension(".fit").string(),
        std::string_view { recording_fit({ .manufacturer = 32, .serial = 82, .start = 20010 }) }));
    export_file("163.fit", recording_fit({ .manufacturer = 255, .start = 20010, .count = 590 }));
    auto const activities = read_export("Activity ID,Activity Name,Activity Description,Filename\n"
                                        "61,Triathlon,Hot,activities/161.fit\n"
                                        "62,Twice,,activities/162.fit\n"
                                        "63,Wahoo,,activities/163.fit\n",
        "activities.csv")
                                .value_or(ExportActivities { });

    auto const missing = queue_missing(store.storage, activities, export_dir.path).value_or(MissingImport { .queued = 99 });
    CHECK_EQ(missing.queued, 0uz);
    CHECK(store.inbox().empty());
    CHECK_EQ(missing.titles_moved, 1uz);
    CHECK((missing.retitled == std::vector { fit_start(100), fit_start(5000), fit_start(10000) }));
    CHECK(!strava_of(store, fit_start(100)).has_value());
    CHECK((strava_of(store, fit_start(5000)) == StravaTitle { .id = 61, .name = "Triathlon", .description = "Hot" }));
    CHECK(!strava_of(store, fit_start(10000)).has_value());
    CHECK_EQ(strava_of(store, fit_start(15000)).value_or(StravaTitle { }).id, 65u);
    CHECK_EQ(strava_of(store, fit_start(20000)).value_or(StravaTitle { }).id, 63u);
    CHECK(!strava_of(store, fit_start(20010)).has_value());

    // A second import queues nothing. 62's file is here, but 62 is on none
    // of the files now (15000 has its own title), so the import takes it
    // for missing and queues it for the inbox to set aside.
    auto const again = queue_missing(store.storage, activities, export_dir.path).value_or(MissingImport { .queued = 99 });
    CHECK_EQ(again.queued, 0uz);
    CHECK_EQ(again.titles_moved, 0uz);
}

void test_message()
{
    CHECK_EQ((ExportImport { .descriptions = 4502, .empty = 3911, .untitled = 142 }).message(),
        "Strava's export: 4502 descriptions, 3911 of them empty. 142 activities have no Strava title: "
        "if Strava has them, get all the titles (Ctrl+Shift+G) and import again.");
    CHECK_EQ((ExportImport { .descriptions = 1, .empty = 0 }).message(), "Strava's export: 1 description, 0 of them empty.");
    CHECK_EQ((ExportImport { .untitled = 1 }).message(),
        "Strava's export has none of the descriptions missing here. 1 activity has no Strava title: "
        "if Strava has them, get all the titles (Ctrl+Shift+G) and import again.");
    CHECK_EQ((ExportImport { .descriptions = 1, .stubs = { .replaced = 140, .unreadable = 3 } }).message(),
        "Strava's export: 1 description, 0 of them empty. Replaced 140 activities that were only a summary with their recordings. "
        "3 more have a GPX or TCX file, which SweatTrails can't read.");
    CHECK_EQ((ExportImport { .stubs = { .replaced = 1 } }).message(),
        "Strava's export has none of the descriptions missing here. Replaced 1 activity that was only a summary with its recording.");
    CHECK_EQ((ExportImport { .stubs = { .unreadable = 1 } }).message(),
        "Strava's export has none of the descriptions missing here. 1 activity that's only a summary has a GPX or TCX file, which SweatTrails can't read.");

    // The activities the store didn't have.
    CHECK_EQ((ExportImport {
                  .descriptions = 1,
                  .missing = { .unreadable = 15, .added = 493, .set_aside = 163 },
              })
                 .message(),
        "Strava's export: 1 description, 0 of them empty. Added 493 activities from the export; 163 were already here. "
        "15 more have a GPX or TCX file, which SweatTrails can't read.");
    CHECK_EQ((ExportImport { .missing = { .added = 1 } }).message(),
        "Strava's export has none of the descriptions missing here. Added 1 activity from the export.");
    CHECK_EQ((ExportImport { .missing = { .added = 2, .set_aside = 1 } }).message(),
        "Strava's export has none of the descriptions missing here. Added 2 activities from the export; 1 was already here.");
    CHECK_EQ((ExportImport { .missing = { .set_aside = 2 } }).message(),
        "Strava's export has none of the descriptions missing here. 2 of the export's activities were already here.");
    CHECK_EQ((ExportImport { .stubs = { .replaced = 2, .unreadable = 3 }, .missing = { .unreadable = 15, .added = 4 } }).message(),
        "Strava's export has none of the descriptions missing here. Replaced 2 activities that were only a summary with their recordings. "
        "Added 4 activities from the export. 18 more have a GPX or TCX file, which SweatTrails can't read.");
    CHECK_EQ((ExportImport { .missing = { .titles_moved = 64, .added = 64 } }).message(),
        "Strava's export has none of the descriptions missing here. Added 64 activities from the export. Moved 64 Strava titles to the right activity.");
    CHECK_EQ((ExportImport { .missing = { .titles_moved = 1, .set_aside = 1 } }).message(),
        "Strava's export has none of the descriptions missing here. 1 of the export's activities was already here. Moved 1 Strava title to the right activity.");
    CHECK_EQ((ExportImport { .stubs = { .unreadable = 3 }, .missing = { .unreadable = 1 } }).message(),
        "Strava's export has none of the descriptions missing here. 4 activities have a GPX or TCX file, which SweatTrails can't read.");
}

}

int main()
{
    test_fixture();
    test_not_strava();
    test_bad_lines_are_skipped();
    test_is_csv_file();
    test_import();
    test_filename();
    test_import_originals();
    test_queue_missing();
    test_count();
    test_missing_through_inbox();
    test_same_start_twice_in_export();
    test_classify_export_file();
    test_other_recordings_through_inbox();
    test_strava_file_already_here();
    test_message();
    return test_result();
}
