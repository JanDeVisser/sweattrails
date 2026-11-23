/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests a bike's parts in gear.json: where each is fitted, the distance
 * on each, and the distance on a chain since it was waxed
 * (specs/68-bike-components.md).
 */

#include <algorithm>
#include <chrono>
#include <ctime>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <JSON.h>

#include <storage/Gear.h>
#include <storage/Volume.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;
namespace chrono = std::chrono;

namespace {

// The spec's example, with the bikes and parts it names.
constexpr char const Example[] = R"({
  "bikes": [
    { "name": "Canyon", "start_km": 4300 },
    { "name": "Gravel" }
  ],
  "parts": [
    { "name": "Ultegra 52/36", "type": "chainset", "max_km": 30000 },
    { "name": "Canyon front", "type": "front wheel" },
    { "name": "Canyon rear", "type": "rear wheel" },
    { "name": "105 11-28", "type": "cassette", "start_km": 100 },
    { "name": "GP5000 front", "type": "tyre" },
    { "name": "GP5000 rear", "type": "tyre" },
    { "name": "KMC blue", "type": "chain", "max_km": 3000, "wax_km": 300 },
    { "name": "105 11-34", "type": "cassette" }
  ],
  "defaults": [
    { "for": "road", "gear": "Canyon", "from": "2022-05-01" },
    { "for": "indoor", "gear": "Canyon", "from": "2022-05-01" }
  ],
  "fitted": [
    { "part": "Ultegra 52/36", "on": "Canyon", "from": "2022-05-01" },
    { "part": "Canyon front", "on": "Canyon", "from": "2022-05-01" },
    { "part": "Canyon rear", "on": "Canyon", "from": "2022-05-01" },
    { "part": "105 11-28", "on": "Canyon rear", "from": "2022-05-01" },
    { "part": "GP5000 front", "on": "Canyon front", "from": "2026-03-01" },
    { "part": "GP5000 rear", "on": "Canyon rear", "from": "2026-03-01" },
    { "part": "KMC blue", "on": "Canyon", "from": "2026-06-01" },
    { "part": "105 11-34", "on": "Canyon rear", "from": "2026-07-01", "until": "2026-07-12" }
  ],
  "waxed": [ { "chain": "KMC blue", "on": "2026-11-20" } ]
})";

chrono::year_month_day day(int y, unsigned m, unsigned d)
{
    return chrono::year { y } / chrono::month { m } / chrono::day { d };
}

std::expected<GearList, std::string> decode_text(std::string_view text)
{
    auto const json = JSONValue::deserialize(text);
    if (!json) {
        return std::unexpected(std::string { "not JSON" });
    }
    return decode_gear(*json);
}

GearList example()
{
    auto list = decode_text(Example);
    CHECK(list.has_value());
    return list.value_or(GearList { });
}

std::string error_of(std::string_view text)
{
    auto const list = decode_text(text);
    return list ? std::string { } : list.error();
}

// `gear.json` with the example's bike and parts, and `rest` added: a
// `fitted` or `waxed` list to try.
std::string with(std::string_view rest)
{
    return std::format(R"({{
  "bikes": [ {{ "name": "Canyon" }} ],
  "parts": [
    {{ "name": "Front", "type": "front wheel" }},
    {{ "name": "Rear", "type": "rear wheel" }},
    {{ "name": "Cassette", "type": "cassette" }},
    {{ "name": "Chain", "type": "chain" }},
    {{ "name": "Tyre", "type": "tyre" }}
  ],
  {}
}})",
        rest);
}

