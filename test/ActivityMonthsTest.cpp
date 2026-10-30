/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The months with activities, and going from one activity to the one
 * before or after it, as Ctrl+M's list and Left and Right do. See
 * specs/21-activity-selection.md.
 */

#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <IO.h>

#include <storage/Storage.h>
#include <storage/Types.h>

#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using namespace ST::Test;

namespace fs = std::filesystem;

namespace {

Month month(size_t year, int month)
{
    return Month { year, static_cast<DateTime::Month>(month - 1) };
}

std::string show(std::optional<Month> const &m)
{
    return m ? m->dir_name() : std::string { "none" };
}

// A store with rides in September, November (one that doesn't load) and
// December 2021, an empty October, and one ride in February 2023.
struct Store {
    TempStore store { };

    Store()
    {
        ride("2021/09", "a", 0);
        ride("2021/09", "b", 3600);
        fs::create_directories(store.storage.activity_dir / "2021/10");
        put("2021/11", "broken", "not a FIT file");
        ride("2021/12", "c", 90 * 86400);
        ride("2023/02", "d", 500 * 86400);
    }

    void ride(std::string_view dir, std::string_view name, u32 start) const
    {
        FITBuilder fit;
        add_session(fit, 3600, 100, NoSubSport, start);
        put(dir, name, fit.file());
    }

    void put(std::string_view dir, std::string_view name, std::string_view contents) const
    {
        auto const path = store.storage.activity_dir / dir;
        fs::create_directories(path);
        MUST(write_file_by_name((path / std::format("{}.fit", name)).string(), contents));
    }

    std::string next_to(std::string_view dir, std::string_view name, int step)
    {
        auto const year = static_cast<size_t>(std::stoi(std::string { dir.substr(0, 4) }));
        auto const m = std::stoi(std::string { dir.substr(5, 2) });
        auto const id = ActivityID { month(year, m), name, FileTypes::one(ActivityFile::Fit) };
        auto const next = store.storage.activity_next_to(id, step);
        if (!next) {
            return next.error().to_string();
        }
        return *next ? std::format("{} {}", (*next)->month.dir_name(), (*next)->name) : std::string { "none" };
    }
};

void test_plus()
{
    CHECK_EQ(month(2021, 12).plus(1).dir_name(), "2022/01");
    CHECK_EQ(month(2021, 1).plus(-1).dir_name(), "2020/12");
    CHECK_EQ(month(2021, 5).plus(-24).dir_name(), "2019/05");
    CHECK_EQ(month(2021, 5).plus(0).dir_name(), "2021/05");
    CHECK_EQ(month(2021, 1).plus(-13).dir_name(), "2019/12");
    CHECK_EQ(month(2021, 12).plus(13).dir_name(), "2023/01");
}

// Only months with an activity file, oldest first: not an empty one, but
// one whose file doesn't load, as that only shows when it's listed.
void test_months_with_activities()
{
    Store      s;
    auto const months = s.store.storage.months_with_activities();
    CHECK(months.has_value());
    std::vector<std::string> names;
    for (auto const &m : months.value_or(std::vector<Month> { })) {
        names.push_back(m.dir_name());
    }
    CHECK_EQ(names, (std::vector<std::string> { "2021/09", "2021/11", "2021/12", "2023/02" }));
    // rescan() agrees.
    CHECK(s.store.storage.rescan().has_value());
    CHECK_EQ(s.store.storage.years.size(), 2uz);
}

void test_month_after_and_before()
{
    std::vector const months { month(2021, 9), month(2021, 12), month(2023, 2) };
    CHECK_EQ(show(month_after(months, month(2021, 9))), "2021/12");
    CHECK_EQ(show(month_after(months, month(2021, 10))), "2021/12");
    CHECK_EQ(show(month_after(months, month(2023, 2))), "none");
    CHECK_EQ(show(month_before(months, month(2023, 2))), "2021/12");
    CHECK_EQ(show(month_before(months, month(2022, 6))), "2021/12");
    CHECK_EQ(show(month_before(months, month(2021, 9))), "none");
    CHECK_EQ(show(month_after(std::vector<Month> { }, month(2021, 9))), "none");
}

// In the month, then over an empty month and one that lists nothing, and
// nothing at the ends.
void test_activity_next_to()
{
    Store s;
    CHECK_EQ(s.next_to("2021/09", "a", 1), "2021/09 b");
    CHECK_EQ(s.next_to("2021/09", "b", -1), "2021/09 a");
    CHECK_EQ(s.next_to("2021/09", "b", 1), "2021/12 c");
    CHECK_EQ(s.next_to("2021/12", "c", -1), "2021/09 b");
    CHECK_EQ(s.next_to("2021/12", "c", 1), "2023/02 d");
    CHECK_EQ(s.next_to("2023/02", "d", -1), "2021/12 c");
    CHECK_EQ(s.next_to("2021/09", "a", -1), "none");
    CHECK_EQ(s.next_to("2023/02", "d", 1), "none");
}

// A month with only a Strava file counts as one with activities, but lists
// none, so going from one activity to the next passes over it too.
void test_activity_next_to_over_strava_month()
{
    Store s;
    s.put("2022/06", "1654041600", "{}");
    fs::rename(s.store.storage.activity_dir / "2022/06/1654041600.fit", s.store.storage.activity_dir / "2022/06/1654041600.strava");
    CHECK_EQ(s.next_to("2021/12", "c", 1), "2023/02 d");
    CHECK_EQ(s.next_to("2023/02", "d", -1), "2021/12 c");
}

}

int main()
{
    SetTraceLogLevel(LOG_NONE);
    test_plus();
    test_months_with_activities();
    test_month_after_and_before();
    test_activity_next_to();
    test_activity_next_to_over_strava_month();
    return Test::test_result();
}
