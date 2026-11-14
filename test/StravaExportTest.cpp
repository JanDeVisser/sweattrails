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
    CHECK_EQ(progress.size(), 4uz);
    CHECK((progress.back() == std::pair<size_t, size_t> { 3, 4 }));
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
    test_message();
    return test_result();
}