void test_decode_the_example()
{
    auto const list = example();
    CHECK_EQ(list.gear.size(), 10uz);
    auto const *chain = list.find("KMC blue");
    CHECK(chain != nullptr);
    if (chain != nullptr) {
        CHECK(chain->type == GearType::Part);
        CHECK(chain->part == PartType::Chain);
        CHECK(chain->max_km == 3000.0);
        CHECK(chain->wax_km == 300.0);
    }
    auto const *bike = list.find("Canyon");
    CHECK(bike != nullptr && !bike->part && !bike->max_km);
    auto const *rear = list.find("Canyon rear");
    CHECK(rear != nullptr && rear->part == PartType::RearWheel);
    CHECK_EQ(list.fitted.size(), 8uz);
    CHECK((list.fitted.back() == Fitting { .part = "105 11-34", .on = "Canyon rear", .from = day(2026, 7, 1), .until = day(2026, 7, 12) }));
    // A wax on a day counts from the next one.
    CHECK_EQ(list.waxed.size(), 1uz);
    CHECK(list.waxed.size() == 1
        && (list.waxed[0] == Wax { .chain = "KMC blue", .day = day(2026, 11, 20), .counts_from = chrono::local_days { day(2026, 11, 21) } }));
}

void test_wax_with_a_time()
{
    auto const list = decode_text(with(R"("waxed": [ { "chain": "Chain", "on": "2026-11-20T08:30" } ])"));
    CHECK(list.has_value());
    CHECK(list && list->waxed.size() == 1
        && list->waxed[0].counts_from == chrono::local_days { day(2026, 11, 20) } + chrono::hours { 8 } + chrono::minutes { 30 });
    CHECK(list && list->waxed.size() == 1 && list->waxed[0].day == day(2026, 11, 20));
}

void test_decode_errors()
{
    CHECK_EQ(error_of(R"({"parts": [{"name": "A"}]})"), "parts 1's `type` isn't one of chainset, front wheel, rear wheel, cassette, chain, tyre");
    CHECK_EQ(error_of(R"({"parts": [{"name": "A", "type": "saddle"}]})"), "parts 1's `type` isn't one of chainset, front wheel, rear wheel, cassette, chain, tyre");
    // Unique over shoes, bikes and parts.
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A"}], "parts": [{"name": "A", "type": "chain"}]})"), "there are two pieces of gear called `A`");
    CHECK_EQ(error_of(R"({"shoes": [{"name": "A", "max_km": 0}]})"), "shoes 1's `max_km` isn't a number over 0");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A", "wax_km": 300}]})"), "bikes 1 has a `wax_km`, but it isn't a chain");
    CHECK_EQ(error_of(R"({"parts": [{"name": "A", "type": "chain", "wax_km": "300"}]})"), "parts 1's `wax_km` isn't a number over 0");
    // A part is never an activity's gear.
    CHECK_EQ(error_of(R"({"parts": [{"name": "A", "type": "chain"}], "defaults": [{"for": "road", "gear": "A", "from": "2026-01-01"}]})"),
        "default 1's `gear`, `A`, isn't in `shoes` or `bikes`");

    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Nope", "on": "Canyon", "from": "2026-01-01" } ])")),
        "fitted 1's `part`, `Nope`, isn't in `parts`");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Canyon", "on": "Canyon", "from": "2026-01-01" } ])")),
        "fitted 1's `part`, `Canyon`, isn't in `parts`");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Chain", "on": "Nope", "from": "2026-01-01" } ])")),
        "fitted 1's `on`, `Nope`, isn't a bike or a wheel");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Cassette", "on": "Canyon", "from": "2026-01-01" } ])")),
        "fitted 1: a cassette goes on a rear wheel, and `Canyon` isn't one");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Cassette", "on": "Front", "from": "2026-01-01" } ])")),
        "fitted 1: a cassette goes on a rear wheel, and `Front` isn't one");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Chain", "on": "Rear", "from": "2026-01-01" } ])")),
        "fitted 1: a chain goes on a bike, and `Rear` isn't one");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Rear", "on": "Front", "from": "2026-01-01" } ])")),
        "fitted 1: a rear wheel goes on a bike, and `Front` isn't one");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Tyre", "on": "Canyon", "from": "2026-01-01" } ])")),
        "fitted 1: a tyre goes on a front or a rear wheel, and `Canyon` isn't one");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Tyre", "on": "Front", "from": "2026-01-01" } ])")), "");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Tyre", "on": "Rear", "from": "2026-01-01" } ])")), "");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Chain", "on": "Canyon" } ])")),
        "fitted 1 has no `from`, the date it's fitted from, as \"2026-03-14\"");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Chain", "on": "Canyon", "from": "2026-01-10", "until": "2026-01-09" } ])")),
        "fitted 1's `until` is before its `from`");
    // A one-day swap is fine.
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Chain", "on": "Canyon", "from": "2026-01-10", "until": "2026-01-10" } ])")), "");

    CHECK_EQ(error_of(with(R"("waxed": [ { "chain": "Cassette", "on": "2026-01-01" } ])")),
        "waxed 1's `chain`, `Cassette`, isn't a chain in `parts`");
    for (auto const *on : { "yesterday", "2026-01-01T8", "2026-01-01T24:00", "2026-01-01T08:60", "2026-02-30" }) {
        CHECK_EQ(error_of(with(std::format(R"("waxed": [ {{ "chain": "Chain", "on": "{}" }} ])", on))),
            "waxed 1's `on` isn't a date, as \"2026-03-14\", or a date and time, as \"2026-03-14T08:00\"");
    }
}

