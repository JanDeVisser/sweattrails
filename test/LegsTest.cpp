/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests loading a multisport FIT file, one session per leg, and showing a
 * leg as an activity of its own: see specs/56-multisport.md.
 */

#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <IO.h>

#include <storage/Activity.h>
#include <storage/Legs.h>
#include <storage/Storage.h>

#include <TempDir.h>
#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using namespace ST::Test;

namespace fs = std::filesystem;

namespace {

constexpr u8 Running = 1;
constexpr u8 Cycling = 2;
constexpr u8 Transition = 3;
constexpr u8 Swimming = 5;
constexpr u8 Generic = 0;
constexpr u8 Walking = 11;

i64 unix_start(u32 offset = 0)
{
    return static_cast<i64>(Start) + DateTime::FIT_TIMESTAMP_OFFSET + offset;
}

// A leg's session: its sport, when it starts after Start, how long, how
// far, its ascent, and, if given, which of the file's laps are its.
struct TestLeg {
    u8                 sport { Running };
    u32                start { 0 };
    u32                count { 600 };
    u32                distance { 0 }; // m
    u16                ascent { 0 };
    std::optional<u16> first_lap { };
    std::optional<u16> laps { };
};

void add_leg(FITBuilder &fit, TestLeg const &leg)
{
    fit.definition(0, 18,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 2, .size = 4, .base_type = Uint32 },
            { .num = 5, .size = 1, .base_type = Enum },
            { .num = 7, .size = 4, .base_type = Uint32 },
            { .num = 8, .size = 4, .base_type = Uint32 },
            { .num = 9, .size = 4, .base_type = Uint32 },
            { .num = 22, .size = 2, .base_type = Uint16 },
            { .num = 25, .size = 2, .base_type = Uint16 },
            { .num = 26, .size = 2, .base_type = Uint16 },
        });
    fit.message(0);
    fit.u32_le(Start + leg.start + leg.count);
    fit.u32_le(Start + leg.start);
    fit.byte(leg.sport);
    fit.u32_le(leg.count * 1000);
    fit.u32_le(leg.count * 1000);
    fit.u32_le(leg.distance * 100);
    fit.u16_le(leg.ascent);
    // 0xFFFF: FIT's invalid value, as a device that doesn't say writes it.
    fit.u16_le(leg.first_lap.value_or(0xFFFF));
    fit.u16_le(leg.laps.value_or(0xFFFF));
}

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

// A triathlon of 2400 s with a record a second: swim 0-600 s in two laps,
// T1 600-660, bike 660-1800 in two laps, T2 1800-1830, run 1830-2400. The
// sessions come last, as a Garmin writes them, and say which laps are
// theirs if `lap_index`.
std::string triathlon(bool lap_index = true)
{
    FITBuilder fit;
    add_file_id(fit, 1);
    add_records(fit, 2400, 120, 200);
    add_lap(fit, 0, 300);
    add_lap(fit, 300, 300);
    add_lap(fit, 600, 60);
    add_lap(fit, 660, 540);
    add_lap(fit, 1200, 600);
    add_lap(fit, 1800, 30);
    add_lap(fit, 1830, 570);
    auto const laps = [lap_index](u16 first, u16 count) {
        return lap_index ? std::pair { std::optional<u16> { first }, std::optional<u16> { count } } : std::pair<std::optional<u16>, std::optional<u16>> { };
    };
    auto const leg = [&fit, &laps](u8 sport, u32 start, u32 count, u32 distance, u16 ascent, u16 first, u16 lap_count) {
        auto const [first_lap, num_laps] = laps(first, lap_count);
        add_leg(fit, TestLeg { .sport = sport, .start = start, .count = count, .distance = distance, .ascent = ascent, .first_lap = first_lap, .laps = num_laps });
    };
    leg(Swimming, 0, 600, 1000, 0, 0, 2);
    leg(Transition, 600, 60, 100, 0, 2, 1);
    leg(Cycling, 660, 1140, 10000, 50, 3, 2);
    leg(Transition, 1800, 30, 50, 0, 5, 1);
    leg(Running, 1830, 570, 2000, 10, 6, 1);
    return fit.file();
}

