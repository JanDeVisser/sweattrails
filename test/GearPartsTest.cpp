/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests a bike's parts in gear.json: where each is fitted on a day, the
 * distance on each, and the distance on a chain since it was waxed
 * (specs/68-bike-components.md).
 */

#include <algorithm>
#include <chrono>
#include <ctime>
#include <expected>
#include <format>
#include <print>
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

// The spec's example, with a few more parts: a chain replaced, a cassette
// bought for the mountains and swapped in for a trip, and a race wheel
// swapped in for a day.
constexpr char const Example[] = R"({
  "bikes": [
    { "name": "Roadie", "start_km": 4300,
      "fitted": [
        { "name": "Force 48/34", "type": "chainset", "max_km": 30000 },
        { "name": "Flo45 front", "type": "front wheel",
          "fitted": [ { "name": "GP5000 front", "type": "tyre", "since": "2026-03-01" } ] },
        { "name": "Flo45 rear", "type": "rear wheel",
          "fitted": [ { "name": "GP5000 rear", "type": "tyre", "since": "2026-03-01" },
                      { "name": "AXS 10-30", "type": "cassette", "start_km": 100 } ] },
        { "name": "KMC old", "type": "chain", "retired": "2026-05-31" },
        { "name": "Force chain", "type": "chain", "since": "2026-06-01", "max_km": 3000, "wax_km": 300,
          "waxed": [ "2026-11-20", "2026-08-16" ] }
      ] },
    { "name": "Gravel" }
  ],
  "unfitted": [
    { "name": "Flo Disk", "type": "rear wheel",
      "fitted": [ { "name": "GP5000 Disk", "type": "tyre" },
                  { "name": "AXS 10-28", "type": "cassette" } ] },
    { "name": "AXS 10-36", "type": "cassette", "purchased": "2026-07-01" }
  ],
  "defaults": [
    { "for": "road", "gear": "Roadie", "from": "2022-05-01" },
    { "for": "indoor", "gear": "Roadie", "from": "2022-05-01" }
  ],
  "swaps": [
    { "part": "Flo Disk", "on": "Roadie", "from": "2026-08-30", "until": "2026-08-30" },
    { "part": "AXS 10-36", "on": "Flo45 rear", "from": "2026-07-01", "until": "2026-07-12" }
  ]
})";

using Names = std::vector<std::string>;

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

GearList decoded(std::string_view text)
{
    auto list = decode_text(text);
    CHECK(list.has_value());
    if (!list) {
        std::println(stderr, "{}", list.error());
    }
    return list.value_or(GearList { });
}

GearList example()
{
    return decoded(Example);
}

std::string error_of(std::string_view text)
{
    auto const list = decode_text(text);
    return list ? std::string { } : list.error();
}

// A bike with a part of each type, spares in `unfitted`, and `rest` added:
// swaps to try.
std::string with(std::string_view rest)
{
    return std::format(R"({{
  "bikes": [ {{ "name": "Canyon", "fitted": [
    {{ "name": "Front", "type": "front wheel" }},
    {{ "name": "Rear", "type": "rear wheel", "fitted": [ {{ "name": "Cassette", "type": "cassette" }} ] }},
    {{ "name": "Chain", "type": "chain" }}
  ] }} ],
  "unfitted": [
    {{ "name": "Spare rear", "type": "rear wheel" }},
    {{ "name": "Spare cassette", "type": "cassette" }},
    {{ "name": "Spare chain", "type": "chain" }}
  ]
  {}
}})",
        rest.empty() ? std::string { } : std::format(", {}", rest));
}

Gear &gear_of(GearList &list, std::string_view name)
{
    auto const it = std::ranges::find(list.gear, name, &Gear::name);
    CHECK(it != list.gear.end());
    return *it;
}

std::string name_of(Gear const *gear)
{
    return gear == nullptr ? std::string { "none" } : gear->name;
}

Names names(std::vector<Gear const *> const &parts)
{
    Names ret;
    for (auto const *part : parts) {
        ret.push_back(part->name);
    }
    return ret;
}

