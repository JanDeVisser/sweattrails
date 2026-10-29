/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests that ActivitySelector, the month's activity list (Ctrl-M), submits
 * the activity that is selected, also while searching, what its rows show,
 * and where Left and Right go. The selector reads the month from storage
 * when it's made; the storage here mostly points at a directory that
 * doesn't exist, unique to this run, so the month is empty, and the test
 * adds the entries itself.
 */

#include <filesystem>
#include <format>
#include <optional>
#include <string>

#include <unistd.h>

#include <IO.h>

#include <Modal.h>

#include <storage/Storage.h>
#include <widget/ActivitySelector.h>

#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using namespace ST::Test;

namespace fs = std::filesystem;

namespace {

struct Fixture {
    Storage                           storage { };
    Month                             month { 2026, DateTime::Month::April };
    std::optional<ActivityID>         submitted { };
    std::shared_ptr<ActivitySelector> selector { };

    explicit Fixture(std::vector<std::string> const &names)
    {
        storage.activity_dir = std::filesystem::temp_directory_path() / std::format("sweattrails-test-{}-no-such-dir", getpid());
        CHECK(!std::filesystem::exists(storage.activity_dir));
        selector = std::make_shared<ActivitySelector>(
            [this](std::shared_ptr<ActivitySelector> const &, ActivityID const &id) {
                submitted = id;
            },
            month, storage);
        for (auto const &name : names) {
            selector->entries.emplace_back(name, ActivityID { month, name, FileTypes::one(ActivityFile::Fit) });
        }
        selector->scroll.lines = 20;
        selector->status = ModalStatus::Active;
    }

    void type(std::string_view const &text)
    {
        for (auto ch : text) {
            selector->character(ch);
        }
    }

    void key(int key)
    {
        selector->status = ModalStatus::Active;
        selector->process_key(KModNone, key);
    }

    [[nodiscard]] std::string submitted_name() const
    {
        return submitted ? submitted->name : std::string { "<none>" };
    }
};

void test_submit_without_search()
{
    Fixture f { { "a run", "b ride", "c run", "d swim" } };
    CHECK(f.selector->entries.size() == 4);
    f.key(KEY_DOWN);
    f.key(KEY_ENTER);
    CHECK_EQ(f.submitted_name(), "b ride");
}

void test_submit_search_match()
{
    Fixture f { { "a run", "b ride", "c run", "d swim" } };
    f.type("swim");
    f.key(KEY_ENTER);
    CHECK_EQ(f.submitted_name(), "d swim");
}

void test_submit_second_search_match()
{
    Fixture f { { "a run", "b ride", "c run", "d swim" } };
    f.type("run");
    f.key(KEY_DOWN);
    f.key(KEY_ENTER);
    CHECK_EQ(f.submitted_name(), "c run");
}

void test_no_match_submits_nothing()
{
    Fixture f { { "a run", "b ride" } };
    f.type("swim");
    f.key(KEY_ENTER);
    CHECK(!f.submitted);
}

Activity activity(Storage &storage, std::string_view title, float32 meters, int hours, int minutes, int seconds)
{
    Activity a { storage, ActivityID { Month { 2026, DateTime::Month::April }, title, FileTypes::one(ActivityFile::Fit) } };
    a.title = title;
    a.sport = sport::Cycling;
    a.segment.start_time.day_of_week = DateTime::WeekDay::Tuesday;
    a.segment.start_time.day_of_month = 14;
    a.segment.start_time.hour = 9;
    a.segment.start_time.minute = 5;
    a.segment.distance = meters;
    a.segment.moving = Duration { hours * 3600 + minutes * 60 + seconds };
    return a;
}

// The sport's icon, the day and time, the title, and the distance and the
// moving time as the data panel has them.
void test_activity_entry()
{
    Storage    storage { };
    auto const ride = activity_entry(activity(storage, "Morning ride", 42300, 1, 32, 10));
    CHECK_EQ(ride.text, "Morning ride");
    CHECK_EQ(ride.prefix, "Tue 14  09:05");
    CHECK_EQ(ride.columns, (std::vector<std::string> { "42.3 km", "1:32:10" }));
    CHECK(ride.icon.ends_with(".png"));
    CHECK(ride.payload.name == "Morning ride");
    // Indoors, without a distance: none.
    auto const zwift = activity_entry(activity(storage, "Zwift", 0, 0, 41, 7));
    CHECK_EQ(zwift.columns, (std::vector<std::string> { "", "41:07" }));
    // A day under 10 is padded, so that the times line up.
    auto early = activity(storage, "Early", 1000, 0, 5, 0);
    early.segment.start_time.day_of_month = 4;
    CHECK_EQ(activity_entry(early).prefix, "Tue  4  09:05");
}

// Typing matches the title, not the day, time, distance or duration.
void test_search_matches_title_only()
{
    Fixture f { { } };
    f.selector->entries.push_back(activity_entry(activity(f.storage, "Morning ride", 42300, 1, 32, 10)));
    f.selector->entries.push_back(activity_entry(activity(f.storage, "Intervals 4x8", 8100, 0, 41, 7)));
    f.type("09");
    f.key(KEY_ENTER);
    CHECK_EQ(f.submitted_name(), "<none>");
    f.selector->search = { };
    f.type("4x8");
    f.key(KEY_ENTER);
    CHECK_EQ(f.submitted_name(), "Intervals 4x8");
}

// Left and Right go to the months with activities next to this one, and
// Shift a year; at the ends, nowhere. A real store, whose months list.
void test_destinations()
{
    TempStore  t;
    auto const write = [&t](std::string_view dir) {
        FITBuilder fit;
        add_session(fit, 600, 10);
        fs::create_directories(t.storage.activity_dir / dir);
        MUST(write_file_by_name((t.storage.activity_dir / dir / "a.fit").string(), fit.file()));
    };
    write("2021/09");
    write("2021/12");
    write("2023/02");
    auto const at = [&](size_t year, int m) {
        return std::make_shared<ActivitySelector>(
            [](std::shared_ptr<ActivitySelector> const &, ActivityID const &) { }, Month { year, static_cast<DateTime::Month>(m - 1) }, t.storage);
    };
    auto const go = [](std::shared_ptr<ActivitySelector> const &s, KeyboardModifier modifier, int key) {
        auto const to = s->destination(modifier, key);
        return to ? to->dir_name() : std::string { "none" };
    };
    auto const september = at(2021, 9);
    CHECK_EQ(september->entries.size(), 1uz);
    CHECK_EQ(go(september, KModNone, KEY_RIGHT), "2021/12");
    CHECK_EQ(go(september, KModNone, KEY_LEFT), "none");
    CHECK_EQ(go(september, KModShift, KEY_RIGHT), "2023/02");
    CHECK_EQ(go(september, KModControl, KEY_RIGHT), "none");
    CHECK_EQ(go(september, KModNone, KEY_DOWN), "none");
    auto const february = at(2023, 2);
    CHECK_EQ(go(february, KModNone, KEY_LEFT), "2021/12");
    CHECK_EQ(go(february, KModShift, KEY_LEFT), "2021/12");
    CHECK_EQ(go(february, KModNone, KEY_RIGHT), "none");
    // At the end, the key is still the list's, and changes nothing.
    february->status = ModalStatus::Active;
    CHECK(february->process_key(KModNone, KEY_RIGHT));
    CHECK_EQ(february->month.dir_name(), "2023/02");
}

// A store whose rides are written by name and start, `start` seconds after
// TestFit's Start, as the inbox would have filed them.
struct RideStore {
    TempStore t { };