struct Loaded {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    bool     loaded { false };

    explicit Loaded(std::string const &file, LoadingDepth depth = LoadingDepth::Deep)
    {
        activity.depth = depth;
        auto const res = activity.load_from_slice(file);
        loaded = res.has_value() && *res;
    }
};

void test_triathlon_is_one_multisport_activity()
{
    Loaded t { triathlon() };
    CHECK(t.loaded);
    auto const &a = t.activity;
    CHECK(a.is_multisport());
    CHECK(a.sport == FIT::sport::Multisport);
    CHECK(!a.sub_sport);
    CHECK_EQ(a.legs.size(), 5uz);
    CHECK_EQ(static_cast<i64>(a.segment.start_time.timestamp), unix_start());
    CHECK_EQ(static_cast<i64>(a.segment.end_time.timestamp), unix_start(2400));
    CHECK_EQ(a.segment.distance, 13150.0f);
    CHECK_EQ(a.segment.elapsed.elapsed, 2400u);
    CHECK_EQ(a.segment.moving.elapsed, 2400u);
    CHECK(a.segment.total_ascent == std::optional<u16> { 60 });
    std::vector<std::optional<FIT::sport>> sports;
    for (auto const &leg : a.legs) {
        sports.push_back(leg.sport);
    }
    CHECK(sports == (std::vector<std::optional<FIT::sport>> { FIT::sport::Swimming, FIT::sport::Transition, FIT::sport::Cycling, FIT::sport::Transition, FIT::sport::Running }));
}

// The month's list loads it shallow: it's a multisport activity there too,
// for the icon.
void test_shallow_load_is_multisport()
{
    Loaded t { triathlon(), LoadingDepth::Shallow };
    CHECK(t.loaded);
    CHECK(t.activity.sport == FIT::sport::Multisport);
    CHECK_EQ(t.activity.segment.distance, 13150.0f);
    CHECK(t.activity.laps.empty());
}

void test_legs_have_their_records()
{
    Loaded                                 t { triathlon() };
    auto const                            &legs = t.activity.legs;
    std::vector<std::pair<size_t, size_t>> records;
    for (auto const &leg : legs) {
        records.emplace_back(leg.first_record, leg.end_record);
    }
    CHECK(records == (std::vector<std::pair<size_t, size_t>> { { 0, 600 }, { 600, 660 }, { 660, 1800 }, { 1800, 1830 }, { 1830, 2400 } }));
    // Their stats come from their records.
    CHECK(legs[2].segment.computed_power_range.range.has_value());
    CHECK(legs[2].segment.hr_load.has_value());
}

void check_leg_laps(Activity const &a)
{
    std::vector<size_t> counts;
    for (auto const &leg : a.legs) {
        counts.push_back(leg.laps.size());
    }
    CHECK(counts == (std::vector<size_t> { 2, 1, 2, 1, 1 }));
    CHECK_EQ(static_cast<i64>(a.legs[2].laps[1].segment.start_time.timestamp), unix_start(1200));
    CHECK_EQ(a.legs[2].laps[1].first_record, 1200uz);
}

void test_legs_have_their_laps_by_index()
{
    Loaded t { triathlon(true) };
    check_leg_laps(t.activity);
}

void test_legs_have_the_laps_starting_in_them_without_an_index()
{
    Loaded t { triathlon(false) };
    check_leg_laps(t.activity);
}

