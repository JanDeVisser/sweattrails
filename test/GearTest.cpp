/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the gear in gear.json, which gear an activity uses, and the
 * distance on each (specs/67-gear.md).
 */

#include <chrono>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <FileIO.h>
#include <JSON.h>

#include <storage/Gear.h>
#include <storage/Volume.h>

#include <TempStore.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;
namespace chrono = std::chrono;
namespace fs = std::filesystem;

namespace {

// The spec's example.
constexpr char const Example[] = R"({
  "shoes": [
    { "name": "Pegasus blue", "brand": "Nike", "model": "Pegasus",
      "version": "41", "colourway": "Blue",
      "purchased": "2026-03-14", "where": "Running Room", "price": "179.99",
      "start_km": 0, "retired": "2026-11-01" }
  ],
  "bikes": [
    { "name": "Canyon", "brand": "Canyon", "model": "Ultimate CF SL", "year": 2022,
      "purchased": "2022-05-01", "where": "canyon.com", "price": "3200",
      "start_km": 4300 },
    { "name": "Gravel" }
  ],
  "defaults": [
    { "for": "running", "gear": "Pegasus blue", "from": "2026-03-14" },
    { "for": "road", "gear": "Canyon", "from": "2022-05-01" },
    { "for": "indoor", "gear": "Canyon", "from": "2025-11-01" },
    { "for": "indoor", "gear": "Gravel", "from": "2026-04-01" }
  ]
})";

chrono::year_month_day day(int y, unsigned m, unsigned d)
{
    return chrono::year { y } / chrono::month { m } / chrono::day { d };
}

constexpr chrono::year_month_day Today = chrono::year { 2026 } / chrono::November / chrono::day { 23 };

std::expected<GearList, std::string> decode_text(std::string_view text)
{
    auto const json = JSONValue::deserialize(text);
    if (!json) {
        return std::unexpected(std::string { "not JSON" });
    }
    return decode_gear(*json);
}

// The list in `text`, which a test expects to decode.
GearList decoded(std::string_view text)
{
    auto list = decode_text(text);
    CHECK(list.has_value());
    return list.value_or(GearList { });
}

GearList example()
{
    return decoded(Example);
}

// The error decoding `text`, or "" if it decodes.
std::string error_of(std::string_view text)
{
    auto const list = decode_text(text);
    return list ? std::string { } : list.error();
}

using enum GearKind;
using FIT::sub_sport;

void test_decode_the_example()
{
    auto const list = decode_text(Example);
    CHECK(list.has_value());
    if (!list) {
        return;
    }
    CHECK_EQ(list->gear.size(), 3uz);
    auto const *shoes = list->find("Pegasus blue");
    CHECK(shoes != nullptr);
    if (shoes != nullptr) {
        CHECK(shoes->type == GearType::Shoes);
        CHECK_EQ(shoes->brand, "Nike");
        CHECK_EQ(shoes->model, "Pegasus");
        CHECK_EQ(shoes->version, "41");
        CHECK_EQ(shoes->colourway, "Blue");
        CHECK_EQ(shoes->where, "Running Room");
        CHECK_EQ(shoes->price, "179.99");
        CHECK(shoes->purchased == day(2026, 3, 14));
        CHECK(shoes->retired == day(2026, 11, 1));
    }
    auto const *bike = list->find("Canyon");
    CHECK(bike != nullptr);
    if (bike != nullptr) {
        CHECK(bike->type == GearType::Bike);
        CHECK(bike->year == 2022);
        CHECK_EQ(bike->start_km, 4300.0);
        CHECK(!bike->retired.has_value());
    }
    // All but the name is optional, and start_km is 0 without one.
    auto const *gravel = list->find("Gravel");
    CHECK(gravel != nullptr && gravel->start_km == 0 && gravel->brand.empty() && !gravel->purchased);
    CHECK_EQ(list->defaults.size(), 4uz);
    CHECK(list->defaults.size() == 4 && (list->defaults[0] == GearDefault { .kind = Road, .gear = "Canyon", .from = day(2022, 5, 1) }));
}

