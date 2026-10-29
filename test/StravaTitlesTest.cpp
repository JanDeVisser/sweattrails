/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests matching the store's activities with Strava's, and saving their
 * titles and descriptions, against canned responses: nothing here talks
 * to Strava.
 */

#include <algorithm>
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
    std::vector const      strava { strava_at(1, 1000), strava_at(2, 1100), strava_at(3, 2060), strava_at(4, 2939) };
    std::vector<i64> const starts { 1040, 1070, 2000, 3000, 5000 };
    auto const             matches = match_strava(strava, starts);
    CHECK((matched_ids(matches) == std::vector<u64> { 1, 2, 3 }));
    CHECK_EQ(matches[0].start, 1040);
    CHECK_EQ(matches[2].start, 2000);
    CHECK(match_strava({ }, starts).empty());
    CHECK(match_strava(strava, { }).empty());
}

// The same ride on two devices: both get its title.
void test_two_devices_match_the_same_activity()
{
    std::vector const      strava { strava_at(7, 1000) };
    std::vector<i64> const starts { 990, 1005 };
    CHECK((matched_ids(match_strava(strava, starts)) == std::vector<u64> { 7, 7 }));
}

// Halfway between two, the earlier one wins, and Strava's list needn't be
// in order.
void test_match_in_any_order()
{
    std::vector const      strava { strava_at(2, 1040), strava_at(1, 960) };
    std::vector<i64> const starts { 1000 };
    CHECK((matched_ids(match_strava(strava, starts)) == std::vector<u64> { 1 }));
}

struct StravaStore {
    TempStore  store { };
    TempDir    dir { "strava-titles" };
    FakeStrava fake { };
    Strava     strava { logged_in(fake, dir) };

    StravaStore()
    {
        for (auto const start : { Ride, Run, Swim }) {
            store.add_activity(start);
        }
        MUST(write_file_by_name(store.storage.sidecar_of(Ride).string(), std::string_view { R"({"activityName": "Morning Ride"})" }));
    }

    void list(std::vector<std::string> const &activities)
    {
        fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of(activities) });
    }

    void describe(u64 id, std::string_view description)
    {
        fake.answer(std::format("/api/v3/activities/{}", id), HttpResponse { .status = 200, .body = std::format(R"({{"id":{},"description":{}}})", id, description) });
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
// activity without a sidecar gets one. Then come the descriptions, a null
// one as "". The Strava activity that isn't in the store, and the store's
// that isn't on Strava, are passed over.
void test_titles_and_descriptions()
{
    StravaStore t;
    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara"), strava_activity(102, "2026-10-28T12:04:50Z", "Tempo run"),
        strava_activity(103, "2026-10-26T10:00:00Z", "Not in the store") });
    t.describe(101, R"("With Anna")");
    t.describe(102, "null");
    std::vector<std::pair<size_t, size_t>> progress;
    auto const                             update = MUST_EVAL(update_strava_titles(t.strava, t.store.storage, After, Now, [&progress](size_t index, size_t count) {
        progress.emplace_back(index, count);
    }));
    CHECK_EQ(update.titles, 2u);
    CHECK_EQ(update.descriptions, 2u);
    CHECK_EQ(update.waiting, 0u);
    CHECK(!update.resets_at.has_value());
    CHECK((update.changed == std::vector<i64> { Ride, Run }));
    CHECK((progress == std::vector<std::pair<size_t, size_t>> { { 0, 2 }, { 1, 2 } }));
    CHECK((t.saved(Ride) == StravaTitle { .id = 101, .name = "Ride to Niagara", .description = "With Anna" }));
    CHECK((t.saved(Run) == StravaTitle { .id = 102, .name = "Tempo run", .description = "" }));
    CHECK(!t.saved(Swim).has_value());
    CHECK(!fs::exists(t.store.storage.sidecar_of(Swim)));
    CHECK_EQ(sidecar_title(t.store.storage.sidecar_of(Ride)).value_or(""), "Morning Ride");
}

