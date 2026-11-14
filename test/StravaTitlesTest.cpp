/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests matching the store's activities with Strava's, and saving their
 * titles, against canned responses: nothing here talks to Strava.
 */

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include <FakeStrava.h>
#include <IO.h>

#include <storage/Activity.h>
#include <storage/StravaTitles.h>

#include <TempDir.h>
#include <TempStore.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

constexpr i64 Ride = 1793102700; // 2026-10-27T12:05:00Z
constexpr i64 Run = 1793189100;  // 2026-10-28T12:05:00Z
constexpr i64 Swim = 1793257500; // 2026-10-29T07:05:00Z
constexpr i64 After = Now - 30 * 86400;

StravaActivity strava_at(u64 id, i64 start_time)
{
    return StravaActivity { .id = id, .start_time = start_time, .name = std::format("Activity {}", id) };
}

// Activities in the store, as activity_starts() finds them, by their start
// times.
std::vector<StoredStart> stored(std::vector<i64> const &starts)
{
    std::vector<StoredStart> ret;
    for (auto const start : starts) {
        ret.push_back(StoredStart { .start = start, .sidecar = std::format("{}.json", start) });
    }
    return ret;
}

// The start times of activities in the store.
std::vector<i64> starts_of(std::vector<StoredStart> const &stored)
{
    std::vector<i64> ret;
    for (auto const &s : stored) {
        ret.push_back(s.start);
    }
    return ret;
}

std::vector<u64> matched_ids(std::vector<StravaMatch> const &matches)
{
    std::vector<u64> ids;
    for (auto const &match : matches) {
        ids.push_back(match.strava.id);
    }
    return ids;
}

// Each activity in the store takes the nearest Strava activity, if it's
// within a minute, either way.
void test_match_nearest_within_a_minute()
{
    std::vector const strava { strava_at(1, 1000), strava_at(2, 1100), strava_at(3, 2060), strava_at(4, 2939) };
    auto const        starts = stored({ 1040, 1070, 2000, 3000, 5000 });
    auto const        matches = match_strava(strava, starts);
    CHECK((matched_ids(matches) == std::vector<u64> { 1, 2, 3 }));
    CHECK_EQ(matches[0].stored.start, 1040);
    CHECK_EQ(matches[2].stored.start, 2000);
    CHECK(match_strava({ }, starts).empty());
    CHECK(match_strava(strava, { }).empty());
}

// The same ride on two devices: both get its title.
void test_two_devices_match_the_same_activity()
{
    std::vector const strava { strava_at(7, 1000) };
    auto const        starts = stored({ 990, 1005 });
    CHECK((matched_ids(match_strava(strava, starts)) == std::vector<u64> { 7, 7 }));
}

// Halfway between two, the earlier one wins, and Strava's list needn't be
// in order.
void test_match_in_any_order()
{
    std::vector const strava { strava_at(2, 1040), strava_at(1, 960) };
    auto const        starts = stored({ 1000 });
    CHECK((matched_ids(match_strava(strava, starts)) == std::vector<u64> { 1 }));
}

struct StravaStore {
    TempStore  store { };
    TempDir    dir { "strava-titles" };
    FakeStrava fake { };
    Strava     strava { logged_in(fake, dir) };

    StravaStore()
    {
        // A failure here ends what it fails: asking again is strava_test's.
        strava.retries = 0;
        for (auto const start : { Ride, Run, Swim }) {
            store.add_activity(start);
        }
        MUST(write_file_by_name(store.storage.sidecar_of(Ride).string(), std::string_view { R"({"activityName": "Morning Ride"})" }));
    }

    void list(std::vector<std::string> const &activities)
    {
        fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of(activities) });
    }

    StravaUpdate update()
    {
        return MUST_EVAL(update_strava_titles(strava, store.storage, After, Now));
    }

    [[nodiscard]] std::optional<StravaTitle> saved(i64 start) const
    {
        return sidecar_titles(store.storage.sidecar_of(start)).strava;
    }

    [[nodiscard]] size_t description_requests() const
    {
        return static_cast<size_t>(std::ranges::count_if(fake.requests, [](HttpRequest const &request) {
            return request.url.contains("/api/v3/activities/");
        }));
    }
};

