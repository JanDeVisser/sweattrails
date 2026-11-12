/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests finding the descriptions the store is waiting for, and fetching
 * them as Strava's limits allow, against canned responses: nothing here
 * talks to Strava.
 */

#include <cstdlib>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <FakeStrava.h>

#include <storage/Activity.h>
#include <storage/StravaBacklog.h>

#include <TempDir.h>
#include <TempStore.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

constexpr i64 Ride = 1793102700; // 2026-10-27T12:05:00Z
constexpr i64 Run = 1793189100;  // 2026-10-28T12:05:00Z
constexpr i64 Swim = 1793257500; // 2026-10-29T07:05:00Z
constexpr i64 Walk = 1790836215; // 2026-10-01T06:30:15Z, another month

constexpr i64 NextQuarterHour = Now + 10 * 60; // 12:15
constexpr i64 NextDay = 1793318400;            // 2026-10-30T00:00:00Z

void save_strava(TempStore const &store, i64 start, StravaTitle const &title)
{
    MUST(save_sidecar_strava(store.storage.sidecar_of(start), title));
}

std::optional<StravaTitle> strava_of(TempStore const &store, i64 start)
{
    return sidecar_titles(store.storage.sidecar_of(start)).strava;
}

std::vector<u64> ids_of(std::vector<Undescribed> const &waiting)
{
    std::vector<u64> ret;
    for (auto const &u : waiting) {
        ret.push_back(u.id);
    }
    return ret;
}

// An answer for activity `id`, with Strava's usage of its limits.
HttpResponse activity(u64 id, std::string_view description, std::string_view usage = "10,100")
{
    return HttpResponse {
        .status = 200,
        .body = std::format(R"({{"id":{},"name":"Ride","description":"{}"}})", id, description),
        .headers = { { "X-ReadRateLimit-Limit", "100,1000" }, { "X-ReadRateLimit-Usage", std::string { usage } } },
    };
}

std::string path_of(u64 id)
{
    return std::format("/api/v3/activities/{}", id);
}

// A store with four activities: Swim and Ride wait for their
// descriptions, Run has one, and Walk has no Strava title.
struct Backlog {
    TempStore  store { };
    FakeStrava fake { };
    TempDir    dir { "strava-backlog" };
    Strava     strava = logged_in(fake, dir);

    Backlog()
    {
        for (auto const start : { Ride, Run, Swim, Walk }) {
            store.add_activity(start);
        }
        save_strava(store, Ride, { .id = 1, .name = "Ride" });
        save_strava(store, Run, { .id = 2, .name = "Run", .description = "Easy" });
        save_strava(store, Swim, { .id = 3, .name = "Swim" });
    }

    std::vector<Undescribed> waiting() { return MUST_EVAL(strava_backlog(store.storage)); }
};

void test_backlog_newest_first()
{
    Backlog    t;
    auto const waiting = t.waiting();
    CHECK_EQ(ids_of(waiting), (std::vector<u64> { 3, 1 }));
    CHECK_EQ(waiting.front().stored.size(), 1uz);
    CHECK_EQ(waiting.front().stored.front().start, Swim);
    // An empty description is one: Strava has none.
    save_strava(t.store, Swim, { .id = 3, .name = "Swim", .description = "" });
    CHECK_EQ(ids_of(t.waiting()), (std::vector<u64> { 1 }));
}