// The next time, nothing has changed, and no description is fetched again.
// A name changed on Strava is taken, and its description fetched again.
void test_only_whats_new()
{
    StravaStore t;
    auto const  ride = strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara");
    auto const  run = strava_activity(102, "2026-10-28T12:04:50Z", "Tempo run");
    t.list({ ride, run });
    t.describe(101, R"("With Anna")");
    t.describe(102, R"("")");
    t.update();
    t.list({ ride, run });
    auto const again = t.update();
    CHECK_EQ(again.titles, 0u);
    CHECK_EQ(again.descriptions, 0u);
    CHECK(again.changed.empty());
    CHECK_EQ(t.description_requests(), 2u);

    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Niagara loop"), run });
    t.describe(101, R"("With Anna and Bo")");
    auto const renamed = t.update();
    CHECK_EQ(renamed.titles, 1u);
    CHECK_EQ(renamed.descriptions, 1u);
    CHECK((renamed.changed == std::vector<i64> { Ride }));
    CHECK((t.saved(Ride) == StravaTitle { .id = 101, .name = "Niagara loop", .description = "With Anna and Bo" }));
}

// At Strava's limit, the titles are all there, the descriptions that came
// are kept, and the rest wait for the next time, which fetches only those.
void test_descriptions_stop_at_the_limit()
{
    StravaStore t;
    auto const  activities = std::vector { strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara"),
        strava_activity(102, "2026-10-28T12:04:50Z", "Tempo run"), strava_activity(103, "2026-10-29T07:05:00Z", "Swim") };
    t.list(activities);
    t.describe(101, R"("With Anna")");
    t.fake.answer("/api/v3/activities/102", HttpResponse {
                                                .status = 429,
                                                .body = R"({"message":"Rate Limit Exceeded"})",
                                                .headers = { { "X-ReadRateLimit-Limit", "100,1000" }, { "X-ReadRateLimit-Usage", "101,300" } },
                                            });
    auto const limited = t.update();
    CHECK_EQ(limited.titles, 3u);
    CHECK_EQ(limited.descriptions, 1u);
    CHECK_EQ(limited.waiting, 2u);
    CHECK(limited.resets_at.has_value());
    CHECK((limited.changed == std::vector<i64> { Ride, Run, Swim }));
    CHECK(!t.saved(Run)->description.has_value());

    t.list(activities);
    t.describe(102, R"("Intervals")");
    t.describe(103, R"("")");
    auto const rest = t.update();
    CHECK_EQ(rest.titles, 0u);
    CHECK_EQ(rest.descriptions, 2u);
    CHECK_EQ(rest.waiting, 0u);
    CHECK((rest.changed == std::vector<i64> { Run, Swim }));
    CHECK_EQ(t.description_requests(), 4u);
    CHECK_EQ(t.saved(Run)->description.value_or("<none>"), "Intervals");
}

// Another failure stops the descriptions too, without a time to wait for.
void test_descriptions_stop_at_a_failure()
{
    StravaStore t;
    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara"), strava_activity(102, "2026-10-28T12:04:50Z", "Tempo run") });
    t.fake.answer("/api/v3/activities/101", HttpResponse { .status = 500, .body = "" });
    auto const update = t.update();
    CHECK_EQ(update.titles, 2u);
    CHECK_EQ(update.descriptions, 0u);
    CHECK_EQ(update.waiting, 2u);
    CHECK(!update.resets_at.has_value());
    CHECK_EQ(t.description_requests(), 1u);
}

// A sidecar that isn't a JSON object isn't overwritten: that activity is
// passed over, and its description isn't fetched.
void test_sidecar_that_isnt_an_object()
{
    StravaStore t;
    MUST(write_file_by_name(t.store.storage.sidecar_of(Ride).string(), std::string_view { "[1, 2]" }));
    t.list({ strava_activity(101, "2026-10-27T12:05:30Z", "Ride to Niagara") });
    auto const update = t.update();
    CHECK_EQ(update.titles, 0u);
    CHECK(update.changed.empty());
    CHECK_EQ(t.description_requests(), 0u);
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
    CHECK((MUST_EVAL(store.storage.activity_starts(Ride, Swim)) == std::vector<i64> { Ride, Run, Swim }));
    CHECK((MUST_EVAL(store.storage.activity_starts(Ride + 1, Swim - 1)) == std::vector<i64> { Run }));
    CHECK((MUST_EVAL(store.storage.activity_starts(0, Now)).size() == 4));
    CHECK_EQ(MUST_EVAL(store.storage.oldest_start()).value_or(0), Ride - 40 * 86400);
}

}

int main()
{
    test_match_nearest_within_a_minute();
    test_two_devices_match_the_same_activity();
    test_match_in_any_order();
    test_titles_and_descriptions();
    test_only_whats_new();
    test_descriptions_stop_at_the_limit();
    test_descriptions_stop_at_a_failure();
    test_sidecar_that_isnt_an_object();
    test_not_logged_in();
    test_activity_starts();
    return ST::Test::test_result();
}