// The names go into the sidecars, with the rest of them kept, and an
// activity without a sidecar gets one. The descriptions are the
// background's: none is asked for. The Strava activity that isn't in the
// store, and the store's that isn't on Strava, are passed over.
void test_titles()
{
    StravaStore t;
    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara"), strava_activity(102, "2026-10-28T12:04:50Z", "Tempo run"),
        strava_activity(103, "2026-10-26T10:00:00Z", "Not in the store") });
    auto const update = t.update();
    CHECK_EQ(update.titles, 2u);
    CHECK(update.error.empty());
    CHECK((update.changed == std::vector<i64> { Ride, Run }));
    CHECK((t.saved(Ride) == StravaTitle { .id = 101, .name = "Ride to Niagara" }));
    CHECK((t.saved(Run) == StravaTitle { .id = 102, .name = "Tempo run" }));
    CHECK_EQ(t.description_requests(), 0u);
    CHECK(!t.saved(Swim).has_value());
    CHECK(!fs::exists(t.store.storage.sidecar_of(Swim)));
    CHECK_EQ(sidecar_title(t.store.storage.sidecar_of(Ride)).value_or(""), "Morning Ride");
}

// The next time, nothing has changed. A name changed on Strava is taken,
// and drops the description, which the background fetches again; an
// unchanged name keeps it.
void test_only_whats_new()
{
    StravaStore t;
    auto const  ride = strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara");
    auto const  run = strava_activity(102, "2026-10-28T12:04:50Z", "Tempo run");
    t.list({ ride, run });
    t.update();
    MUST(save_sidecar_strava(t.store.storage.sidecar_of(Ride), { .id = 101, .name = "Ride to Niagara", .description = "With Anna" }));
    MUST(save_sidecar_strava(t.store.storage.sidecar_of(Run), { .id = 102, .name = "Tempo run", .description = "Intervals" }));
    t.list({ ride, run });
    auto const again = t.update();
    CHECK_EQ(again.titles, 0u);
    CHECK(again.changed.empty());

    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Niagara loop"), run });
    auto const renamed = t.update();
    CHECK_EQ(renamed.titles, 1u);
    CHECK((renamed.changed == std::vector<i64> { Ride }));
    CHECK((t.saved(Ride) == StravaTitle { .id = 101, .name = "Niagara loop" }));
    CHECK_EQ(t.saved(Run)->description.value_or("<none>"), "Intervals");
    CHECK_EQ(t.description_requests(), 0u);
}

// A list that stops short on its second page keeps the first page's
// titles, and says why it stopped.
void test_list_stopped_short_keeps_its_titles()
{
    StravaStore              t;
    std::vector<std::string> full { strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara") };
    for (auto ix = 1u; ix < 200; ++ix) {
        full.push_back(strava_activity(5000 + ix, "2026-09-01T12:00:00Z", std::format("Not in the store {}", ix)));
    }
    t.list(full);
    t.fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 500, .body = "" });
    auto const update = t.update();
    CHECK_EQ(update.titles, 1u);
    CHECK(update.error.starts_with("Strava's list stopped short: Strava answered HTTP 500"));
    CHECK_EQ(t.saved(Ride)->name, "Ride to Niagara");
}