void test_whole_laps_are_the_legs()
{
    Loaded                   t { triathlon() };
    auto const              &a = t.activity;
    std::vector<std::string> titles;
    for (auto const &lap : a.laps) {
        titles.push_back(lap.title);
    }
    CHECK(titles == (std::vector<std::string> { "Swim", "T1", "Bike", "T2", "Run" }));
    CHECK_EQ(a.laps[2].first_record, 660uz);
    CHECK_EQ(a.laps[2].end_record, 1800uz);
    CHECK_EQ(a.laps[2].segment.distance, 10000.0f);
    // They're the device's laps, which the user's replace and come back to.
    CHECK_EQ(a.device_laps.size(), 5uz);
    CHECK(a.lap_starts() == (std::vector<i64> { unix_start(600), unix_start(660), unix_start(1800), unix_start(1830) }));
}

void test_brick_without_transitions_is_multisport()
{
    FITBuilder fit;
    add_file_id(fit, 1);
    add_records(fit, 1200, 120, 200);
    add_leg(fit, TestLeg { .sport = Cycling, .start = 0, .count = 900, .distance = 8000 });
    add_leg(fit, TestLeg { .sport = Running, .start = 900, .count = 300, .distance = 1000 });
    Loaded t { fit.file() };
    CHECK(t.activity.is_multisport());
    CHECK(t.activity.sport == FIT::sport::Multisport);
    CHECK(leg_tabs(t.activity) == (std::vector<size_t> { 0, 1 }));
}

void test_one_session_is_no_multisport()
{
    FITBuilder fit;
    add_file_id(fit, 1);
    add_records(fit, 600, 120, 200);
    add_lap(fit, 0, 300);
    add_lap(fit, 300, 300);
    add_leg(fit, TestLeg { .sport = Cycling, .start = 0, .count = 600, .distance = 5000, .ascent = 20 });
    Loaded      t { fit.file() };
    auto const &a = t.activity;
    CHECK(!a.is_multisport());
    CHECK(a.sport == FIT::sport::Cycling);
    CHECK_EQ(a.segment.distance, 5000.0f);
    CHECK(a.segment.total_ascent == std::optional<u16> { 20 });
    CHECK_EQ(a.laps.size(), 2uz);
    CHECK(a.laps[0].title.empty());
    CHECK_EQ(static_cast<i64>(filing_start(a).timestamp), unix_start());
}

void test_leg_names()
{
    FITBuilder fit;
    add_file_id(fit, 1);
    add_leg(fit, TestLeg { .sport = Running, .start = 0, .count = 300 });
    add_leg(fit, TestLeg { .sport = Generic, .start = 300, .count = 180 });
    add_leg(fit, TestLeg { .sport = Transition, .start = 480, .count = 20 });
    add_leg(fit, TestLeg { .sport = Walking, .start = 500, .count = 300 });
    add_leg(fit, TestLeg { .sport = Transition, .start = 800, .count = 20 });
    add_leg(fit, TestLeg { .sport = Swimming, .start = 820, .count = 300 });
    Loaded                   t { fit.file() };
    std::vector<std::string> names;
    for (size_t ix = 0; ix < t.activity.legs.size(); ++ix) {
        names.push_back(leg_name(t.activity, ix));
    }
    CHECK(names == (std::vector<std::string> { "Run", "Other", "T1", "Walking", "T2", "Swim" }));
}

void test_tabs_leave_out_transitions()
{
    Loaded t { triathlon() };
    CHECK(leg_tabs(t.activity) == (std::vector<size_t> { 0, 2, 4 }));
}

void test_tabs()
{
    Loaded t { triathlon() };
    CHECK(tab_labels(t.activity) == (std::vector<std::string> { "Whole", "Swim", "Bike", "Run" }));
    CHECK(tab_activity(t.activity, 0).is_multisport());
    CHECK(!tab_activity(t.activity, 0).leg);
    auto const run = tab_activity(t.activity, 3);
    CHECK(run.sport == FIT::sport::Running);
    CHECK(run.leg.has_value() && run.leg->number == 3);
}

