/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the Gear screen without a window (specs/67-gear.md): its rows, its
 * note, and its numbers as the picks and the summaries come in.
 */

#include <chrono>
#include <ctime>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <widget/Gear.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;
namespace chrono = std::chrono;

namespace {

GearList list()
{
    return GearList {
        .gear = {
            Gear { .type = GearType::Shoes, .name = "Pegasus blue", .brand = "Nike", .model = "Pegasus", .version = "41", .purchased = chrono::year { 2026 } / 3 / 14 },
            Gear { .type = GearType::Shoes, .name = "Ghost", .retired = chrono::year { 2026 } / 3 / 1 },
            Gear { .type = GearType::Bike, .name = "Canyon", .brand = "Canyon", .start_km = 4300 },
        },
        .defaults = {
            GearDefault { .kind = GearKind::Cycling, .gear = "Canyon", .from = chrono::year { 2022 } / 5 / 1 },
        },
    };
}

ActivitySummary ride(i64 start, float64 km)
{
    return ActivitySummary {
        .source = SummaryFile { .file = std::format("2026/05/{}.fit", start) },
        .start = start,
        .sport = FIT::sport::Cycling,
        .distance = km * 1000,
    };
}

constexpr chrono::year_month_day Today = chrono::year { 2026 } / chrono::November / chrono::day { 23 };

// Mid-May 2026.
constexpr i64 May = 1778846400;

std::shared_ptr<GearScreen> make_screen()
{
    auto screen = Widget::make<GearScreen>(nullptr);
    screen->set_summaries({ ride(May, 40), ride(May + 86400, 60.4) });
    return screen;
}

void test_rows()
{
    auto const                  old = ride(May + 86400, 10);
    auto const                  totals = gear_totals(list(), { ride(May, 40), old }, { { pick_key(old), GearPick { .name = "Old Canyon" } } }, Today);
    auto const                  rows = gear_rows(totals);
    std::vector<TableRow> const expected {
        { "Pegasus blue", "Nike Pegasus 41", "2026-03-14", "", "0", "0 km", "" },
        { "Ghost", "", "", "", "0", "0 km", "" },
        { "Canyon", "Canyon", "", "", "1", "4340 km", "" },
        { "Unknown: Old Canyon", "", "", "", "1", "10 km", "" },
    };
    CHECK(rows == expected);
    CHECK((gear_dimmed(totals) == std::vector<bool> { false, true, false, false }));
    auto const columns = gear_columns();
    CHECK_EQ(columns.size(), 7uz);
    CHECK(columns.size() == 7 && columns[GearColumn::On].header == "On" && columns[GearColumn::Waxed].header == "Waxed");
    // The version after the model, and what's left out isn't there.
    GearTotal const speed { .name = "Speedy", .gear = Gear { .brand = "Saucony", .model = "Endorphin Speed", .version = "5" } };
    GearTotal const no_brand { .name = "Old", .gear = Gear { .model = "Ghost", .version = "15" } };
    auto const      versions = gear_rows({ speed, no_brand });
    CHECK(versions.size() == 2 && versions[0][1] == "Saucony Endorphin Speed 5" && versions[1][1] == "Ghost 15");
}

Gear part(std::string name, PartType type)
{
    return Gear { .type = GearType::Part, .name = std::move(name), .part = type };
}

chrono::year_month_day const Nov20 = chrono::year { 2026 } / 11 / 20;

// Where a part is, and a chain's distance since its wax.
void test_part_rows()
{
    auto const tyre = GearTotal {
        .name = "GP5000 rear",
        .gear = part("GP5000 rear", PartType::Tyre),
        .on = "Race rear",
    };
    auto const chain = GearTotal {
        .name = "KMC blue",
        .gear = part("KMC blue", PartType::Chain),
        .activities = 12,
        .km = 812.4,
        .on = "Canyon",
        .wax = WaxTotal { .day = Nov20, .km = 212.3 },
    };
    auto const spare = GearTotal { .name = "Spare chain", .gear = part("Spare chain", PartType::Chain) };
    auto const rows = gear_rows({ tyre, chain, spare });
    CHECK(rows.size() == 3);
    if (rows.size() != 3) {
        return;
    }
    CHECK_EQ(rows[0][GearColumn::On], "Race rear");
    CHECK_EQ(rows[0][GearColumn::Waxed], "");
    CHECK((rows[1] == TableRow { "KMC blue", "", "", "Canyon", "12", "812 km", "212 km (20 Nov)" }));
    // Not fitted, and never waxed.
    CHECK_EQ(rows[2][GearColumn::On], "–");
    CHECK_EQ(rows[2][GearColumn::Waxed], "–");
}

void test_highlights()
{
    auto worn = part("Cassette", PartType::Cassette);
    worn.max_km = 100;
    auto chain = part("Chain", PartType::Chain);
    chain.wax_km = 300;
    std::vector<GearTotal> const totals {
        GearTotal { .name = "Cassette", .gear = worn, .km = 150 },
        GearTotal { .name = "Chain", .gear = chain, .km = 1000, .wax = WaxTotal { .day = Nov20, .km = 301 } },
        GearTotal { .name = "New chain", .gear = chain, .km = 301 },
        GearTotal { .name = "Waxed chain", .gear = chain, .km = 1000, .wax = WaxTotal { .day = Nov20, .km = 10 } },
    };
    auto const expected = std::set<TableCell> {
        { 0, GearColumn::Distance },
        { 1, GearColumn::Waxed },
        // Never waxed: its distance is the one since it was new.
        { 2, GearColumn::Distance },
    };
    CHECK(gear_highlights(totals) == expected);
}

void test_note()
{
    CHECK_EQ(gear_note(GearState { .file = "/data/gear.json" }), "No gear yet: write /data/gear.json, as the README says · Esc goes back");
    CHECK_EQ(gear_note(GearState { .gear = list(), .reading = true }), "2 pairs of shoes, 1 bike · Reading which gear the activities used… · Esc goes back");
    CHECK_EQ(gear_note(GearState { .gear = GearList { } }), "0 pairs of shoes, 0 bikes · Esc goes back");
    auto with_parts = list();
    with_parts.gear.push_back(part("Chain", PartType::Chain));
    CHECK_EQ(gear_note(GearState { .gear = with_parts }), "2 pairs of shoes, 1 bike, 1 part · Esc goes back");
    with_parts.gear.push_back(part("Cassette", PartType::Cassette));
    CHECK_EQ(gear_note(GearState { .gear = with_parts }), "2 pairs of shoes, 1 bike, 2 parts · Esc goes back");
    // What's wrong with the file comes first.
    CHECK_EQ(gear_note(GearState { .gear = list(), .reading = true, .error = "gear.json can't be read: oops" }),
        "gear.json can't be read: oops. Showing the gear read before it broke · Esc goes back");
    CHECK_EQ(gear_note(GearState { .error = "gear.json can't be read: oops" }), "gear.json can't be read: oops. No gear until it's fixed · Esc goes back");
}

// Until the picks are read, the gear is listed without numbers; then
// with them, and again as the summaries change.
void test_numbers_come_with_the_picks()
{
    ScopedTestApp app;
    auto          screen = make_screen();
    screen->open(list(), "/data/gear.json", Today);
    CHECK_EQ(screen->table->rows.size(), 3uz);
    CHECK(screen->table->rows.size() == 3 && screen->table->rows[2][GearColumn::Distance].empty());
    CHECK(screen->note->text.contains("Reading"));
    screen->set_picks({ });
    CHECK(screen->table->rows.size() == 3 && screen->table->rows[2][GearColumn::Activities] == "2" && screen->table->rows[2][GearColumn::Distance] == "4400 km");
    CHECK(!screen->note->text.contains("Reading"));
    CHECK(screen->table->is_dimmed(1));
    screen->set_summaries({ ride(May, 40) });
    CHECK(screen->table->rows.size() == 3 && screen->table->rows[2][GearColumn::Activities] == "1" && screen->table->rows[2][GearColumn::Distance] == "4340 km");
    // Opening it again reads the picks again.
    screen->open(list(), "/data/gear.json", Today);
    CHECK(screen->table->rows.size() == 3 && screen->table->rows[2][GearColumn::Distance].empty());
}

// The orange cells come with the numbers, and a chain's Waxed waits for
// them too.
void test_highlights_come_with_the_picks()
{
    ScopedTestApp app;
    auto          screen = make_screen();
    auto          gear = list();
    gear.gear[2].max_km = 4000;
    auto chain = part("Chain", PartType::Chain);
    chain.place = "Canyon";
    chain.waxed.push_back(Wax { .day = chrono::year { 2026 } / 1 / 1, .counts_from = chrono::local_days { chrono::year { 2026 } / 1 / 2 } });
    gear.gear.push_back(chain);
    screen->open(gear, "/data/gear.json", Today);
    CHECK(screen->table->highlighted.empty());
    CHECK(screen->table->rows.size() == 4 && screen->table->rows[3][GearColumn::Waxed].empty());
    CHECK(screen->table->rows.size() == 4 && screen->table->rows[3][GearColumn::On] == "Canyon");
    screen->set_picks({ });
    CHECK(screen->table->is_highlighted(2, GearColumn::Distance));
    CHECK(screen->table->rows.size() == 4 && screen->table->rows[3][GearColumn::Waxed] == "100 km (1 Jan)");
}

bool same(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// A broken gear.json stands out.
void test_error_stands_out()
{
    ScopedTestApp app;
    auto          screen = make_screen();
    screen->open(list(), "/data/gear.json", Today, "gear.json can't be read: oops");
    CHECK(screen->note->text.starts_with("gear.json can't be read: oops"));
    CHECK(same(screen->note->color, ORANGE));
    CHECK_EQ(screen->table->rows.size(), 3uz);
    screen->open(list(), "/data/gear.json", Today);
    CHECK(same(screen->note->color, LIGHTGRAY));
}

void test_without_gear()
{
    ScopedTestApp app;
    auto          screen = make_screen();
    screen->open(std::nullopt, "/data/gear.json", Today);
    screen->set_picks({ });
    CHECK(screen->table->rows.empty());
    CHECK(screen->note->text.starts_with("No gear yet"));
}

void test_step_row()
{
    ScopedTestApp app;
    auto          screen = make_screen();
    screen->open(list(), "/data/gear.json", Today);
    screen->step_row(1);
    CHECK(screen->table->selection == 0uz);
    screen->step_row(5);
    CHECK(screen->table->selection == 2uz);
    screen->step_row(-1);
    CHECK(screen->table->selection == 1uz);
    // The selection stays as the rows are worked out again.
    screen->set_picks({ });
    CHECK(screen->table->selection == 1uz);
}

}

int main()
{
    test_rows();
    test_part_rows();
    test_highlights();
    test_highlights_come_with_the_picks();
    test_note();
    test_numbers_come_with_the_picks();
    test_error_stands_out();
    test_without_gear();
    test_step_row();
    return test_result();
}