// The day's limit on the second page: the time it resets, in local time.
void test_list_stopped_short_by_the_day()
{
    StravaStore              t;
    std::vector<std::string> full { strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara") };
    for (auto ix = 1u; ix < 200; ++ix) {
        full.push_back(strava_activity(5000 + ix, "2026-09-01T12:00:00Z", std::format("Not in the store {}", ix)));
    }
    t.list(full);
    t.fake.answer("/api/v3/athlete/activities", HttpResponse {
                                                    .status = 429,
                                                    .body = R"({"message":"Rate Limit Exceeded"})",
                                                    .headers = { { "X-ReadRateLimit-Limit", "100,1000" }, { "X-ReadRateLimit-Usage", "40,1000" } },
                                                });
    auto const update = t.update();
    CHECK_EQ(update.titles, 1u);
    CHECK_EQ(update.error, "Strava's limit stopped its list short. Try again after 00:00");
}

// A sidecar that isn't a JSON object isn't overwritten: that activity is
// passed over.
void test_sidecar_that_isnt_an_object()
{
    StravaStore t;
    MUST(write_file_by_name(t.store.storage.sidecar_of(Ride).string(), std::string_view { "[1, 2]" }));
    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara") });
    auto const update = t.update();
    CHECK_EQ(update.titles, 0u);
    CHECK(update.changed.empty());
    CHECK_EQ(read_file_by_name(t.store.storage.sidecar_of(Ride).string()).value_or(""), "[1, 2]");
}

// Without a login, nothing is listed or written.
void test_not_logged_in()
{
    StravaStore t;
    auto        strava = logged_in(t.fake, t.dir, std::nullopt);
    auto const  update = update_strava_titles(strava, t.store.storage, After, Now);
    CHECK(!update.has_value() && update.error().kind == StravaError::Kind::NotLoggedIn);
    CHECK(t.fake.requests.empty());
}

// The store's start times come from its FIT files' names, in the months
// asked for, and a file not named by its start time isn't one.
void test_activity_starts()
{
    TempStore store;
    CHECK(!MUST_EVAL(store.storage.oldest_start()).has_value());
    for (auto const start : { Swim, Ride, Run, Ride - 40 * 86400 }) {
        store.add_activity(start);
    }
    auto const month = store.storage.sidecar_of(Ride).parent_path();
    MUST(write_file_by_name((month / "copied.fit").string(), std::string_view { "fit" }));
    MUST(write_file_by_name((month / "1793100000.json").string(), std::string_view { "{}" }));
    CHECK((starts_of(MUST_EVAL(store.storage.activity_starts(Ride, Swim))) == std::vector<i64> { Ride, Run, Swim }));
    CHECK((starts_of(MUST_EVAL(store.storage.activity_starts(Ride + 1, Swim - 1))) == std::vector<i64> { Run }));
    CHECK((MUST_EVAL(store.storage.activity_starts(0, Now)).size() == 4));
    CHECK_EQ(MUST_EVAL(store.storage.activity_starts(Ride, Ride)).front().sidecar.string(), store.storage.sidecar_of(Ride).string());
    CHECK_EQ(MUST_EVAL(store.storage.oldest_start()).value_or(0), Ride - 40 * 86400);
}

// Added by the Reviewer: Strava's limit on the list itself is an error,
// RateLimited with when it resets, and nothing is written.
void test_list_rate_limited()
{
    StravaStore t;
    t.fake.answer("/api/v3/athlete/activities", HttpResponse {
                                                    .status = 429,
                                                    .body = R"({"message":"Rate Limit Exceeded"})",
                                                    .headers = { { "X-ReadRateLimit-Limit", "100,1000" }, { "X-ReadRateLimit-Usage", "101,300" } },
                                                });
    auto const update = update_strava_titles(t.strava, t.store.storage, After, Now);
    CHECK(!update.has_value() && update.error().kind == StravaError::Kind::RateLimited);
    CHECK(!update.has_value() && update.error().resets_at == Now - Now % 900 + 900);
    CHECK(!t.saved(Ride).has_value());
    CHECK(!fs::exists(t.store.storage.sidecar_of(Run)));
}

// Added by the Reviewer: the same ride on two devices gets the title on
// both.
void test_two_devices_get_the_title()
{
    StravaStore t;
    t.store.add_activity(Ride + 30);
    t.list({ strava_activity(101, "2026-10-27T12:05:10Z", "Ride to Niagara") });
    auto const update = t.update();
    CHECK_EQ(update.titles, 2u);
    CHECK((update.changed == std::vector<i64> { Ride, Ride + 30 }));
    StravaTitle const titled { .id = 101, .name = "Ride to Niagara" };
    CHECK(t.saved(Ride) == titled);
    CHECK(t.saved(Ride + 30) == titled);
}

// Added by the Reviewer: the store files an activity in the month of its
// start in the time zone it was imported in. Looked for from another time
// zone, its sidecar is still the one next to its FIT file. 1790825400 is
// 2026-09-30 23:30 in Toronto, and 2026-10-01 05:30 in Amsterdam.
void test_activity_filed_in_another_time_zone()
{
    constexpr i64     MonthEnd = 1790825400;
    auto const       *tz = std::getenv("TZ");
    std::string const old_tz = tz ? tz : "";
    setenv("TZ", "America/Toronto", 1);
    tzset();
    {
        StravaStore t;
        t.store.add_activity(MonthEnd);
        auto const filed = t.store.storage.activity_dir / "2026/09" / std::format("{}.json", MonthEnd);
        setenv("TZ", "Europe/Amsterdam", 1);
        tzset();
        t.list({ strava_activity(201, "2026-10-01T03:30:00Z", "Last ride of September") });
        auto const update = MUST_EVAL(update_strava_titles(t.strava, t.store.storage, MonthEnd - 86400, MonthEnd + 86400));
        CHECK_EQ(update.titles, 1u);
        CHECK(sidecar_titles(filed).strava.has_value());
        // Not a sidecar of its own, in a month without its FIT file.
        CHECK(!fs::exists(t.store.storage.activity_dir / "2026/10" / std::format("{}.json", MonthEnd)));
    }
    if (tz) {
        setenv("TZ", old_tz.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}

// Added by the Reviewer: a sync writes Strava's name into a sidecar with
// the user's own title and description, and leaves those.
void test_sync_keeps_the_users_notes()
{
    StravaStore t;
    MUST(write_file_by_name(t.store.storage.sidecar_of(Ride).string(), std::string_view { R"({"activityName": "Morning Ride", "title": "Niagara", "notes": "Mine"})" }));
    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara") });
    auto const update = t.update();
    CHECK_EQ(update.titles, 1u);
    auto const names = sidecar_titles(t.store.storage.sidecar_of(Ride));
    CHECK_EQ(names.own.value_or(""), "Niagara");
    CHECK_EQ(names.own_notes.value_or(""), "Mine");
    CHECK((names.strava == StravaTitle { .id = 101, .name = "Ride to Niagara" }));
}

// The export import found Strava's file of 101 isn't the ride's: the
// titles run doesn't give the ride 101 again, but does give it another.
// The rejection keeps the rest of the sidecar, and only removes the
// title of the activity rejected.
void test_rejected_isnt_given_again()
{
    StravaStore t;
    auto const  sidecar = t.store.storage.sidecar_of(Ride);
    MUST(write_file_by_name(sidecar.string(), std::string_view { R"({"activityName": "Morning Ride", "title": "Niagara", "notes": "Mine", "strava": {"id": 101, "name": "Ride to Niagara"}})" }));
    MUST(reject_sidecar_strava(sidecar, 999));
    CHECK((t.saved(Ride) == StravaTitle { .id = 101, .name = "Ride to Niagara" }));
    MUST(reject_sidecar_strava(sidecar, 101));
    MUST(reject_sidecar_strava(sidecar, 101));
    auto const names = sidecar_titles(sidecar);
    CHECK(!names.strava.has_value());
    CHECK((names.strava_rejected == std::vector<u64> { 999, 101 }));
    CHECK_EQ(names.own.value_or(""), "Niagara");
    CHECK_EQ(names.own_notes.value_or(""), "Mine");
    CHECK_EQ(names.service.value_or(""), "Morning Ride");

    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara") });
    CHECK_EQ(t.update().titles, 0u);
    CHECK(!t.saved(Ride).has_value());
    t.list({ strava_activity(104, "2026-10-27T12:05:30Z", "Ride to Hamilton") });
    CHECK_EQ(t.update().titles, 1u);
    CHECK((t.saved(Ride) == StravaTitle { .id = 104, .name = "Ride to Hamilton" }));
}

}

int main()
{
    // The messages say when Strava's limit resets in local time.
    setenv("TZ", "UTC", 1);
    tzset();
    test_match_nearest_within_a_minute();
    test_two_devices_match_the_same_activity();
    test_match_in_any_order();
    test_titles();
    test_only_whats_new();
    test_list_stopped_short_keeps_its_titles();
    test_list_stopped_short_by_the_day();
    test_sidecar_that_isnt_an_object();
    test_not_logged_in();
    test_activity_starts();
    test_list_rate_limited();
    test_two_devices_get_the_title();
    test_activity_filed_in_another_time_zone();
    test_sync_keeps_the_users_notes();
    test_rejected_isnt_given_again();
    return ST::Test::test_result();
}