// The names of `parts`, in order.
std::vector<std::string> names(std::vector<Gear const *> const &parts)
{
    std::vector<std::string> ret;
    for (auto const *part : parts) {
        ret.push_back(part->name);
    }
    return ret;
}

using Names = std::vector<std::string>;

std::string part_name(Gear const *part)
{
    return part == nullptr ? std::string { "none" } : part->name;
}

std::string on_wheels(GearList const &list, std::string_view wheels, PartType type, chrono::year_month_day on)
{
    return part_name(part_at(list, wheels, type, on));
}

void test_part_at()
{
    auto const list = example();
    // Not fitted yet before its date.
    CHECK_EQ(part_name(part_at(list, "Canyon", PartType::Chain, day(2026, 5, 31))), "none");
    CHECK_EQ(part_name(part_at(list, "Canyon", PartType::Chain, day(2026, 6, 1))), "KMC blue");
    CHECK_EQ(part_name(part_at(list, "Canyon rear", PartType::Tyre, day(2026, 3, 1))), "GP5000 rear");
    CHECK_EQ(part_name(part_at(list, "Canyon front", PartType::Tyre, day(2026, 3, 1))), "GP5000 front");
    CHECK_EQ(part_name(part_at(list, "Canyon front", PartType::Tyre, day(2026, 2, 28))), "none");
    // The swap applies up to and including its until, and the cassette
    // before it is back after.
    CHECK_EQ(on_wheels(list, "Canyon rear", PartType::Cassette, day(2026, 6, 30)), "105 11-28");
    CHECK_EQ(on_wheels(list, "Canyon rear", PartType::Cassette, day(2026, 7, 1)), "105 11-34");
    CHECK_EQ(on_wheels(list, "Canyon rear", PartType::Cassette, day(2026, 7, 12)), "105 11-34");
    CHECK_EQ(on_wheels(list, "Canyon rear", PartType::Cassette, day(2026, 7, 13)), "105 11-28");
    CHECK(fitting_of(list, "105 11-34", day(2026, 7, 13)) == nullptr);
    // The 11-28's entry is still in effect during the swap, but it's off.
    CHECK(fitting_of(list, "105 11-28", day(2026, 7, 5)) == nullptr);
    CHECK(fitting_of(list, "105 11-28", day(2026, 7, 13)) != nullptr);
    auto const *fitting = fitting_of(list, "105 11-34", day(2026, 7, 5));
    CHECK(fitting != nullptr && fitting->on == "Canyon rear");
}

