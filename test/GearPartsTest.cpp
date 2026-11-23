/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests a bike's parts in gear.json: where each is fitted, the distance
 * on each, and the distance on a chain since it was waxed
 * (specs/68-bike-components.md).
 */

#include <chrono>
#include <expected>
#include <string>
#include <string_view>

#include <JSON.h>

#include <storage/Gear.h>

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
    { "name": "Canyon stock wheels", "type": "wheels" },
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
    { "part": "Canyon stock wheels", "on": "Canyon", "from": "2022-05-01" },
    { "part": "105 11-28", "on": "Canyon stock wheels", "from": "2022-05-01" },
    { "part": "GP5000 front", "on": "Canyon stock wheels", "at": "front", "from": "2026-03-01" },
    { "part": "GP5000 rear", "on": "Canyon stock wheels", "at": "rear", "from": "2026-03-01" },
    { "part": "KMC blue", "on": "Canyon", "from": "2026-06-01" },
    { "part": "105 11-34", "on": "Canyon stock wheels", "from": "2026-07-01", "until": "2026-07-12" }
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
    {{ "name": "Wheels", "type": "wheels" }},
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
    CHECK_EQ(list.gear.size(), 9uz);
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
    CHECK_EQ(list.fitted.size(), 7uz);
    CHECK((list.fitted.back() == Fitting { .part = "105 11-34", .on = "Canyon stock wheels", .from = day(2026, 7, 1), .until = day(2026, 7, 12) }));
    CHECK(list.fitted.size() == 7 && list.fitted[3].at == TyrePosition::Front && list.fitted[4].at == TyrePosition::Rear);
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
    CHECK_EQ(error_of(R"({"parts": [{"name": "A"}]})"), "parts 1's `type` isn't one of chainset, wheels, cassette, chain, tyre");
    CHECK_EQ(error_of(R"({"parts": [{"name": "A", "type": "saddle"}]})"), "parts 1's `type` isn't one of chainset, wheels, cassette, chain, tyre");
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
        "fitted 1's `on`, `Nope`, isn't a bike or wheels");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Cassette", "on": "Canyon", "from": "2026-01-01" } ])")),
        "fitted 1: a cassette goes on wheels, and `Canyon` isn't wheels");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Chain", "on": "Wheels", "from": "2026-01-01" } ])")),
        "fitted 1: a chain goes on a bike, and `Wheels` isn't a bike");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Tyre", "on": "Wheels", "from": "2026-01-01" } ])")),
        "fitted 1: a tyre needs `at`, \"front\" or \"rear\"");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Tyre", "on": "Wheels", "at": "middle", "from": "2026-01-01" } ])")),
        "fitted 1's `at` isn't \"front\" or \"rear\"");
    CHECK_EQ(error_of(with(R"("fitted": [ { "part": "Chain", "on": "Canyon", "at": "rear", "from": "2026-01-01" } ])")),
        "fitted 1 has an `at`, but only a tyre has one");
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

}

int main()
{
    test_decode_the_example();
    test_wax_with_a_time();
    test_decode_errors();
    test_choices_leave_out_parts();
    return Test::test_result();
}