void test_empty_and_missing_lists()
{
    auto const list = decode_text("{}");
    CHECK(list.has_value() && list->gear.empty() && list->defaults.empty());
}

void test_decode_errors()
{
    CHECK_EQ(error_of("[]"), "it isn't an object");
    CHECK_EQ(error_of(R"({"shoe": []})"), "`shoe` isn't one of shoes, bikes, unfitted, defaults or swaps");
    CHECK_EQ(error_of(R"({"bikes": [], "default": []})"), "`default` isn't one of shoes, bikes, unfitted, defaults or swaps");
    // A field an entry doesn't use is the user's, such as a note.
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A", "note": "New chain in May"}]})"), "");
    CHECK_EQ(error_of(R"({"shoes": 12})"), "`shoes` isn't a list");
    CHECK_EQ(error_of(R"({"shoes": [12]})"), "shoes 1 isn't an object");
    CHECK_EQ(error_of(R"({"bikes": [{"brand": "Canyon"}]})"), "bikes 1 has no `name`");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A"}, {"name": "B", "purchased": "May 2022"}]})"),
        "bikes 2's `purchased` isn't a date, as \"2026-03-14\"");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A", "retired": "2026-02-30"}]})"),
        "bikes 1's `retired` isn't a date, as \"2026-03-14\"");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A", "brand": 12}]})"), "bikes 1's `brand` isn't text");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A", "year": "2022"}]})"), "bikes 1's `year` isn't a number");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A", "start_km": -5}]})"), "bikes 1's `start_km` isn't a number of 0 or more");
    // Unique over shoes and bikes.
    CHECK_EQ(error_of(R"({"shoes": [{"name": "Blue"}], "bikes": [{"name": "Blue"}]})"), "there are two pieces of gear called `Blue`");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A"}], "defaults": [{"for": "bmx", "gear": "A", "from": "2026-01-01"}]})"),
        "default 1's `for` isn't one of running, road, gravel, mountain, tt, indoor, cycling");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A"}], "defaults": [{"for": "road", "gear": "B", "from": "2026-01-01"}]})"),
        "default 1's `gear`, `B`, isn't in `shoes` or `bikes`");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A"}], "defaults": [{"for": "road", "gear": "A"}]})"),
        "default 1 has no `from`, the date it's the default from, as \"2026-03-14\"");
}

void test_read_gear()
{
    TempDir    dir { "gear" };
    auto const path = dir.path / "gear.json";
    auto const none = read_gear(path);
    CHECK(none.has_value() && !none->has_value());

    MUST(write_file_by_name(path.string(), std::string_view { Example }));
    auto const read = read_gear(path);
    CHECK(read.has_value() && read->has_value() && (*read)->gear.size() == 3);

    MUST(write_file_by_name(path.string(), std::string_view { R"({"shoes": [{"name": "A"}, {"name": "A"}]})" }));
    auto const twice = read_gear(path);
    CHECK(!twice.has_value());
    CHECK_EQ(twice.error_or(""), std::format("{} can't be read: there are two pieces of gear called `A`", path.string()));

    MUST(write_file_by_name(path.string(), std::string_view { "{ not JSON" }));
    auto const broken = read_gear(path);
    CHECK(!broken.has_value() && broken.error().starts_with(std::format("{} can't be read: ", path.string())));
}