// A wheel moved to another bike leaves the first with the wheel it had
// before it, unless that has moved on too.
void test_wheels_move()
{
    auto const list = decode_text(R"({
  "bikes": [ { "name": "Road" }, { "name": "TT" } ],
  "parts": [
    { "name": "Alloy", "type": "rear wheel" },
    { "name": "Carbon", "type": "rear wheel" },
    { "name": "Spare", "type": "rear wheel" }
  ],
  "fitted": [
    { "part": "Alloy", "on": "Road", "from": "2026-01-01" },
    { "part": "Carbon", "on": "Road", "from": "2026-02-01" },
    { "part": "Carbon", "on": "TT", "from": "2026-03-01" },
    { "part": "Alloy", "on": "TT", "from": "2026-04-01", "until": "2026-04-10" },
    { "part": "Spare", "on": "TT", "from": "2026-05-01" },
    { "part": "Spare", "on": "Road", "from": "2026-05-01" }
  ]
})")
                          .value_or(GearList { });
    auto const wheels = [&list](std::string_view bike, chrono::year_month_day on) {
        return part_name(part_at(list, bike, PartType::RearWheel, on));
    };
    CHECK_EQ(wheels("Road", day(2026, 2, 15)), "Carbon");
    CHECK_EQ(wheels("Road", day(2026, 3, 1)), "Alloy");
    CHECK_EQ(wheels("TT", day(2026, 3, 1)), "Carbon");
    // The alloy wheel on the TT bike for ten days: the road bike has
    // none, as the carbon wheel it had before moved on too.
    CHECK_EQ(wheels("TT", day(2026, 4, 5)), "Alloy");
    CHECK_EQ(wheels("Road", day(2026, 4, 5)), "none");
    // After, the alloy wheel goes back where it was before.
    CHECK_EQ(wheels("Road", day(2026, 4, 11)), "Alloy");
    CHECK_EQ(wheels("TT", day(2026, 4, 11)), "Carbon");
    // Two entries on one day: the later in the file wins.
    CHECK_EQ(wheels("Road", day(2026, 5, 1)), "Spare");
    CHECK_EQ(wheels("TT", day(2026, 5, 1)), "Carbon");
}

// A retired part stops counting, and what it replaced doesn't come back.
void test_retired_part()
{
    auto const list = decode_text(R"({
  "bikes": [ { "name": "Canyon" } ],
  "parts": [
    { "name": "Old chain", "type": "chain" },
    { "name": "New chain", "type": "chain", "retired": "2026-03-01" }
  ],
  "fitted": [
    { "part": "Old chain", "on": "Canyon", "from": "2026-01-01" },
    { "part": "New chain", "on": "Canyon", "from": "2026-02-01" }
  ]
})")
                          .value_or(GearList { });
    CHECK_EQ(part_name(part_at(list, "Canyon", PartType::Chain, day(2026, 3, 1))), "New chain");
    CHECK_EQ(part_name(part_at(list, "Canyon", PartType::Chain, day(2026, 3, 2))), "none");
    CHECK(fitting_of(list, "New chain", day(2026, 3, 1)) != nullptr);
    CHECK(fitting_of(list, "New chain", day(2026, 3, 2)) == nullptr);
}

void test_parts_used()
{
    auto const list = example();
    CHECK(names(parts_used(list, "Canyon", day(2026, 6, 1), false))
        == (Names { "Ultegra 52/36", "KMC blue", "Canyon front", "GP5000 front", "Canyon rear", "105 11-28", "GP5000 rear" }));
    CHECK(names(parts_used(list, "Canyon", day(2026, 7, 4), false))
        == (Names { "Ultegra 52/36", "KMC blue", "Canyon front", "GP5000 front", "Canyon rear", "105 11-34", "GP5000 rear" }));
    CHECK(names(parts_used(list, "Canyon", day(2026, 6, 1), true)) == (Names { "Ultegra 52/36", "KMC blue" }));
    CHECK(names(parts_used(list, "Canyon", day(2022, 6, 1), false)) == (Names { "Ultegra 52/36", "Canyon front", "Canyon rear", "105 11-28" }));
    CHECK(parts_used(list, "Gravel", day(2026, 6, 1), false).empty());
}