void test_decode_the_example()
{
    auto const list = example();
    Names      order;
    for (auto const &gear : list.gear) {
        order.push_back(gear.name);
    }
    // Bikes, then the parts depth first: a wheel followed by its own.
    CHECK(order
        == (Names { "Roadie", "Gravel", "Force 48/34", "Flo45 front", "GP5000 front", "Flo45 rear", "GP5000 rear", "AXS 10-30", "KMC old",
            "Force chain", "Flo Disk", "GP5000 Disk", "AXS 10-28", "AXS 10-36" }));
    auto const *chain = list.find("Force chain");
    CHECK(chain != nullptr);
    if (chain != nullptr) {
        CHECK(chain->type == GearType::Part);
        CHECK(chain->part == PartType::Chain);
        CHECK_EQ(chain->place, "Roadie");
        CHECK(chain->since == day(2026, 6, 1));
        CHECK(chain->max_km == 3000.0);
        CHECK(chain->wax_km == 300.0);
        // In order, each counting from the next day.
        CHECK((chain->waxed
            == std::vector {
                Wax { .day = day(2026, 8, 16), .counts_from = chrono::local_days { day(2026, 8, 17) } },
                Wax { .day = day(2026, 11, 20), .counts_from = chrono::local_days { day(2026, 11, 21) } },
            }));
    }
    auto const *bike = list.find("Roadie");
    CHECK(bike != nullptr && !bike->part && bike->place.empty());
    CHECK_EQ(name_of(list.find("Flo45 rear")), "Flo45 rear");
    CHECK(list.find("GP5000 front") != nullptr && list.find("GP5000 front")->place == "Flo45 front");
    CHECK(list.find("Flo Disk") != nullptr && list.find("Flo Disk")->place.empty());
    CHECK(list.find("AXS 10-28") != nullptr && list.find("AXS 10-28")->place == "Flo Disk");
    CHECK_EQ(list.swaps.size(), 2uz);
    CHECK(list.swaps.size() == 2
        && (list.swaps[1] == Swap { .part = "AXS 10-36", .on = "Flo45 rear", .from = day(2026, 7, 1), .until = day(2026, 7, 12) }));
}

void test_wax_with_a_time()
{
    auto const  list = decoded(R"({"unfitted": [{"name": "Chain", "type": "chain", "waxed": ["2026-11-20T08:30"]}]})");
    auto const *chain = list.find("Chain");
    CHECK(chain != nullptr && chain->waxed.size() == 1);
    if (chain != nullptr && chain->waxed.size() == 1) {
        CHECK(chain->waxed[0].counts_from == chrono::local_days { day(2026, 11, 20) } + chrono::hours { 8 } + chrono::minutes { 30 });
        CHECK(chain->waxed[0].day == day(2026, 11, 20));
    }
}

constexpr char const TypeNames[] = "chainset, front wheel, rear wheel, cassette, chain, tyre";

void test_part_errors()
{
    CHECK_EQ(error_of(R"({"unfitted": [{"name": "A"}]})"), std::format("unfitted 1's `type` isn't one of {}", TypeNames));
    CHECK_EQ(error_of(R"({"bikes": [{"name": "B", "fitted": [{"name": "A", "type": "saddle"}]}]})"),
        std::format("fitted 1 on `B`'s `type` isn't one of {}", TypeNames));
    // Unique over shoes, bikes and parts, wherever they're nested.
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A"}], "unfitted": [{"name": "A", "type": "chain"}]})"), "there are two pieces of gear called `A`");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "B", "fitted": [{"name": "C", "type": "chain"}]}], "unfitted": [{"name": "C", "type": "chain"}]})"),
        "there are two pieces of gear called `C`");
    CHECK_EQ(error_of(R"({"shoes": [{"name": "A", "max_km": 0}]})"), "shoes 1's `max_km` isn't a number over 0");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "A", "wax_km": 300}]})"), "bikes 1 has a `wax_km`, but it isn't a chain");
    CHECK_EQ(error_of(R"({"unfitted": [{"name": "A", "type": "chain", "wax_km": "300"}]})"), "unfitted 1's `wax_km` isn't a number over 0");
    CHECK_EQ(error_of(R"({"unfitted": [{"name": "A", "type": "chain", "since": "June"}]})"), "unfitted 1's `since` isn't a date, as \"2026-03-14\"");
    // A part is never an activity's gear.
    CHECK_EQ(error_of(R"({"unfitted": [{"name": "A", "type": "chain"}], "defaults": [{"for": "road", "gear": "A", "from": "2026-01-01"}]})"),
        "default 1's `gear`, `A`, isn't in `shoes` or `bikes`");
    // The old lists.
    for (auto const *key : { "parts", "fitted", "waxed" }) {
        CHECK_EQ(error_of(std::format(R"({{"{}": []}})", key)), std::format("`{}` isn't one of shoes, bikes, unfitted, defaults or swaps", key));
    }
}