// A leg's best opens on its tab; a transition's, which has none, on
// Whole.
void test_tab_of_leg()
{
    Loaded t { triathlon() };
    CHECK_EQ(tab_of_leg(t.activity, 0), 1uz);
    CHECK_EQ(tab_of_leg(t.activity, 1), 0uz);
    CHECK_EQ(tab_of_leg(t.activity, 2), 2uz);
    CHECK_EQ(tab_of_leg(t.activity, 4), 3uz);
}

void test_leg_activity()
{
    Loaded t { triathlon() };
    CHECK_EQ(leg_activity(t.activity, 2).title, "Bike");
    t.activity.title = "IM Test";
    t.activity.legs[2].title = "IM Test | Bike";
    t.activity.legs[2].notes = "Hot";
    auto const bike = leg_activity(t.activity, 2);
    CHECK(bike.sport == FIT::sport::Cycling);
    CHECK(!bike.is_multisport());
    CHECK(bike.legs.empty());
    CHECK_EQ(bike.title, "IM Test | Bike");
    CHECK_EQ(bike.notes, "Hot");
    CHECK(bike.leg.has_value() && bike.leg->index == 2);
    CHECK_EQ(bike.segment.distance, 10000.0f);
    CHECK_EQ(bike.segment.elapsed.elapsed, 1140u);
    CHECK_EQ(static_cast<i64>(bike.segment.start_time.timestamp), unix_start(660));
    CHECK_EQ(bike.records.size(), 1140uz);
    CHECK_EQ(bike.recorded.size(), 1140uz);
    CHECK_EQ(static_cast<i64>(bike.recorded.front().timestamp.timestamp), unix_start(660));
    CHECK_EQ(bike.laps.size(), 2uz);
    CHECK_EQ(bike.laps[0].first_record, 0uz);
    CHECK_EQ(bike.laps[0].end_record, 540uz);
    CHECK_EQ(bike.laps[1].first_record, 540uz);
    CHECK_EQ(bike.laps[1].end_record, 1140uz);
    CHECK_EQ(bike.device_laps.size(), 2uz);
    CHECK(!bike.user_lap_starts);
    CHECK(bike.leg.has_value());
    CHECK_EQ(bike.leg->name, "Bike");
    CHECK_EQ(bike.leg->number, 2uz);
    CHECK_EQ(bike.leg->count, 3uz);
    // Laps the user made on Whole aren't the leg's.
    t.activity.user_lap_starts = std::vector<i64> { unix_start(100) };
    CHECK(!leg_activity(t.activity, 2).user_lap_starts);
}

// The last lap of a leg runs up to the next leg's first lap: the leg's
// records cut it off.
void test_leg_laps_stay_within_the_leg()
{
    Loaded     t { triathlon() };
    auto const swim = leg_activity(t.activity, 0);
    CHECK_EQ(swim.records.size(), 600uz);
    for (auto const &lap : swim.laps) {
        CHECK(lap.end_record <= swim.records.size());
    }
}

void test_inbox_files_by_the_last_legs_start()
{
    TempStore store;
    MUST(write_file_by_name((store.storage.inbox_dir / "race.fit").string(), std::string_view { triathlon() }));
    std::optional<i64> filed_at;
    auto const         filed = store.storage.process_inbox({ }, [&filed_at](std::string_view, Storage::InboxFiling const &filing) {
        if (filing.filed()) {
            filed_at = filing.start;
        }
    });
    CHECK(filed.has_value());
    CHECK(filed_at == std::optional<i64> { unix_start(1830) });
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start(1830)));
    auto const dir = store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1);
    CHECK(fs::exists(dir / std::format("{}.fit", unix_start(1830))));
}