void test_gear_kinds()
{
    using V = std::vector<GearKind>;
    CHECK(gear_kinds(FIT::sport::Running, { }) == V { Running });
    CHECK(gear_kinds(FIT::sport::Running, sub_sport::trail) == V { Running });
    CHECK(gear_kinds(FIT::sport::Running, sub_sport::virtual_activity) == V { Running });
    CHECK(gear_kinds(FIT::sport::Training, sub_sport::treadmill) == V { Running });
    CHECK(gear_kinds(FIT::sport::Cycling, { }) == V { Cycling });
    CHECK(gear_kinds(FIT::sport::Cycling, sub_sport::generic) == V { Cycling });
    CHECK(gear_kinds(FIT::sport::Cycling, sub_sport::commuting) == V { Cycling });
    CHECK(gear_kinds(FIT::sport::Cycling, sub_sport::road) == (V { Road, Cycling }));
    CHECK(gear_kinds(FIT::sport::Cycling, sub_sport::gravel_cycling) == (V { Gravel, Cycling }));
    CHECK(gear_kinds(FIT::sport::Cycling, sub_sport::cyclocross) == (V { Gravel, Cycling }));
    CHECK(gear_kinds(FIT::sport::Cycling, sub_sport::mountain) == (V { Mountain, Cycling }));
    CHECK(gear_kinds(FIT::sport::Cycling, sub_sport::virtual_activity) == (V { Indoor, Cycling }));
    CHECK(gear_kinds(FIT::sport::Cycling, sub_sport::indoor_cycling) == (V { Indoor, Cycling }));
    CHECK(gear_kinds(FIT::sport::Training, sub_sport::indoor_cycling) == (V { Indoor, Cycling }));
    CHECK(gear_kinds(FIT::sport::Walking, { }).empty());
    CHECK(gear_kinds(FIT::sport::Hiking, { }).empty());
    CHECK(gear_kinds(FIT::sport::Swimming, sub_sport::lap_swimming).empty());
    CHECK(gear_kinds({ }, { }).empty());
}

void test_default_gear()
{
    auto const list = example();
    auto const indoor = std::vector { Indoor, Cycling };
    // The latest entry on or before the day: the bike on the trainer
    // changes with the season.
    CHECK(default_gear(list, indoor, day(2026, 1, 10)) == "Canyon");
    CHECK(default_gear(list, indoor, day(2026, 3, 31)) == "Canyon");
    CHECK(default_gear(list, indoor, day(2026, 4, 1)) == "Gravel");
    CHECK(default_gear(list, indoor, day(2026, 6, 1)) == "Gravel");
    // Nothing before the first entry, nor for a kind without one.
    CHECK(!default_gear(list, indoor, day(2025, 10, 31)).has_value());
    CHECK(!default_gear(list, { Mountain, Cycling }, day(2026, 6, 1)).has_value());
    CHECK(!default_gear(list, { }, day(2026, 6, 1)).has_value());
    CHECK(default_gear(list, { Road, Cycling }, day(2024, 6, 1)) == "Canyon");
}

void test_default_falls_back_to_cycling()
{
    auto list = decoded(R"({
        "bikes": [{ "name": "Canyon" }, { "name": "Gravel" }],
        "defaults": [
            { "for": "cycling", "gear": "Gravel", "from": "2024-01-01" },
            { "for": "road", "gear": "Canyon", "from": "2025-01-01" }
        ]})");
    CHECK(default_gear(list, { Road, Cycling }, day(2024, 6, 1)) == "Gravel");
    CHECK(default_gear(list, { Road, Cycling }, day(2025, 6, 1)) == "Canyon");
    CHECK(default_gear(list, { Indoor, Cycling }, day(2025, 6, 1)) == "Gravel");
    CHECK(default_gear(list, { Cycling }, day(2025, 6, 1)) == "Gravel");
}