void test_wrong_places()
{
    auto const on_bike = [](std::string_view type) {
        return error_of(std::format(R"({{"bikes": [{{"name": "Canyon", "fitted": [{{"name": "P", "type": "{}"}}]}}]}})", type));
    };
    auto const on_wheel = [](std::string_view wheel, std::string_view type) {
        return error_of(std::format(R"({{"unfitted": [{{"name": "W", "type": "{}", "fitted": [{{"name": "P", "type": "{}"}}]}}]}})", wheel, type));
    };
    CHECK_EQ(on_bike("cassette"), "`P`, a cassette, goes on a rear wheel, and `Canyon` isn't one");
    CHECK_EQ(on_bike("tyre"), "`P`, a tyre, goes on a front or a rear wheel, and `Canyon` isn't one");
    CHECK_EQ(on_wheel("front wheel", "cassette"), "`P`, a cassette, goes on a rear wheel, and `W` isn't one");
    CHECK_EQ(on_wheel("rear wheel", "chain"), "`P`, a chain, goes on a bike, and `W` isn't one");
    CHECK_EQ(on_wheel("front wheel", "rear wheel"), "`P`, a rear wheel, goes on a bike, and `W` isn't one");
    CHECK_EQ(on_wheel("rear wheel", "chainset"), "`P`, a chainset, goes on a bike, and `W` isn't one");
    for (auto const *type : { "chainset", "chain", "front wheel", "rear wheel" }) {
        CHECK_EQ(on_bike(type), "");
    }
    CHECK_EQ(on_wheel("front wheel", "tyre"), "");
    CHECK_EQ(on_wheel("rear wheel", "tyre"), "");
    CHECK_EQ(on_wheel("rear wheel", "cassette"), "");
    // In `unfitted` itself, anything goes.
    CHECK_EQ(error_of(R"({"unfitted": [{"name": "P", "type": "tyre"}, {"name": "Q", "type": "chainset"}]})"), "");

    // Only bikes and wheels hold parts.
    for (auto const *type : { "chainset", "chain", "cassette", "tyre" }) {
        CHECK_EQ(error_of(std::format(R"({{"unfitted": [{{"name": "X", "type": "{}", "fitted": []}}]}})", type)),
            "`X` has `fitted`, but only bikes and wheels do");
    }
    CHECK_EQ(error_of(R"({"shoes": [{"name": "S", "fitted": []}]})"), "`S` has `fitted`, but only bikes and wheels do");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "B", "fitted": {}}]})"), "`B`'s `fitted` isn't a list");
    CHECK_EQ(error_of(R"({"unfitted": {}})"), "`unfitted` isn't a list");
}

void test_wax_errors()
{
    CHECK_EQ(error_of(R"({"unfitted": [{"name": "C", "type": "cassette", "waxed": ["2026-01-01"]}]})"), "unfitted 1 has `waxed`, but it isn't a chain");
    CHECK_EQ(error_of(R"({"bikes": [{"name": "B", "waxed": []}]})"), "bikes 1 has `waxed`, but it isn't a chain");
    CHECK_EQ(error_of(R"({"unfitted": [{"name": "C", "type": "chain", "waxed": "2026-01-01"}]})"), "unfitted 1's `waxed` isn't a list");
    for (auto const *on : { R"("yesterday")", R"("2026-01-01T8")", R"("2026-01-01T24:00")", R"("2026-01-01T08:60")", R"("2026-02-30")", "20260101" }) {
        CHECK_EQ(error_of(std::format(R"({{"unfitted": [{{"name": "C", "type": "chain", "waxed": ["2026-01-01", {}]}}]}})", on)),
            "unfitted 1's `waxed` 2 isn't a date, as \"2026-03-14\", or a date and time, as \"2026-03-14T08:00\"");
    }
}

std::string swap_error(std::string_view swaps)
{
    return error_of(with(std::format(R"("swaps": [ {} ])", swaps)));
}