// The reviewer's: laps the user made on Whole replace the legs there, the
// device's laps, which are the legs, come back when they're removed, and a
// leg's tab keeps the device's laps either way.
void test_user_laps_replace_the_legs_and_go_back()
{
    TempDir dir { "legs" };
    MUST(write_file_by_name((dir.path / "dummy.fit").string(), std::string_view { triathlon() }));
    MUST(write_file_by_name((dir.path / "dummy.json").string(), std::format(R"({{"laps": [{}]}})", unix_start(1200))));
    Storage  storage { };
    Activity a { storage, ActivityID::dummy(ActivityFile::Fit) };
    a.depth = LoadingDepth::Deep;
    CHECK(a.load_from_dir(dir.path).value_or(false));
    CHECK_EQ(a.laps.size(), 2uz);
    CHECK(a.laps[0].title.empty());
    CHECK_EQ(a.device_laps.size(), 5uz);
    CHECK_EQ(a.device_laps[2].title, "Bike");
    auto const bike = leg_activity(a, 2);
    CHECK_EQ(bike.laps.size(), 2uz);
    CHECK(!bike.user_lap_starts);
    CHECK(a.set_lap_starts(std::nullopt).has_value());
    CHECK_EQ(a.laps.size(), 5uz);
    if (a.laps.size() == 5) {
        CHECK_EQ(a.laps[4].title, "Run");
        CHECK_EQ(a.laps[4].first_record, 1830uz);
    }
}

// The triathlon in a directory of its own, with `sidecar`, loaded as the
// store loads it.
struct TriathlonInDir {
    TempDir  dir { "leg-titles" };
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };

    explicit TriathlonInDir(std::string_view sidecar)
    {
        MUST(write_file_by_name((dir.path / "dummy.fit").string(), std::string_view { triathlon() }));
        if (!sidecar.empty()) {
            MUST(write_file_by_name((dir.path / "dummy.json").string(), sidecar));
        }
        activity.depth = LoadingDepth::Deep;
        CHECK(activity.load_from_dir(dir.path).value_or(false));
    }
};

void test_common_title()
{
    std::vector<std::string> const du { "Welland Sprint Du | Run 1", "Welland Sprint Du | Ride", "Welland Sprint Du | Run 2" };
    CHECK_EQ(common_title(du, true).value_or(""), "Welland Sprint Du");
    std::vector<std::string> const im { "IM Ottawa Swim", "IM Ottawa Ride", "IM Ottawa Run" };
    CHECK_EQ(common_title(im, true).value_or(""), "IM Ottawa");
    std::vector<std::string> const separators { "Race - Swim", "Race - Bike", "Race · Run" };
    CHECK_EQ(common_title(separators, true).value_or(""), "Race");
    std::vector<std::string> const colon { "Tri: swim", "Tri: bike" };
    CHECK_EQ(common_title(colon, true).value_or(""), "Tri");
    // Whole words: not "Morning R".
    std::vector<std::string> const morning { "Morning Run", "Morning Ride" };
    CHECK_EQ(common_title(morning, true).value_or(""), "Morning");
    std::vector<std::string> const one_word { "Swimbikerun", "Swimming" };
    CHECK_EQ(common_title(one_word, true).value_or(""), "Swimbikerun");
    std::vector<std::string> const apart { "Swim in the bay", "Bike", "Run" };
    CHECK_EQ(common_title(apart, true).value_or(""), "Swim in the bay");
    // Not halfway through "é" or "è", which start with the same byte.
    std::vector<std::string> const accents { "Café du lac", "Cafè du lac" };
    CHECK_EQ(common_title(accents, true).value_or(""), "Café du lac");
    CHECK(!common_title(apart, false).has_value());
    CHECK(!common_title({ }, true).has_value());
}

void test_legs_description()
{
    std::vector<LegDescription> const legs {
        { .name = "Swim", .description = "Super choppy\n" },
        { .name = "Bike", .description = "" },
        { .name = "Run", .description = "Hot" },
    };
    CHECK_EQ(legs_description(legs), "Swim: Super choppy\n\nRun: Hot");
    std::vector<LegDescription> const none { { .name = "Swim" }, { .name = "Run", .description = " " } };
    CHECK_EQ(legs_description(none), "");
}