// A retirement ends the default: neither an earlier entry nor the
// fallback kind takes its place.
void test_retired_default_is_no_gear()
{
    auto list = decoded(R"({
        "shoes": [{ "name": "Ghost" }, { "name": "Pegasus", "retired": "2026-11-01" }],
        "bikes": [{ "name": "Gravel" }, { "name": "Canyon", "retired": "2026-02-01" }],
        "defaults": [
            { "for": "running", "gear": "Ghost", "from": "2025-06-01" },
            { "for": "running", "gear": "Pegasus", "from": "2026-03-14" },
            { "for": "cycling", "gear": "Gravel", "from": "2024-01-01" },
            { "for": "indoor", "gear": "Canyon", "from": "2025-11-01" }
        ]})");
    // Retired on the day itself, it's still the default that day.
    CHECK(default_gear(list, { Running }, day(2026, 11, 1)) == "Pegasus");
    CHECK(!default_gear(list, { Running }, day(2026, 11, 10)).has_value());
    CHECK(default_gear(list, { Indoor, Cycling }, day(2026, 2, 1)) == "Canyon");
    CHECK(!default_gear(list, { Indoor, Cycling }, day(2026, 3, 5)).has_value());
    // Before the retirement, as before.
    CHECK(default_gear(list, { Running }, day(2026, 1, 1)) == "Ghost");
}

void test_activity_gear()
{
    auto const list = example();
    auto const road = std::vector { Road, Cycling };
    auto const on = day(2026, 6, 1);
    CHECK((activity_gear(list, { }, road, on) == ActivityGear { .name = "Canyon", .from_default = true }));
    CHECK(activity_gear(list, GearPick { .name = "Gravel" }, road, on) == ActivityGear { .name = "Gravel" });
    // A pick of no gear isn't overridden by the default.
    CHECK(!activity_gear(list, GearPick { }, road, on).has_value());
    // A pick of gear that isn't in the list is kept, and said so.
    auto const old = activity_gear(list, GearPick { .name = "Old Canyon" }, road, on);
    CHECK((old == ActivityGear { .name = "Old Canyon", .unknown = true }));
    CHECK_EQ(gear_text(*old), "Old Canyon (unknown)");
    CHECK_EQ(gear_text(ActivityGear { .name = "Canyon" }), "Canyon");
    CHECK(!activity_gear(list, { }, { Mountain, Cycling }, on).has_value());
}

std::vector<std::string> texts(GearChoices const &choices)
{
    std::vector<std::string> ret;
    for (auto const &choice : choices.choices) {
        ret.push_back(choice.text);
    }
    return ret;
}

using Texts = std::vector<std::string>;

void test_choices_fit_the_activity()
{
    auto list = example();
    list.gear.push_back(Gear { .type = GearType::Shoes, .name = "Ghost" });
    auto const on = day(2026, 6, 1);
    CHECK(texts(gear_choices(list, { Running }, { }, on)) == (Texts { "Default (Pegasus blue)", "None", "Pegasus blue", "Ghost" }));
    CHECK(texts(gear_choices(list, { Road, Cycling }, { }, on)) == (Texts { "Default (Canyon)", "None", "Canyon", "Gravel" }));
    CHECK(texts(gear_choices(list, { }, { }, on)) == (Texts { "Default (none)", "None", "Pegasus blue", "Canyon", "Gravel", "Ghost" }));
    auto const choices = gear_choices(list, { Running }, { }, on);
    CHECK(choices.choices.size() == 4 && !choices.choices[0].pick && choices.choices[1].pick == GearPick { }
        && choices.choices[2].pick == GearPick { .name = "Pegasus blue" });
}

void test_choices_leave_out_retired_gear()
{
    auto const list = example();
    // Pegasus blue retired on 2026-11-01: not offered after.
    CHECK(texts(gear_choices(list, { Running }, { }, day(2026, 11, 1))) == (Texts { "Default (Pegasus blue)", "None", "Pegasus blue" }));
    CHECK(texts(gear_choices(list, { Running }, { }, day(2026, 11, 2))) == (Texts { "Default (none)", "None" }));
    // Unless it's the pick.
    auto const picked = gear_choices(list, { Running }, GearPick { .name = "Pegasus blue" }, day(2026, 12, 1));
    CHECK(texts(picked) == (Texts { "Default (none)", "None", "Pegasus blue" }));
    CHECK_EQ(picked.current, 2uz);
}