void test_swap_errors()
{
    CHECK_EQ(swap_error(""), "");
    CHECK_EQ(swap_error(R"({ "part": "Nope", "on": "Canyon", "from": "2026-01-01", "until": "2026-01-02" })"),
        "swap 1's `part`, `Nope`, isn't a part in `bikes` or `unfitted`");
    CHECK_EQ(swap_error(R"({ "part": "Canyon", "on": "Canyon", "from": "2026-01-01", "until": "2026-01-02" })"),
        "swap 1's `part`, `Canyon`, isn't a part in `bikes` or `unfitted`");
    CHECK_EQ(swap_error(R"({ "part": "Spare chain", "on": "Nope", "from": "2026-01-01", "until": "2026-01-02" })"),
        "swap 1's `on`, `Nope`, isn't a bike or a wheel");
    CHECK_EQ(swap_error(R"({ "part": "Spare cassette", "on": "Cassette", "from": "2026-01-01", "until": "2026-01-02" })"),
        "swap 1's `on`, `Cassette`, isn't a bike or a wheel");
    CHECK_EQ(swap_error(R"({ "part": "Spare cassette", "on": "Canyon", "from": "2026-01-01", "until": "2026-01-02" })"),
        "swap 1: a cassette goes on a rear wheel, and `Canyon` isn't one");
    CHECK_EQ(swap_error(R"({ "part": "Spare cassette", "on": "Front", "from": "2026-01-01", "until": "2026-01-02" })"),
        "swap 1: a cassette goes on a rear wheel, and `Front` isn't one");
    CHECK_EQ(swap_error(R"({ "part": "Spare chain", "on": "Canyon", "until": "2026-01-02" })"),
        "swap 1 has no `from`, its first day, as \"2026-03-14\"");
    CHECK_EQ(swap_error(R"({ "part": "Spare chain", "on": "Canyon", "from": "2026-01-02" })"),
        "swap 1 has no `until`, its last day, as \"2026-03-14\": a lasting change goes in `fitted`");
    CHECK_EQ(swap_error(R"({ "part": "Spare chain", "on": "Canyon", "from": "2026-01-02", "until": "2026-01-01" })"),
        "swap 1's `until` is before its `from`");
    // A one-day swap is fine.
    CHECK_EQ(swap_error(R"({ "part": "Spare chain", "on": "Canyon", "from": "2026-01-02", "until": "2026-01-02" })"), "");

    // One part in two places at once.
    CHECK_EQ(swap_error(R"({ "part": "Spare rear", "on": "Canyon", "from": "2026-01-01", "until": "2026-01-10" },
                           { "part": "Spare cassette", "on": "Rear", "from": "2026-02-01", "until": "2026-02-10" },
                           { "part": "Spare rear", "on": "Canyon", "from": "2026-01-10", "until": "2026-01-12" })"),
        "swap 1 and swap 3 put `Spare rear` in two places at once");
    // Two parts of a type on one place at once.
    CHECK_EQ(swap_error(R"({ "part": "Spare chain", "on": "Canyon", "from": "2026-01-01", "until": "2026-01-10" },
                           { "part": "Chain", "on": "Canyon", "from": "2026-01-05", "until": "2026-01-05" })"),
        "swap 1 and swap 2 both put a chain on `Canyon` at once");
    // Back to back, or of different types on one place, is fine.
    CHECK_EQ(swap_error(R"({ "part": "Spare chain", "on": "Canyon", "from": "2026-01-01", "until": "2026-01-10" },
                           { "part": "Spare chain", "on": "Canyon", "from": "2026-01-11", "until": "2026-01-12" },
                           { "part": "Spare rear", "on": "Canyon", "from": "2026-01-01", "until": "2026-01-10" })"),
        "");
}

std::string at(GearList const &list, std::string_view place, PartType type, chrono::year_month_day on)
{
    return name_of(part_at(list, place, type, on));
}

std::string place(GearList const &list, std::string_view part, chrono::year_month_day on)
{
    return name_of(place_of(list, part, on));
}

void test_since()
{
    auto const list = example();
    // A tyre isn't there before its `since`, and a part without one, or a
    // `purchased`, always was.
    CHECK_EQ(at(list, "Flo45 front", PartType::Tyre, day(2026, 2, 28)), "none");
    CHECK_EQ(at(list, "Flo45 front", PartType::Tyre, day(2026, 3, 1)), "GP5000 front");
    CHECK_EQ(at(list, "Roadie", PartType::Chainset, day(2000, 1, 1)), "Force 48/34");
    // The old chain until its last day, and the new one from the day of
    // the replacement.
    CHECK_EQ(at(list, "Roadie", PartType::Chain, day(2026, 5, 31)), "KMC old");
    CHECK_EQ(at(list, "Roadie", PartType::Chain, day(2026, 6, 1)), "Force chain");
    CHECK_EQ(place(list, "KMC old", day(2026, 5, 31)), "Roadie");
    CHECK_EQ(place(list, "KMC old", day(2026, 6, 1)), "none");
    CHECK_EQ(place(list, "Force chain", day(2026, 5, 31)), "none");
}