constexpr chrono::year_month_day Today = chrono::year { 2026 } / chrono::November / chrono::day { 23 };

// A summary of a ride starting at `hour` on `on`, local time.
ActivitySummary ride(chrono::year_month_day on, float64 km, int hour = 12, std::optional<FIT::sub_sport> sub = FIT::sub_sport::road)
{
    std::tm tm { };
    tm.tm_year = static_cast<int>(on.year()) - 1900;
    tm.tm_mon = static_cast<int>(static_cast<unsigned>(on.month())) - 1;
    tm.tm_mday = static_cast<int>(static_cast<unsigned>(on.day()));
    tm.tm_hour = hour;
    tm.tm_isdst = -1;
    auto const start = static_cast<i64>(std::mktime(&tm));
    return ActivitySummary {
        .source = SummaryFile { .file = std::format("{}.fit", start) },
        .start = start,
        .sport = FIT::sport::Cycling,
        .sub_sport = sub,
        .distance = km * 1000.0,
    };
}

GearTotal total_of(std::vector<GearTotal> const &totals, std::string_view name)
{
    auto const it = std::ranges::find(totals, name, &GearTotal::name);
    CHECK(it != totals.end());
    return it == totals.end() ? GearTotal { } : *it;
}

// A tyre moved from the rear wheel to the front keeps its distance, and
// the rear wheel gets a new one.
void test_tyre_moves_to_the_front()
{
    auto list = example();
    list.gear.push_back(Gear { .type = GearType::Part, .name = "New rear", .part = PartType::Tyre });
    list.fitted.push_back(Fitting { .part = "GP5000 rear", .on = "Canyon front", .from = day(2026, 10, 1) });
    list.fitted.push_back(Fitting { .part = "New rear", .on = "Canyon rear", .from = day(2026, 10, 1) });
    CHECK(names(parts_used(list, "Canyon", day(2026, 10, 1), false))
        == (Names { "Ultegra 52/36", "KMC blue", "Canyon front", "GP5000 rear", "Canyon rear", "105 11-28", "New rear" }));
    // The front tyre it replaced is off.
    CHECK(fitting_of(list, "GP5000 front", day(2026, 10, 1)) == nullptr);
    auto const totals = gear_totals(list, { ride(day(2026, 9, 1), 30), ride(day(2026, 10, 2), 20) }, { }, Today);
    CHECK_EQ(total_of(totals, "GP5000 rear").km, 50.0);
    CHECK_EQ(total_of(totals, "GP5000 front").km, 30.0);
    CHECK_EQ(total_of(totals, "New rear").km, 20.0);
    auto const fitted = total_of(totals, "GP5000 rear").fitted;
    CHECK(fitted && fitted->on == "Canyon front");
}