void test_choices_current()
{
    auto const list = example();
    auto const on = day(2026, 6, 1);
    CHECK_EQ(gear_choices(list, { Road, Cycling }, { }, on).current, 0uz);
    CHECK_EQ(gear_choices(list, { Road, Cycling }, GearPick { }, on).current, 1uz);
    CHECK_EQ(gear_choices(list, { Road, Cycling }, GearPick { .name = "Gravel" }, on).current, 3uz);
    // Shoes picked for a ride stay its choice.
    auto const shoes = gear_choices(list, { Road, Cycling }, GearPick { .name = "Pegasus blue" }, on);
    CHECK(texts(shoes) == (Texts { "Default (Canyon)", "None", "Pegasus blue", "Canyon", "Gravel" }));
    CHECK_EQ(shoes.current, 2uz);
    // As does gear that isn't in the list any more.
    auto const old = gear_choices(list, { Road, Cycling }, GearPick { .name = "Old Canyon" }, on);
    CHECK(texts(old) == (Texts { "Default (Canyon)", "None", "Canyon", "Gravel", "Old Canyon (unknown)" }));
    CHECK_EQ(old.current, 4uz);
    CHECK(old.choices.size() == 5 && old.choices[4].pick == GearPick { .name = "Old Canyon" });
}

// A summary of a ride or a run at noon on `on`, local time.
ActivitySummary summary(chrono::year_month_day on, FIT::sport sport, std::optional<FIT::sub_sport> sub, float64 km,
    std::optional<u32> leg = { }, std::string file = { })
{
    std::tm tm { };
    tm.tm_year = static_cast<int>(on.year()) - 1900;
    tm.tm_mon = static_cast<int>(static_cast<unsigned>(on.month())) - 1;
    tm.tm_mday = static_cast<int>(static_cast<unsigned>(on.day()));
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    auto const start = static_cast<i64>(std::mktime(&tm));
    if (file.empty()) {
        file = std::format("{:04}/{:02}/{}.fit", static_cast<int>(on.year()), static_cast<unsigned>(on.month()), start);
    }
    return ActivitySummary {
        .source = SummaryFile { .file = file },
        .start = start,
        .sport = sport,
        .sub_sport = sub,
        .distance = km * 1000.0,
        .leg = leg,
    };
}

GearTotal const *row(std::vector<GearTotal> const &totals, std::string_view name)
{
    auto const it = std::ranges::find(totals, name, &GearTotal::name);
    return it == totals.end() ? nullptr : &*it;
}

void test_totals()
{
    auto const        list = example();
    ActivitySummaries summaries {
        summary(day(2026, 4, 10), FIT::sport::Running, { }, 10),
        summary(day(2026, 4, 11), FIT::sport::Cycling, sub_sport::road, 80),
        summary(day(2026, 4, 12), FIT::sport::Cycling, sub_sport::virtual_activity, 30),
        summary(day(2026, 4, 13), FIT::sport::Running, { }, 5),
        summary(day(2026, 4, 14), FIT::sport::Walking, { }, 3),
        summary(day(2026, 4, 15), FIT::sport::Running, { }, 12),
        summary(day(2026, 4, 16), FIT::sport::Cycling, sub_sport::road, 50),
    };
    summaries.push_back(ActivitySummary { .source = SummaryFile { .file = "2026/04/1.fit" } }); // Can't be read
    GearPicks picks;
    picks[pick_key(summaries[3])] = GearPick { };                        // Borrowed shoes
    picks[pick_key(summaries[4])] = GearPick { .name = "Pegasus blue" }; // Shoes count on a walk when picked
    picks[pick_key(summaries[5])] = GearPick { .name = "Old Pegasus" };
    picks[pick_key(summaries[6])] = GearPick { .name = "Gravel" };
    auto const totals = gear_totals(list, summaries, picks, Today);
    CHECK_EQ(totals.size(), 4uz);
    // Shoes, then bikes, as in the file; then the unknown names.
    CHECK(totals.size() == 4 && totals[0].name == "Pegasus blue" && totals[1].name == "Canyon" && totals[2].name == "Gravel" && totals[3].name == "Old Pegasus");
    auto const *shoes = row(totals, "Pegasus blue");
    CHECK(shoes != nullptr && shoes->activities == 2 && shoes->km == 13.0 && !shoes->unknown());
    auto const *canyon = row(totals, "Canyon");
    CHECK(canyon != nullptr && canyon->activities == 1 && canyon->km == 4380.0);
    auto const *gravel = row(totals, "Gravel");
    CHECK(gravel != nullptr && gravel->activities == 2 && gravel->km == 80.0);
    auto const *old = row(totals, "Old Pegasus");
    CHECK(old != nullptr && old->unknown() && old->activities == 1 && old->km == 12.0);
}