// `purchased` stands in for `since`; `since` wins over it; of two fitted
// on one day, the later in the file wins; a retired part leaves its place
// empty until there's a new one.
void test_since_and_retired()
{
    auto const list = decoded(R"({"bikes": [{"name": "Canyon", "fitted": [
  { "name": "A", "type": "chain" },
  { "name": "B", "type": "chain", "purchased": "2026-02-01" },
  { "name": "C", "type": "chain", "purchased": "2025-01-01", "since": "2026-03-01", "retired": "2026-03-31" },
  { "name": "D", "type": "chain", "since": "2026-05-01" },
  { "name": "E", "type": "chain", "since": "2026-05-01" }
]}]})");
    CHECK_EQ(at(list, "Canyon", PartType::Chain, day(2026, 1, 31)), "A");
    CHECK_EQ(at(list, "Canyon", PartType::Chain, day(2026, 2, 1)), "B");
    CHECK_EQ(at(list, "Canyon", PartType::Chain, day(2026, 3, 1)), "C");
    CHECK_EQ(at(list, "Canyon", PartType::Chain, day(2026, 3, 31)), "C");
    // B doesn't come back.
    CHECK_EQ(at(list, "Canyon", PartType::Chain, day(2026, 4, 1)), "none");
    CHECK_EQ(place(list, "B", day(2026, 4, 1)), "none");
    CHECK_EQ(at(list, "Canyon", PartType::Chain, day(2026, 5, 1)), "E");
}

void test_swaps()
{
    auto const list = example();
    // The 10-36 on the Flo45's rear wheel for its days, both inclusive,
    // and the 10-30 off meanwhile.
    CHECK_EQ(at(list, "Flo45 rear", PartType::Cassette, day(2026, 6, 30)), "AXS 10-30");
    CHECK_EQ(at(list, "Flo45 rear", PartType::Cassette, day(2026, 7, 1)), "AXS 10-36");
    CHECK_EQ(at(list, "Flo45 rear", PartType::Cassette, day(2026, 7, 12)), "AXS 10-36");
    CHECK_EQ(at(list, "Flo45 rear", PartType::Cassette, day(2026, 7, 13)), "AXS 10-30");
    CHECK_EQ(place(list, "AXS 10-30", day(2026, 7, 5)), "none");
    CHECK_EQ(place(list, "AXS 10-36", day(2026, 7, 5)), "Flo45 rear");
    CHECK_EQ(place(list, "AXS 10-36", day(2026, 7, 13)), "none");
    // The race wheel on the bike for a day, with its parts, and the
    // Flo45's rear wheel off, with its own.
    CHECK_EQ(at(list, "Roadie", PartType::RearWheel, day(2026, 8, 30)), "Flo Disk");
    CHECK_EQ(at(list, "Roadie", PartType::RearWheel, day(2026, 8, 31)), "Flo45 rear");
    CHECK_EQ(place(list, "Flo Disk", day(2026, 8, 30)), "Roadie");
    CHECK_EQ(place(list, "Flo45 rear", day(2026, 8, 30)), "none");
    CHECK_EQ(place(list, "AXS 10-28", day(2026, 8, 30)), "Flo Disk");
}

// A swapped-out part's own place is without it, even when the part it
// replaced is still listed there.
void test_swapped_out_leaves_a_gap()
{
    auto const list = decoded(with(R"("swaps": [ { "part": "Cassette", "on": "Spare rear", "from": "2026-01-01", "until": "2026-01-02" } ])"));
    CHECK_EQ(at(list, "Rear", PartType::Cassette, day(2026, 1, 1)), "none");
    CHECK_EQ(at(list, "Spare rear", PartType::Cassette, day(2026, 1, 1)), "Cassette");
    CHECK_EQ(at(list, "Rear", PartType::Cassette, day(2026, 1, 3)), "Cassette");
}