    void ride(std::string_view dir, std::string_view name, u32 start) const
    {
        FITBuilder fit;
        add_session(fit, 600, 10, NoSubSport, start);
        put(dir, std::format("{}.fit", name), fit.file());
    }

    void put(std::string_view dir, std::string_view file, std::string_view contents) const
    {
        fs::create_directories(t.storage.activity_dir / dir);
        MUST(write_file_by_name((t.storage.activity_dir / dir / file).string(), contents));
    }

    std::shared_ptr<ActivitySelector> at(size_t year, int m)
    {
        return std::make_shared<ActivitySelector>(
            [](std::shared_ptr<ActivitySelector> const &, ActivityID const &) { }, Month { year, static_cast<DateTime::Month>(m - 1) }, t.storage);
    }
};

// The rows are oldest first, not by name, and opening at the activity on
// display selects it, as cmd_list_month() does with the ID that
// show_activity() kept.
void test_opens_at_activity()
{
    RideStore s;
    s.ride("2021/09", "z", 0);
    s.ride("2021/09", "m", 3600);
    s.ride("2021/09", "a", 7200);
    auto const selector = s.at(2021, 9);
    CHECK_EQ(selector->entries.size(), 3uz);
    std::vector<std::string> names;
    for (auto const &e : selector->entries) {
        names.push_back(e.payload.name);
    }
    CHECK_EQ(names, (std::vector<std::string> { "z", "m", "a" }));
    selector->scroll.lines = 2;
    auto const shown = MUST_EVAL(ActivityID(Month { 2021, DateTime::Month::September }, "a", FileTypes::one(ActivityFile::Fit)).load(s.t.storage, LoadingDepth::Deep));
    CHECK(shown.has_value());
    if (shown) {
        selector->select(shown->id);
    }
    CHECK_EQ(selector->selection, 2);
    CHECK(selector->scroll.top_line <= 2 && 2 < selector->scroll.top_line + selector->scroll.lines);
}

// Left and Right skip months that list no activities: one with only a
// Strava file, which Month::has_activities() counts but Month::list()
// doesn't, and one whose only FIT file doesn't load. Otherwise the list
// stops on an empty month, which the spec says it doesn't.
void test_destinations_skip_months_that_list_nothing()
{
    RideStore s;
    s.ride("2021/09", "a", 0);
    s.put("2021/10", "1633046400.strava", "{}");
    s.put("2021/11", "broken.fit", "not a FIT file");
    s.ride("2021/12", "c", 90 * 86400);
    auto const september = s.at(2021, 9);
    CHECK_EQ(september->destination(KModNone, KEY_RIGHT).value_or(Month { }).dir_name(), "2021/12");
    auto const december = s.at(2021, 12);
    CHECK_EQ(december->destination(KModNone, KEY_LEFT).value_or(Month { }).dir_name(), "2021/09");
}

}

int main()
{
    Test::ScopedTestApp app;
    test_submit_without_search();
    test_submit_search_match();
    test_submit_second_search_match();
    test_no_match_submits_nothing();
    test_activity_entry();
    test_search_matches_title_only();
    test_destinations();
    test_opens_at_activity();
    test_destinations_skip_months_that_list_nothing();
    return Test::test_result();
}