void test_retired_at_the_bottom_of_their_group()
{
    auto const list = decoded(R"({
        "shoes": [{ "name": "Old", "retired": "2025-01-01" }, { "name": "New" }],
        "bikes": [{ "name": "Sold", "retired": "2024-01-01" }, { "name": "Canyon", "start_km": 100 }]
    })");
    auto const totals = gear_totals(list, { }, { }, Today);
    CHECK_EQ(totals.size(), 4uz);
    CHECK(totals.size() == 4 && totals[0].name == "New" && totals[1].name == "Old" && totals[2].name == "Canyon" && totals[3].name == "Sold");
    // Gear without activities has its start_km.
    CHECK(totals.size() == 4 && totals[2].activities == 0 && totals[2].km == 100.0);
}

// A multisport file's legs each have their gear, and a file counts once
// for each piece of gear.
void test_totals_of_legs()
{
    auto const        list = decoded(R"({
        "shoes": [{ "name": "Racers" }],
        "bikes": [{ "name": "TT" }],
        "defaults": [{ "for": "running", "gear": "Racers", "from": "2020-01-01" }]
    })");
    auto const        on = day(2026, 7, 5);
    std::string const file = "2026/07/1783245600.fit";
    ActivitySummaries summaries {
        summary(on, FIT::sport::Running, { }, 5, 0, file),
        summary(on, FIT::sport::Transition, { }, 0.2, 1, file),
        summary(on, FIT::sport::Cycling, { }, 40, 2, file),
        summary(on, FIT::sport::Transition, { }, 0.1, 3, file),
        summary(on, FIT::sport::Running, { }, 10, 4, file),
    };
    GearPicks picks;
    picks[pick_key(summaries[2])] = GearPick { .name = "TT" };
    auto const  totals = gear_totals(list, summaries, picks, Today);
    auto const *shoes = row(totals, "Racers");
    CHECK(shoes != nullptr && shoes->activities == 1 && shoes->km == 15.0);
    auto const *bike = row(totals, "TT");
    CHECK(bike != nullptr && bike->activities == 1 && bike->km == 40.0);
}