// A tyre goes where its wheel goes, and a part on a wheel that's on no
// bike counts for no ride, though it's still fitted on the wheel.
void test_parts_follow_their_wheel()
{
    auto const list = decode_text(R"({
  "bikes": [ { "name": "Road" }, { "name": "TT" } ],
  "parts": [
    { "name": "Race front", "type": "front wheel" },
    { "name": "Race rear", "type": "rear wheel" },
    { "name": "Race cassette", "type": "cassette" },
    { "name": "Race tyre", "type": "tyre" },
    { "name": "Spare rear", "type": "rear wheel" },
    { "name": "Spare tyre", "type": "tyre" }
  ],
  "fitted": [
    { "part": "Race front", "on": "Road", "from": "2026-01-01" },
    { "part": "Race rear", "on": "Road", "from": "2026-01-01" },
    { "part": "Race cassette", "on": "Race rear", "from": "2026-01-01" },
    { "part": "Race tyre", "on": "Race front", "from": "2026-01-01" },
    { "part": "Spare tyre", "on": "Spare rear", "from": "2026-01-01" },
    { "part": "Race rear", "on": "TT", "from": "2026-03-01" }
  ]
})")
                          .value_or(GearList { });
    // The rear wheel's cassette goes with it to the TT bike, and the road
    // bike keeps its front wheel.
    CHECK(names(parts_used(list, "Road", day(2026, 2, 1), false)) == (Names { "Race front", "Race tyre", "Race rear", "Race cassette" }));
    CHECK(names(parts_used(list, "Road", day(2026, 3, 1), false)) == (Names { "Race front", "Race tyre" }));
    CHECK(names(parts_used(list, "TT", day(2026, 3, 1), false)) == (Names { "Race rear", "Race cassette" }));
    // The spare wheel is on no bike: its tyre is on it, but not used.
    auto const *fitting = fitting_of(list, "Spare tyre", day(2026, 3, 1));
    CHECK(fitting != nullptr && fitting->on == "Spare rear");
    auto const totals = gear_totals(list, { ride(day(2026, 3, 5), 40) }, { }, Today);
    CHECK_EQ(total_of(totals, "Spare tyre").km, 0.0);
    CHECK_EQ(total_of(totals, "Spare rear").km, 0.0);
    CHECK_EQ(total_of(totals, "Race cassette").km, 0.0);
}

void test_totals()
{
    auto const               list = example();
    auto const               totals = gear_totals(list,
        {
            ride(day(2026, 6, 15), 50),
            ride(day(2026, 7, 5), 100), // On the 11-34
            ride(day(2026, 8, 1), 30, 18, FIT::sub_sport::indoor_cycling),
            ride(day(2026, 11, 20), 20), // The day of the wax: before it
            ride(day(2026, 11, 22), 40),
        },
        { }, Today);
    std::vector<std::string> order;
    for (auto const &total : totals) {
        order.push_back(total.name);
    }
    CHECK(order
        == (Names { "Canyon", "Gravel", "Ultegra 52/36", "Canyon front", "Canyon rear", "105 11-28", "GP5000 front", "GP5000 rear", "KMC blue",
            "105 11-34" }));
    CHECK_EQ(total_of(totals, "Canyon").km, 4540.0);
    CHECK_EQ(total_of(totals, "Canyon").activities, 5uz);
    // Indoors too.
    CHECK_EQ(total_of(totals, "Ultegra 52/36").km, 240.0);
    CHECK_EQ(total_of(totals, "KMC blue").km, 240.0);
    // Not indoors.
    CHECK_EQ(total_of(totals, "Canyon front").km, 210.0);
    CHECK_EQ(total_of(totals, "Canyon rear").km, 210.0);
    CHECK_EQ(total_of(totals, "GP5000 rear").km, 210.0);
    // start_km, and not during the swap.
    CHECK_EQ(total_of(totals, "105 11-28").km, 210.0);
    CHECK_EQ(total_of(totals, "105 11-28").activities, 3uz);
    CHECK_EQ(total_of(totals, "105 11-34").km, 100.0);
    CHECK_EQ(total_of(totals, "105 11-34").activities, 1uz);

    // Where each is today.
    auto const rear = total_of(totals, "GP5000 rear").fitted;
    CHECK(rear && rear->on == "Canyon rear");
    CHECK(!total_of(totals, "105 11-34").fitted);
    CHECK(!total_of(totals, "Canyon").fitted);

    // Since the wax on the 20th: from the next day.
    CHECK((total_of(totals, "KMC blue").wax == WaxTotal { .day = day(2026, 11, 20), .km = 40 }));
    CHECK(!total_of(totals, "Canyon").wax);
}