// Each leg's title is its name, then Strava's, then the user's; its
// description Strava's, then the user's. Transitions aren't in Whole's.
void test_leg_names_from_the_sidecar()
{
    TriathlonInDir t { R"({"legs": [
        {"strava": {"id": 1, "name": "IM Test Swim", "description": "Choppy"}},
        {"strava": {"id": 2, "name": "Morning Workout", "description": "In T1"}},
        {"strava": {"id": 3, "name": "IM Test Ride"}, "title": "IM Test Bike", "notes": "Windy"},
        {},
        {"notes": "Hot"}
    ]})" };
    auto const    &legs = t.activity.legs;
    CHECK_EQ(legs[0].title, "IM Test Swim");
    CHECK_EQ(legs[0].notes, "Choppy");
    CHECK_EQ(legs[1].title, "Morning Workout");
    CHECK_EQ(legs[2].title, "IM Test Bike");
    CHECK_EQ(legs[2].original_title, "IM Test Ride");
    CHECK_EQ(legs[2].notes, "Windy");
    CHECK_EQ(legs[2].original_notes, "");
    CHECK_EQ(legs[3].title, "T2");
    CHECK_EQ(legs[4].title, "Run");
    CHECK_EQ(legs[4].notes, "Hot");
    // "IM Test Swim", "IM Test Bike" and "Run" have nothing in common.
    CHECK_EQ(t.activity.title, "IM Test Swim");
    CHECK_EQ(t.activity.notes, "Swim: Choppy\n\nBike: Windy\n\nRun: Hot");
    CHECK_EQ(t.activity.original_notes, t.activity.notes);
    auto const bike = tab_activity(t.activity, 2);
    CHECK_EQ(bike.title, "IM Test Bike");
    CHECK_EQ(bike.notes, "Windy");
}

// Whole's title: the service's, then the old "strava", then the legs'
// common title, then the user's. Its description: the legs', then the
// user's.
void test_whole_names()
{
    TriathlonInDir service { R"({"activityName": "Toronto Multisport"})" };
    CHECK_EQ(service.activity.title, "Toronto Multisport");
    CHECK_EQ(service.activity.legs[2].title, "Bike");
    TriathlonInDir old { R"({"activityName": "Toronto Multisport", "strava": {"id": 7, "name": "IM Test Swim", "description": "Choppy"}})" };
    CHECK_EQ(old.activity.title, "IM Test Swim");
    CHECK_EQ(old.activity.notes, "Choppy");
    auto const     legs = R"("legs": [{"strava": {"id": 1, "name": "IM Test Swim"}}, {}, {"strava": {"id": 3, "name": "IM Test Ride"}}, {}, {"strava": {"id": 5, "name": "IM Test Run", "description": "Hot"}}])";
    TriathlonInDir common { std::format(R"({{"activityName": "Toronto Multisport", {}}})", legs) };
    CHECK_EQ(common.activity.title, "IM Test");
    CHECK_EQ(common.activity.original_title, "IM Test");
    CHECK_EQ(common.activity.notes, "Run: Hot");
    TriathlonInDir own { std::format(R"({{"title": "Ironman", "notes": "Mine", {}}})", legs) };
    CHECK_EQ(own.activity.title, "Ironman");
    CHECK_EQ(own.activity.original_title, "IM Test");
    CHECK_EQ(own.activity.notes, "Mine");
    CHECK_EQ(own.activity.original_notes, "Run: Hot");
}