// The same ride on two devices: one Strava ID, one request, both saved.
void test_backlog_one_request_per_id()
{
    Backlog t;
    save_strava(t.store, Walk, { .id = 1, .name = "Ride" });
    auto waiting = t.waiting();
    CHECK_EQ(ids_of(waiting), (std::vector<u64> { 3, 1 }));
    CHECK_EQ(waiting[1].stored.size(), 2uz);
    CHECK_EQ(waiting[1].stored.front().start, Ride);

    t.fake.answer(path_of(3), activity(3, "Cold"));
    t.fake.answer(path_of(1), activity(1, "Windy"));
    auto const chunk = fetch_backlog(t.strava, waiting, 10);
    CHECK_EQ(chunk.fetched, 2uz);
    CHECK(waiting.empty());
    CHECK_EQ(t.fake.requests.size(), 2uz);
    CHECK_EQ(chunk.changed, (std::vector<i64> { Walk, Ride, Swim }));
    CHECK_EQ(strava_of(t.store, Walk).value().description.value_or("none"), "Windy");
    CHECK_EQ(strava_of(t.store, Ride).value().description.value_or("none"), "Windy");
    CHECK_EQ(strava_of(t.store, Swim).value().description.value_or("none"), "Cold");
    CHECK(!chunk.resume_at && !chunk.stopped);
}

void test_resume_at()
{
    auto const usage = [](i64 quarter, i64 day) {
        return StravaUsage { .quarter_used = quarter, .quarter_limit = 100, .day_used = day, .day_limit = 1000 };
    };
    // Before the first answer, nothing is known: ask.
    CHECK(!backlog_resume_at(std::nullopt, Now).has_value());
    CHECK(!backlog_resume_at(usage(79, 500), Now).has_value());
    // 20 short of either limit, it's the user's.
    CHECK_EQ(backlog_resume_at(usage(80, 500), Now).value_or(0), NextQuarterHour);
    CHECK_EQ(backlog_resume_at(usage(101, 500), Now).value_or(0), NextQuarterHour);
    CHECK_EQ(backlog_resume_at(usage(10, 980), Now).value_or(0), NextDay);
    // Both: the day's comes first.
    CHECK_EQ(backlog_resume_at(usage(90, 990), Now).value_or(0), NextDay);
    // A minute before midnight UTC, the next day is a minute away.
    CHECK_EQ(backlog_resume_at(usage(10, 980), NextDay - 60).value_or(0), NextDay);
    CHECK_EQ(backlog_resume_at(usage(80, 500), NextDay - 60).value_or(0), NextDay);
}

// The chunk asks for no more than it's told to.
void test_fetch_count()
{
    Backlog t;
    auto    waiting = t.waiting();
    t.fake.answer(path_of(3), activity(3, "Cold"));
    auto const chunk = fetch_backlog(t.strava, waiting, 1);
    CHECK_EQ(chunk.fetched, 1uz);
    CHECK_EQ(ids_of(waiting), (std::vector<u64> { 1 }));
    CHECK_EQ(chunk.changed, (std::vector<i64> { Swim }));
    CHECK(!strava_of(t.store, Ride).value().description.has_value());
}

// Strava's answer says the 15-minute limit is 20 short: the next request
// waits for the quarter hour.
void test_fetch_stops_short_of_the_limit()
{
    Backlog t;
    auto    waiting = t.waiting();
    t.fake.answer(path_of(3), activity(3, "Cold", "80,300"));
    auto const chunk = fetch_backlog(t.strava, waiting, 10);
    CHECK_EQ(chunk.fetched, 1uz);
    CHECK_EQ(chunk.resume_at.value_or(0), NextQuarterHour);
    CHECK_EQ(t.fake.requests.size(), 1uz);
    CHECK_EQ(ids_of(waiting), (std::vector<u64> { 1 }));

    // Usage kept from before, such as from the list, counts too.
    auto const again = fetch_backlog(t.strava, waiting, 10);
    CHECK_EQ(again.fetched, 0uz);
    CHECK_EQ(again.resume_at.value_or(0), NextQuarterHour);
    CHECK_EQ(t.fake.requests.size(), 1uz);
}

void test_fetch_stops_at_the_daily_limit()
{
    Backlog t;
    auto    waiting = t.waiting();
    t.fake.answer(path_of(3), activity(3, "Cold", "5,985"));
    auto const chunk = fetch_backlog(t.strava, waiting, 10);
    CHECK_EQ(chunk.fetched, 1uz);
    CHECK_EQ(chunk.resume_at.value_or(0), NextDay);
}

