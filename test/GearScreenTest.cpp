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
            Gear { .type = GearType::Shoes, .name = "Pegasus blue", .brand = "Nike", .model = "Pegasus 41", .purchased = chrono::year { 2026 } / 3 / 14 },
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
    auto const                  totals = gear_totals(list(), { ride(May, 40), old }, { { pick_key(old), GearPick { .name = "Old Canyon" } } });
    auto const                  rows = gear_rows(totals);
    std::vector<TableRow> const expected {
        { "Pegasus blue", "Nike Pegasus 41", "2026-03-14", "0", "0 km" },
        { "Ghost", "", "", "0", "0 km" },
        { "Canyon", "Canyon", "", "1", "4340 km" },
        { "Unknown: Old Canyon", "", "", "1", "10 km" },
    };
    CHECK(rows == expected);
    CHECK((gear_dimmed(totals) == std::vector<bool> { false, true, false, false }));
    CHECK_EQ(gear_columns().size(), 5uz);
}

void test_note()
{
    CHECK_EQ(gear_note(GearState { .file = "/data/gear.json" }), "No gear yet: write /data/gear.json, as the README says · Esc goes back");
    CHECK_EQ(gear_note(GearState { .gear = list(), .reading = true }), "2 pairs of shoes, 1 bike · Reading which gear the activities used… · Esc goes back");
    CHECK_EQ(gear_note(GearState { .gear = GearList { } }), "0 pairs of shoes, 0 bikes · Esc goes back");
}

// Until the picks are read, the gear is listed without numbers; then
// with them, and again as the summaries change.
void test_numbers_come_with_the_picks()
{
    ScopedTestApp app;
    auto          screen = make_screen();
    screen->open(list(), "/data/gear.json");
    CHECK_EQ(screen->table->rows.size(), 3uz);
    CHECK(screen->table->rows.size() == 3 && screen->table->rows[2][4].empty());
    CHECK(screen->note->text.contains("Reading"));
    screen->set_picks({ });
    CHECK(screen->table->rows.size() == 3 && screen->table->rows[2][3] == "2" && screen->table->rows[2][4] == "4400 km");
    CHECK(!screen->note->text.contains("Reading"));
    CHECK(screen->table->is_dimmed(1));
    screen->set_summaries({ ride(May, 40) });
    CHECK(screen->table->rows.size() == 3 && screen->table->rows[2][3] == "1" && screen->table->rows[2][4] == "4340 km");
    // Opening it again reads the picks again.
    screen->open(list(), "/data/gear.json");
    CHECK(screen->table->rows.size() == 3 && screen->table->rows[2][4].empty());
}

void test_without_gear()
{
    ScopedTestApp app;
    auto          screen = make_screen();
    screen->open(std::nullopt, "/data/gear.json");
    screen->set_picks({ });
    CHECK(screen->table->rows.empty());
    CHECK(screen->note->text.starts_with("No gear yet"));
}

void test_step_row()
{
    ScopedTestApp app;
    auto          screen = make_screen();
    screen->open(list(), "/data/gear.json");
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
    test_note();
    test_numbers_come_with_the_picks();
    test_without_gear();
    test_step_row();
    return test_result();
}
