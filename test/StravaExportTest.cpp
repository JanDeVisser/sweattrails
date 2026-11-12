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

#include <TempStore.h>
#include <TestApp.h>

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
    auto const descriptions = export_descriptions(fixture(), "activities.csv");
    CHECK(descriptions.has_value());
    if (!descriptions) {
        return;
    }
    CHECK_EQ(descriptions->size(), 5uz);
    CHECK_EQ(descriptions->at(LineBreaks), "Big Loop, twice.\r\n\r\nLegs felt \"fine\" until the second climb.");
    CHECK_EQ(descriptions->at(Quotes), "The \"Tempus Fugit\" route: flat and fast");
    CHECK_EQ(descriptions->at(Commas), "Rain, wind, and a flat tyre");
    CHECK_EQ(descriptions->at(Empty), "");
    CHECK_EQ(descriptions->at(AlsoEmpty), "");
}

void test_not_strava()
{
    auto const no_description = export_descriptions("Activity ID,Activity Name\n1,Ride\n", "activities.csv");
    CHECK(!no_description.has_value());
    if (!no_description) {
        CHECK_EQ(no_description.error(), "activities.csv isn't Strava's list of activities: it has no Activity Description column.");
    }
    auto const no_id = export_descriptions("Activity Name,Activity Description\nRide,Fun\n", "rides.csv");
    CHECK(!no_id.has_value());
    if (!no_id) {
        CHECK_EQ(no_id.error(), "rides.csv isn't Strava's list of activities: it has no Activity ID column.");
    }
    auto const invalid = export_descriptions("Activity ID,Activity Description\n1,\"Fun\n", "activities.csv");
    CHECK(!invalid.has_value());
    if (!invalid) {
        CHECK_EQ(invalid.error(), "activities.csv isn't Strava's list of activities: The quote that opens a field on line 2 isn't closed");
    }
}

void test_bad_lines_are_skipped()
{
    auto const descriptions = export_descriptions("Activity ID,Activity Name,Activity Description\n"
                                                  "1,Ride,Fun\n"
                                                  "2,Run\n"
                                                  "x3,Swim,Wet\n"
                                                  ",Walk,Slow\n"
                                                  "4,Hike,\n",
        "activities.csv");
    CHECK(descriptions.has_value());
    if (descriptions) {
        CHECK_EQ(descriptions->size(), 2uz);
        CHECK_EQ(descriptions->at(1), "Fun");
        CHECK_EQ(descriptions->at(4), "");
    }
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
    auto const                             read = export_descriptions(fixture(), "activities.csv");
    CHECK(read.has_value());
    auto const descriptions = read.value_or(std::map<u64, std::string> { });
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

    CHECK(strava_of(store, Ride) == (StravaTitle { .id = LineBreaks, .name = "Morning Ride", .description = descriptions.at(LineBreaks) }));
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

void test_message()
{
    CHECK_EQ((ExportImport { .descriptions = 4502, .empty = 3911, .untitled = 142 }).message(),
        "Strava's export: 4502 descriptions, 3911 of them empty. 142 activities have no Strava title: "
        "if Strava has them, get all the titles (Ctrl+Shift+G) and import again.");
    CHECK_EQ((ExportImport { .descriptions = 1, .empty = 0 }).message(), "Strava's export: 1 description, 0 of them empty.");
    CHECK_EQ((ExportImport { .untitled = 1 }).message(),
        "Strava's export has none of the descriptions missing here. 1 activity has no Strava title: "
        "if Strava has them, get all the titles (Ctrl+Shift+G) and import again.");
}

}

int main()
{
    test_fixture();
    test_not_strava();
    test_bad_lines_are_skipped();
    test_import();
    test_message();
    return test_result();
}