// On a leg's tab, a rename and a description are the leg's own, and
// saving one empty brings back Strava's.
void test_rename_a_leg()
{
    TriathlonInDir t { R"({"legs": [{}, {}, {"strava": {"id": 3, "name": "IM Test Ride", "description": "Flat"}}]})" };
    auto           bike = tab_activity(t.activity, 2);
    CHECK(bike.rename("Bike leg").has_value());
    CHECK(bike.set_notes("Windy").has_value());
    CHECK_EQ(bike.title, "Bike leg");
    auto names = sidecar_titles(t.dir.path / "dummy.json");
    CHECK(!names.own.has_value() && !names.own_notes.has_value());
    CHECK_EQ(names.legs.size(), 3uz);
    CHECK(names.legs.size() == 3 && names.legs[2].own == "Bike leg" && names.legs[2].own_notes == "Windy");
    t.activity.read_names();
    CHECK_EQ(t.activity.legs[2].title, "Bike leg");
    bike.read_names();
    CHECK_EQ(bike.title, "Bike leg");
    CHECK_EQ(bike.notes, "Windy");
    CHECK(bike.rename("").has_value());
    CHECK(bike.set_notes("").has_value());
    CHECK_EQ(bike.title, "IM Test Ride");
    CHECK_EQ(bike.notes, "Flat");
    names = sidecar_titles(t.dir.path / "dummy.json");
    CHECK(names.legs.size() == 3 && !names.legs[2].own && !names.legs[2].own_notes);
}

void test_best_leg_title()
{
    TriathlonInDir t { R"({"title": "IM Test", "legs": [{"strava": {"id": 1, "name": "IM Test Swim"}}]})" };
    CHECK_EQ(best_leg_title(t.activity, 0), "IM Test Swim");
    CHECK_EQ(best_leg_title(t.activity, 2), "IM Test · Bike");
}

// The reviewer's: a workout message doesn't turn a multisport activity
// into the workout's sport.
void test_workout_sport_doesnt_override_multisport()
{
    FITBuilder fit;
    add_file_id(fit, 1);
    add_workout(fit, "Race");
    add_records(fit, 1200, 120, 200);
    add_leg(fit, TestLeg { .sport = Cycling, .start = 0, .count = 600, .distance = 5000 });
    add_leg(fit, TestLeg { .sport = Running, .start = 600, .count = 600, .distance = 2000 });
    Loaded t { fit.file() };
    CHECK(t.activity.sport == FIT::sport::Multisport);
    CHECK_EQ(t.activity.title, "Race");
}

// The reviewer's: no leg has an ascent, so the whole has none; one has, and
// it's the sum of those that do.
// IM Ottawa's legs add up to 13:28:59 with their fractions of a second,
// and to 13:28:57 in whole seconds.
void test_whole_time_adds_up_fractions()
{
    std::vector<Leg> legs(3);
    for (auto &leg : legs) {
        leg.segment.elapsed = Duration { 10.7 };
        leg.segment.moving = Duration { 9.4 };
    }
    auto const whole = whole_segment(legs);
    CHECK_EQ(whole.elapsed.elapsed, 32u);
    CHECK_EQ(whole.moving.elapsed, 28u);
}

void test_whole_ascent()
{
    std::vector<Leg> legs(2);
    CHECK(!whole_segment(legs).total_ascent);
    legs[1].segment.total_ascent = 7;
    CHECK(whole_segment(legs).total_ascent == std::optional<u16> { 7 });
    CHECK(!whole_segment({ }).total_ascent);
}

}

int main()
{
    test_common_title();
    test_legs_description();
    test_leg_names_from_the_sidecar();
    test_whole_names();
    test_rename_a_leg();
    test_best_leg_title();
    test_triathlon_is_one_multisport_activity();
    test_shallow_load_is_multisport();
    test_legs_have_their_records();
    test_legs_have_their_laps_by_index();
    test_legs_have_the_laps_starting_in_them_without_an_index();
    test_whole_laps_are_the_legs();
    test_brick_without_transitions_is_multisport();
    test_one_session_is_no_multisport();
    test_leg_names();
    test_tabs_leave_out_transitions();
    test_tabs();
    test_tab_of_leg();
    test_leg_activity();
    test_leg_laps_stay_within_the_leg();
    test_inbox_files_by_the_last_legs_start();
    test_user_laps_replace_the_legs_and_go_back();
    test_workout_sport_doesnt_override_multisport();
    test_whole_time_adds_up_fractions();
    test_whole_ascent();
    return test_result();
}