// Someone else used the limit up: Strava's 429 says when to go on.
void test_fetch_rate_limited()
{
    Backlog t;
    auto    waiting = t.waiting();
    t.fake.answer(path_of(3), HttpResponse {
                                  .status = 429,
                                  .body = R"({"message":"Rate Limit Exceeded"})",
                                  .headers = { { "X-ReadRateLimit-Limit", "100,1000" }, { "X-ReadRateLimit-Usage", "40,1001" } },
                              });
    auto const chunk = fetch_backlog(t.strava, waiting, 10);
    CHECK_EQ(chunk.fetched, 0uz);
    CHECK_EQ(chunk.resume_at.value_or(0), NextDay);
    CHECK_EQ(ids_of(waiting), (std::vector<u64> { 3, 1 }));
    CHECK(!strava_of(t.store, Swim).value().description.has_value());
}

// A failure waits for the next quarter hour, and is asked again then.
void test_fetch_failure()
{
    Backlog t;
    auto    waiting = t.waiting();
    t.fake.answer(path_of(3), HttpResponse { .status = 400, .body = "{}" });
    auto const chunk = fetch_backlog(t.strava, waiting, 10);
    CHECK_EQ(chunk.resume_at.value_or(0), NextQuarterHour);
    CHECK(!chunk.stopped);
    CHECK_EQ(ids_of(waiting), (std::vector<u64> { 3, 1 }));
}

// Without a login there's nothing to wait for: the next titles run, which
// offers the login, starts it again.
void test_fetch_not_logged_in()
{
    Backlog t;
    auto    waiting = t.waiting();
    t.strava.tokens.reset();
    auto const chunk = fetch_backlog(t.strava, waiting, 10);
    CHECK(chunk.stopped);
    CHECK(!chunk.resume_at.has_value());
    CHECK_EQ(waiting.size(), 2uz);
}

// An activity Strava no longer has would be asked for forever: it's
// given an empty description, and the others carry on.
void test_fetch_not_found()
{
    Backlog t;
    auto    waiting = t.waiting();
    t.fake.answer(path_of(1), activity(1, "Windy"));
    auto const chunk = fetch_backlog(t.strava, waiting, 10);
    CHECK(waiting.empty());
    CHECK_EQ(chunk.fetched, 1uz);
    CHECK_EQ(strava_of(t.store, Swim).value().description.value_or("none"), "");
    CHECK_EQ(strava_of(t.store, Ride).value().description.value_or("none"), "Windy");
    CHECK(t.waiting().empty());
}

// A titles run between reading the backlog and the answer gave the
// activity a new name and maybe a description: those stay.
void test_fetch_keeps_what_changed_since()
{
    Backlog t;
    auto    waiting = t.waiting();
    save_strava(t.store, Swim, { .id = 3, .name = "Renamed" });
    save_strava(t.store, Ride, { .id = 1, .name = "Ride", .description = "From the export" });
    t.fake.answer(path_of(3), activity(3, "Cold"));
    t.fake.answer(path_of(1), activity(1, "Windy"));
    auto const chunk = fetch_backlog(t.strava, waiting, 10);
    CHECK_EQ(strava_of(t.store, Swim).value().name, "Renamed");
    CHECK_EQ(strava_of(t.store, Swim).value().description.value_or("none"), "Cold");
    CHECK_EQ(strava_of(t.store, Ride).value().description.value_or("none"), "From the export");
    CHECK_EQ(chunk.changed, (std::vector<i64> { Swim }));
}

}

int main()
{
    // The store's months are in local time.
    setenv("TZ", "UTC", 1);
    tzset();
    test_backlog_newest_first();
    test_backlog_one_request_per_id();
    test_resume_at();
    test_fetch_count();
    test_fetch_stops_short_of_the_limit();
    test_fetch_stops_at_the_daily_limit();
    test_fetch_rate_limited();
    test_fetch_failure();
    test_fetch_not_logged_in();
    test_fetch_not_found();
    test_fetch_keeps_what_changed_since();
    return ST::Test::test_result();
}