void test_read_gear_picks()
{
    TempStore const   store;
    auto const        on = day(2026, 7, 5);
    auto const        ride = summary(on, FIT::sport::Cycling, sub_sport::road, 40, { }, "2026/07/100.fit");
    auto const        walk = summary(on, FIT::sport::Walking, { }, 3, { }, "2026/07/200.fit");
    auto const        none = summary(on, FIT::sport::Running, { }, 5, { }, "2026/07/300.fit");
    auto const        leg0 = summary(on, FIT::sport::Swimming, { }, 1, 0, "2026/07/400.fit");
    auto const        leg2 = summary(on, FIT::sport::Cycling, { }, 40, 2, "2026/07/400.fit");
    ActivitySummaries summaries { ride, walk, none, leg0, leg2 };
    fs::create_directories(store.storage.activity_dir / "2026/07");
    auto const sidecar = [&store](std::string_view name) { return store.storage.activity_dir / "2026/07" / name; };
    MUST(save_sidecar_gear(sidecar("100.json"), GearPick { .name = "Canyon" }));
    MUST(save_sidecar_gear(sidecar("200.json"), GearPick { }));
    MUST(save_sidecar_gear(sidecar("400.json"), GearPick { .name = "TT" }, 2));
    auto const picks = read_gear_picks(store.storage, summaries);
    CHECK_EQ(picks.size(), 3uz);
    CHECK(picks.contains(pick_key(ride)) && picks.at(pick_key(ride)) == GearPick { .name = "Canyon" });
    CHECK(picks.contains(pick_key(walk)) && picks.at(pick_key(walk)) == GearPick { });
    CHECK(!picks.contains(pick_key(none)));
    CHECK(!picks.contains(pick_key(leg0)));
    CHECK(picks.contains(pick_key(leg2)) && picks.at(pick_key(leg2)) == GearPick { .name = "TT" });
}

// Entries in any order in the file: the latest by date counts, and of two
// on the same day the one later in the file.
void test_defaults_in_any_order()
{
    auto const list = decoded(R"({
        "bikes": [{ "name": "A" }, { "name": "B" }, { "name": "C" }],
        "defaults": [
            { "for": "cycling", "gear": "C", "from": "2026-06-01" },
            { "for": "cycling", "gear": "A", "from": "2025-01-01" },
            { "for": "cycling", "gear": "B", "from": "2026-06-01" }
        ]})");
    CHECK(default_gear(list, { Cycling }, day(2025, 12, 31)) == "A");
    CHECK(default_gear(list, { Cycling }, day(2026, 5, 31)) == "A");
    CHECK(default_gear(list, { Cycling }, day(2026, 6, 1)) == "B");
    CHECK(default_gear(list, { Cycling }, day(2030, 1, 1)) == "B");
}

// A default naming retired gear gives no gear in the totals either, and a
// pick of null isn't overridden by it.
void test_totals_without_retired_default()
{
    auto const        list = decoded(R"({
        "shoes": [{ "name": "Old", "retired": "2026-04-01", "start_km": 10 }],
        "defaults": [{ "for": "running", "gear": "Old", "from": "2026-01-01" }]
    })");
    ActivitySummaries summaries {
        summary(day(2026, 3, 31), FIT::sport::Running, { }, 5),
        summary(day(2026, 4, 1), FIT::sport::Running, { }, 6),
        summary(day(2026, 4, 2), FIT::sport::Running, { }, 7),
    };
    auto const totals = gear_totals(list, summaries, { }, Today);
    CHECK_EQ(totals.size(), 1uz);
    CHECK(totals.size() == 1 && totals[0].activities == 2 && totals[0].km == 21.0);
}

void test_walking_on_a_treadmill_is_a_run()
{
    CHECK(gear_kinds(FIT::sport::Walking, sub_sport::treadmill) == std::vector { Running });
    CHECK(gear_kinds(FIT::sport::Walking, { }).empty());
    CHECK(gear_kinds(FIT::sport::Hiking, sub_sport::generic).empty());
    CHECK(gear_kinds({ }, { }).empty());
}

}

int main()
{
    test_decode_the_example();
    test_empty_and_missing_lists();
    test_decode_errors();
    test_read_gear();
    test_gear_kinds();
    test_default_gear();
    test_default_falls_back_to_cycling();
    test_retired_default_is_no_gear();
    test_activity_gear();
    test_choices_fit_the_activity();
    test_choices_leave_out_retired_gear();
    test_choices_current();
    test_totals();
    test_retired_at_the_bottom_of_their_group();
    test_totals_of_legs();
    test_read_gear_picks();
    test_defaults_in_any_order();
    test_totals_without_retired_default();
    test_walking_on_a_treadmill_is_a_run();
    return ST::Test::test_result();
}
