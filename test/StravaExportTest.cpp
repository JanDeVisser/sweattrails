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
    test_message();
    return test_result();
}