// A wax with a time counts the rides that start after it that day.
void test_wax_at_a_time()
{
    auto list = example();
    list.waxed = { Wax { .chain = "KMC blue", .day = day(2026, 11, 20), .counts_from = chrono::local_days { day(2026, 11, 20) } + chrono::hours { 8 } } };
    auto const totals = gear_totals(list, { ride(day(2026, 11, 20), 15, 7), ride(day(2026, 11, 20), 25, 9) }, { }, Today);
    CHECK((total_of(totals, "KMC blue").wax == WaxTotal { .day = day(2026, 11, 20), .km = 25 }));
    // A wax after today doesn't count yet.
    auto const before = gear_totals(list, { ride(day(2026, 11, 20), 15, 7) }, { }, day(2026, 11, 19));
    CHECK(!total_of(before, "KMC blue").wax);
}

void test_limits()
{
    auto const chain = Gear { .type = GearType::Part, .name = "Chain", .part = PartType::Chain, .max_km = 3000, .wax_km = 300 };
    CHECK(!(GearTotal { .gear = chain, .km = 3000 }).worn());
    CHECK((GearTotal { .gear = chain, .km = 3001 }).worn());
    auto retired = chain;
    retired.retired = day(2026, 1, 1);
    CHECK(!(GearTotal { .gear = retired, .km = 3001 }).worn());
    CHECK(!(GearTotal { .gear = retired, .km = 3001 }).needs_wax());
    // Since its wax, or in all if it was never waxed.
    CHECK(!(GearTotal { .gear = chain, .km = 1000, .wax = WaxTotal { .km = 300 } }).needs_wax());
    CHECK((GearTotal { .gear = chain, .km = 1000, .wax = WaxTotal { .km = 301 } }).needs_wax());
    CHECK((GearTotal { .gear = chain, .km = 301 }).needs_wax());
    // No limits, no warnings; shoes can have one too.
    CHECK(!(GearTotal { .gear = Gear { .name = "Bike" }, .km = 1e6 }).worn());
    CHECK((GearTotal { .gear = Gear { .name = "Shoes", .max_km = 800 }, .km = 801 }).worn());
}

// The picker offers shoes and bikes, never parts.
void test_choices_leave_out_parts()
{
    auto const list = example();
    auto const choices = gear_choices(list, { GearKind::Road, GearKind::Cycling }, { }, day(2026, 8, 1));
    CHECK_EQ(choices.choices.size(), 4uz); // Default, None, Canyon, Gravel
    auto const chain = gear_choices(list, { GearKind::Road }, GearPick { .name = "KMC blue" }, day(2026, 8, 1));
    CHECK(chain.choices.size() == 5 && chain.choices.back().text == "KMC blue (unknown)" && chain.current == 4);
    CHECK((activity_gear(list, GearPick { .name = "KMC blue" }, { }, day(2026, 8, 1)) == ActivityGear { .name = "KMC blue", .unknown = true }));
}

// A sidecar naming a part as the activity's gear (only by hand) is an
// unknown pick: the ride isn't the part's own, and the part isn't counted
// twice.
void test_pick_of_a_part()
{
    auto const list = example();
    auto const summary = ride(day(2026, 8, 1), 50);
    auto const totals = gear_totals(list, { summary }, GearPicks { { pick_key(summary), GearPick { .name = "KMC blue" } } }, Today);
    CHECK_EQ(total_of(totals, "KMC blue").km, 0.0);
    CHECK_EQ(total_of(totals, "KMC blue").activities, 0uz);
    // It's shown as an unknown name instead, after the gear.
    CHECK(!totals.empty() && totals.back().name == "KMC blue" && totals.back().unknown());
    CHECK(!totals.empty() && totals.back().km == 50.0 && totals.back().activities == 1);
}

}

int main()
{
    test_decode_the_example();
    test_wax_with_a_time();
    test_decode_errors();
    test_choices_leave_out_parts();
    test_part_at();
    test_wheels_move();
    test_retired_part();
    test_parts_used();
    test_totals();
    test_tyre_moves_to_the_front();
    test_parts_follow_their_wheel();
    test_wax_at_a_time();
    test_limits();
    test_pick_of_a_part();
    return Test::test_result();
}