void test_parts_used()
{
    auto const list = example();
    CHECK(names(parts_used(list, "Roadie", day(2026, 6, 15), false))
        == (Names { "Force 48/34", "Force chain", "Flo45 front", "GP5000 front", "Flo45 rear", "AXS 10-30", "GP5000 rear" }));
    CHECK(names(parts_used(list, "Roadie", day(2026, 7, 5), false))
        == (Names { "Force 48/34", "Force chain", "Flo45 front", "GP5000 front", "Flo45 rear", "AXS 10-36", "GP5000 rear" }));
    CHECK(names(parts_used(list, "Roadie", day(2026, 8, 30), false))
        == (Names { "Force 48/34", "Force chain", "Flo45 front", "GP5000 front", "Flo Disk", "AXS 10-28", "GP5000 Disk" }));
    CHECK(names(parts_used(list, "Roadie", day(2026, 6, 15), true)) == (Names { "Force 48/34", "Force chain" }));
    CHECK(parts_used(list, "Gravel", day(2026, 6, 15), false).empty());
}

// A wheel moved for good gets a `since`, so the other bike's rides before
// it don't count for it.
void test_moved_for_good()
{
    auto const list = decoded(R"({"bikes": [
  { "name": "Road", "fitted": [ { "name": "Alloy", "type": "rear wheel" } ] },
  { "name": "Gravel", "fitted": [ { "name": "Carbon", "type": "rear wheel", "since": "2026-04-01",
                                    "fitted": [ { "name": "Cassette", "type": "cassette" } ] } ] }
]})");
    CHECK(names(parts_used(list, "Gravel", day(2026, 3, 31), false)).empty());
    CHECK(names(parts_used(list, "Gravel", day(2026, 4, 1), false)) == (Names { "Carbon", "Cassette" }));
    CHECK(names(parts_used(list, "Road", day(2026, 4, 1), false)) == (Names { "Alloy" }));
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

ActivitySummaries rides()
{
    return {
        ride(day(2026, 2, 15), 10),                                    // The old chain, no tyres yet
        ride(day(2026, 6, 15), 50),                                    // The new chain
        ride(day(2026, 7, 5), 100),                                    // On the 10-36
        ride(day(2026, 8, 1), 30, 18, FIT::sub_sport::indoor_cycling), // No wheels
        ride(day(2026, 8, 30), 40),                                    // On the Flo Disk
        ride(day(2026, 11, 20), 20),                                   // The day of a wax: before it
        ride(day(2026, 11, 22), 40),
    };
}

void test_totals()
{
    auto const list = example();
    auto const totals = gear_totals(list, rides(), { }, Today);
    Names      order;
    for (auto const &total : totals) {
        order.push_back(total.name);
    }
    // Retired parts at the bottom.
    CHECK(order
        == (Names { "Roadie", "Gravel", "Force 48/34", "Flo45 front", "GP5000 front", "Flo45 rear", "GP5000 rear", "AXS 10-30", "Force chain",
            "Flo Disk", "GP5000 Disk", "AXS 10-28", "AXS 10-36", "KMC old" }));
    CHECK_EQ(total_of(totals, "Roadie").km, 4590.0);
    CHECK_EQ(total_of(totals, "Roadie").activities, 7uz);
    CHECK_EQ(total_of(totals, "Force 48/34").km, 290.0);
    CHECK_EQ(total_of(totals, "KMC old").km, 10.0);
    CHECK_EQ(total_of(totals, "Force chain").km, 280.0);
    CHECK_EQ(total_of(totals, "Flo45 front").km, 260.0);
    CHECK_EQ(total_of(totals, "GP5000 front").km, 250.0);
    CHECK_EQ(total_of(totals, "Flo45 rear").km, 220.0);
    CHECK_EQ(total_of(totals, "GP5000 rear").km, 210.0);
    CHECK_EQ(total_of(totals, "AXS 10-30").km, 220.0); // With its start_km
    CHECK_EQ(total_of(totals, "AXS 10-30").activities, 4uz);
    CHECK_EQ(total_of(totals, "AXS 10-36").km, 100.0);
    for (auto const *race : { "Flo Disk", "GP5000 Disk", "AXS 10-28" }) {
        CHECK_EQ(total_of(totals, race).km, 40.0);
        CHECK_EQ(total_of(totals, race).activities, 1uz);
    }

    // Since the wax on the 20th: from the next day.
    CHECK((total_of(totals, "Force chain").wax == WaxTotal { .day = day(2026, 11, 20), .km = 40 }));
    CHECK(!total_of(totals, "KMC old").wax);
}

void test_on()
{
    auto const list = example();
    auto const today = gear_totals(list, { }, { }, Today);
    CHECK_EQ(total_of(today, "Roadie").on, "");
    CHECK_EQ(total_of(today, "Force 48/34").on, "Roadie");
    CHECK_EQ(total_of(today, "Force chain").on, "Roadie");
    CHECK_EQ(total_of(today, "AXS 10-30").on, "Flo45 rear");
    CHECK_EQ(total_of(today, "GP5000 front").on, "Flo45 front");
    // In `unfitted`, on a wheel there, or retired: nowhere.
    CHECK_EQ(total_of(today, "Flo Disk").on, "");
    CHECK_EQ(total_of(today, "GP5000 Disk").on, "");
    CHECK_EQ(total_of(today, "AXS 10-36").on, "");
    CHECK_EQ(total_of(today, "KMC old").on, "");

    // On the race day, the race wheel and its parts are on the bike, and
    // the Flo45's rear wheel and its parts aren't.
    auto const race_day = gear_totals(list, { }, { }, day(2026, 8, 30));
    CHECK_EQ(total_of(race_day, "Flo Disk").on, "Roadie");
    CHECK_EQ(total_of(race_day, "GP5000 Disk").on, "Flo Disk");
    CHECK_EQ(total_of(race_day, "Flo45 rear").on, "");
    CHECK_EQ(total_of(race_day, "AXS 10-30").on, "");
}

// A wax with a time counts the rides that start after it that day.
void test_wax_at_a_time()
{
    auto list = example();
    gear_of(list, "Force chain").waxed = { Wax { .day = day(2026, 11, 20), .counts_from = chrono::local_days { day(2026, 11, 20) } + chrono::hours { 8 } } };
    auto const totals = gear_totals(list, { ride(day(2026, 11, 20), 15, 7), ride(day(2026, 11, 20), 25, 9) }, { }, Today);
    CHECK((total_of(totals, "Force chain").wax == WaxTotal { .day = day(2026, 11, 20), .km = 25 }));
    // A wax after today doesn't count yet.
    auto const before = gear_totals(list, { ride(day(2026, 11, 20), 15, 7) }, { }, day(2026, 11, 19));
    CHECK(!total_of(before, "Force chain").wax);
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
    CHECK_EQ(choices.choices.size(), 4uz); // Default, None, Roadie, Gravel
    auto const chain = gear_choices(list, { GearKind::Road }, GearPick { .name = "Force chain" }, day(2026, 8, 1));
    CHECK(chain.choices.size() == 5 && chain.choices.back().text == "Force chain (unknown)" && chain.current == 4);
    CHECK((activity_gear(list, GearPick { .name = "Force chain" }, { }, day(2026, 8, 1)) == ActivityGear { .name = "Force chain", .unknown = true }));
}

// A sidecar naming a part as the activity's gear (only by hand) is an
// unknown pick: the ride isn't the part's own, and the part isn't counted
// twice.
void test_pick_of_a_part()
{
    auto const list = example();
    auto const summary = ride(day(2026, 8, 1), 50);
    auto const totals = gear_totals(list, { summary }, GearPicks { { pick_key(summary), GearPick { .name = "Force chain" } } }, Today);
    CHECK_EQ(total_of(totals, "Force chain").km, 0.0);
    CHECK_EQ(total_of(totals, "Force chain").activities, 0uz);
    // It's shown as an unknown name instead, after the gear.
    CHECK(!totals.empty() && totals.back().name == "Force chain" && totals.back().unknown());
    CHECK(!totals.empty() && totals.back().km == 50.0 && totals.back().activities == 1);
}

}

int main()
{
    test_decode_the_example();
    test_wax_with_a_time();
    test_part_errors();
    test_wrong_places();
    test_wax_errors();
    test_swap_errors();
    test_since();
    test_since_and_retired();
    test_swaps();
    test_swapped_out_leaves_a_gap();
    test_parts_used();
    test_moved_for_good();
    test_totals();
    test_on();
    test_wax_at_a_time();
    test_limits();
    test_choices_leave_out_parts();
    test_pick_of_a_part();
    return Test::test_result();
}
