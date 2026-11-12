/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the Strava client with canned responses: nothing here talks to
 * Strava.
 */

#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <FakeStrava.h>
#include <IO.h>
#include <JSON.h>

#include <Http.h>
#include <OAuth.h>
#include <storage/Strava.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

QueryParameters form_of(HttpRequest const &request)
{
    return parse_query(request.body);
}

bool has_header(HttpRequest const &request, std::string_view header)
{
    return std::ranges::find(request.headers, header) != request.headers.end();
}

void test_authorize_url()
{
    FakeStrava fake;
    TempDir    dir { "strava-url" };
    auto const url = logged_in(fake, dir).authorize_url("the-state");
    CHECK(url.starts_with("https://www.strava.com/oauth/authorize?"));
    auto const query = parse_query(url.substr(url.find('?') + 1));
    CHECK_EQ(query.at("client_id"), "12345");
    CHECK_EQ(query.at("redirect_uri"), "https://localhost:8090/callback");
    CHECK_EQ(query.at("response_type"), "code");
    CHECK_EQ(query.at("scope"), "activity:read_all");
    CHECK_EQ(query.at("state"), "the-state");
    // The secret is never in a URL.
    CHECK(!url.contains("the-secret"));
}

void test_exchange_code()
{
    FakeStrava fake;
    TempDir    dir { "strava-exchange" };
    auto       strava = logged_in(fake, dir, std::nullopt);
    fake.answer("/oauth/token", HttpResponse { .status = 200, .body = token_response("access-1", "refresh-1") });
    CHECK(strava.exchange_code("the-code").has_value());
    CHECK_EQ(fake.requests.size(), 1uz);
    auto const form = form_of(fake.requests.front());
    CHECK_EQ(fake.requests.front().method, "POST");
    CHECK_EQ(form.at("grant_type"), "authorization_code");
    CHECK_EQ(form.at("code"), "the-code");
    CHECK_EQ(form.at("client_secret"), "the-secret");
    auto const saved = read_tokens(strava.tokens_file);
    CHECK(saved.has_value());
    if (saved) {
        CHECK_EQ(saved->access_token, "access-1");
        CHECK_EQ(saved->refresh_token, "refresh-1");
        CHECK_EQ(saved->expires_at, Now + 21600);
        CHECK(!saved->code_verifier.has_value());
    }
}

void test_exchange_code_refused()
{
    FakeStrava fake;
    TempDir    dir { "strava-refused" };
    auto       strava = logged_in(fake, dir, std::nullopt);
    fake.answer("/oauth/token", HttpResponse { .status = 400, .body = R"({"message":"Bad Request","errors":[{"resource":"AuthorizationCode","field":"code","code":"invalid"}]})" });
    auto const refused = strava.exchange_code("old-code");
    CHECK(!refused.has_value() && refused.error().kind == StravaError::Kind::NotLoggedIn);
    fake.answer("/oauth/token", HttpResponse { .status = 401, .body = R"({"message":"Bad Request","errors":[{"resource":"Application","field":"client_secret","code":"invalid"}]})" });
    auto const bad_secret = strava.exchange_code("code");
    CHECK(!bad_secret.has_value() && bad_secret.error().kind == StravaError::Kind::Failed);
    CHECK(!bad_secret.has_value() && bad_secret.error().message.contains("Bad Request"));
    CHECK(!read_tokens(strava.tokens_file).has_value());
}

// Strava gives a new refresh token with each refresh, and the old one
// stops working at once.
void test_refresh_saves_the_new_refresh_token()
{
    FakeStrava fake;
    TempDir    dir { "strava-refresh" };
    auto       strava = logged_in(fake, dir, OAuthTokens { .access_token = "old", .refresh_token = "refresh-1", .expires_at = Now - 10 });
    fake.answer("/oauth/token", HttpResponse { .status = 200, .body = token_response("access-2", "refresh-2") });
    fake.answer("/api/v3/activities/7", HttpResponse { .status = 200, .body = R"({"id":7,"description":"Windy"})" });
    auto const description = strava.description(7);
    CHECK_EQ(description.value_or("?"), "Windy");
    CHECK_EQ(fake.requests.size(), 2uz);
    auto const form = form_of(fake.requests.front());
    CHECK_EQ(form.at("grant_type"), "refresh_token");
    CHECK_EQ(form.at("refresh_token"), "refresh-1");
    CHECK_EQ(form.at("client_secret"), "the-secret");
    CHECK(has_header(fake.requests.back(), "Authorization: Bearer access-2"));
    auto const saved = read_tokens(strava.tokens_file);
    CHECK(saved && saved->refresh_token == "refresh-2");
}

void test_not_logged_in()
{
    FakeStrava fake;
    TempDir    dir { "strava-no-login" };
    auto       none = logged_in(fake, dir, std::nullopt);
    auto const listed = none.list_activities(0, Now);
    CHECK(!listed.has_value() && listed.error().kind == StravaError::Kind::NotLoggedIn);
    CHECK(fake.requests.empty());

    auto strava = logged_in(fake, dir);
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 401, .body = R"({"message":"Authorization Error"})" });
    auto const revoked = strava.list_activities(0, Now);
    CHECK(!revoked.has_value() && revoked.error().kind == StravaError::Kind::NotLoggedIn);

    // A refresh token Strava doesn't take any more.
    auto expired = logged_in(fake, dir, OAuthTokens { .access_token = "old", .refresh_token = "gone", .expires_at = Now - 10 });
    fake.answer("/oauth/token", HttpResponse { .status = 400, .body = R"({"message":"Bad Request","errors":[{"resource":"RefreshToken","field":"refresh_token","code":"invalid"}]})" });
    auto const refused = expired.list_activities(0, Now);
    CHECK(!refused.has_value() && refused.error().kind == StravaError::Kind::NotLoggedIn);
}

void test_list_activities()
{
    FakeStrava               fake;
    TempDir                  dir { "strava-list" };
    auto                     strava = logged_in(fake, dir);
    std::vector<std::string> full;
    for (auto ix = 0u; ix < 200; ++ix) {
        full.push_back(strava_activity(1000 + ix, "2026-10-20T12:00:00Z", std::format("Ride {}", ix)));
    }
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of(full) });
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of({ strava_activity(1, "2026-10-01T06:30:15Z", "Morning ride"), R"({"name":"No ID","start_date":"2026-10-01T06:30:15Z"})", R"({"id":3,"name":"No start"})" }) });
    auto const listed = strava.list_activities(100, Now);
    CHECK(listed.has_value());
    if (!listed) {
        return;
    }
    // Two pages: the second was short. The entries without an ID or a
    // start are skipped.
    CHECK_EQ(fake.requests.size(), 2uz);
    CHECK(fake.requests[0].url.contains("after=100") && fake.requests[0].url.contains(std::format("before={}", Now)));
    CHECK(fake.requests[0].url.contains("per_page=200"));
    CHECK(!fake.requests[0].url.contains("&page="));
    // The first page's activities all start at 2026-10-20T12:00:00Z, so
    // it says nothing about which way the list goes: newest first, as
    // Strava lists, and the window ends a second after that.
    CHECK(fake.requests[1].url.contains("after=100&before=1792497601"));
    CHECK(has_header(fake.requests[0], "Authorization: Bearer access"));
    CHECK_EQ(listed->activities.size(), 201uz);
    CHECK(!listed->stopped.has_value());
    auto const &last = listed->activities.back();
    CHECK_EQ(last.id, 1u);
    CHECK_EQ(last.name, "Morning ride");
    // 2026-10-01T06:30:15Z.
    CHECK_EQ(last.start_time, 1790836215);
}

// A Unix time as Strava writes it: 2026-10-01T00:00:00Z.
std::string utc(i64 time)
{
    return std::format("{:%FT%TZ}", std::chrono::sys_seconds { std::chrono::seconds { time } });
}

// The list doesn't page: each request moves the window past the last
// activity listed. Oldest first, the window's start moves. An activity
// listed twice, as the one at the window's edge is, is kept once.
void test_list_moves_window()
{
    FakeStrava fake;
    TempDir    dir { "strava-list-window" };
    auto       strava = logged_in(fake, dir);
    // Oldest first: 2026-10-01T00:00:00Z on.
    auto const page = [](u64 first_id, i64 first_start, size_t count) {
        std::vector<std::string> ret;
        for (auto ix = 0uz; ix < count; ++ix) {
            ret.push_back(strava_activity(first_id + ix, utc(first_start + static_cast<i64>(ix) * 60), "Ride"));
        }
        return list_of(ret);
    };
    i64 const start = 1790812800;
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = page(1, start, 200) });
    // Starts with the last one of the page before.
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = page(200, start + 199 * 60, 200) });
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = page(399, start + 398 * 60, 10) });
    auto const listed = strava.list_activities(0, Now);
    CHECK(listed.has_value() && !listed->stopped.has_value());
    if (!listed) {
        return;
    }
    CHECK_EQ(fake.requests.size(), 3uz);
    CHECK(fake.requests[1].url.contains(std::format("after={}&before={}", start + 199 * 60 - 1, Now)));
    CHECK(fake.requests[2].url.contains(std::format("after={}&before={}", start + 398 * 60 - 1, Now)));
    CHECK_EQ(listed->activities.size(), 408uz);
    CHECK_EQ(listed->activities.back().id, 408u);
}

// Newest first, as Strava lists, the window's end moves instead. And a full page with
// nothing new can't move the window, so the list ends there.
void test_list_moves_window_back()
{
    FakeStrava               fake;
    TempDir                  dir { "strava-list-window-back" };
    auto                     strava = logged_in(fake, dir);
    std::vector<std::string> newest;
    for (auto ix = 0u; ix < 200; ++ix) {
        newest.push_back(strava_activity(1 + ix, utc(1790812800 - static_cast<i64>(ix) * 60), "Ride"));
    }
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of(newest) });
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of(newest) });
    auto const listed = strava.list_activities(0, Now);
    CHECK(listed.has_value() && !listed->stopped.has_value());
    if (!listed) {
        return;
    }
    CHECK_EQ(fake.requests.size(), 2uz);
    CHECK(fake.requests[1].url.contains(std::format("after=0&before={}", 1790812800 - 199 * 60 + 1)));
    CHECK_EQ(listed->activities.size(), 200uz);
}

// A Strava with a history, newest first, that answers the list as Strava
// does, `after`, `before`, `per_page` and `page`, and with HTTP 404 for a
// page with a lost activity in it.
struct History {
    struct Entry {
        u64  id;
        i64  start;
        bool lost { false };
    };
    std::vector<Entry>       entries;
    std::vector<std::string> urls { };

    HttpFetch fetch()
    {
        return [this](HttpRequest const &request) -> std::expected<HttpResponse, LibCError> {
            urls.push_back(request.url);
            auto const parameter = [&request](std::string const &name, i64 otherwise) {
                auto at = request.url.find(std::format("?{}=", name));
                if (at == std::string::npos) {
                    at = request.url.find(std::format("&{}=", name));
                }
                if (at == std::string::npos) {
                    return otherwise;
                }
                return std::stoll(request.url.substr(at + name.size() + 2));
            };
            auto const         after = parameter("after", 0);
            auto const         before = parameter("before", Now);
            auto const         per_page = static_cast<size_t>(parameter("per_page", 30));
            auto const         page = static_cast<size_t>(parameter("page", 1));
            std::vector<Entry> window;
            for (auto const &entry : entries) {
                if (entry.start > after && entry.start < before) {
                    window.push_back(entry);
                }
            }
            std::vector<std::string> listed;
            for (auto ix = (page - 1) * per_page; ix < std::min(page * per_page, window.size()); ++ix) {
                if (window[ix].lost) {
                    return HttpResponse { .status = 404, .body = R"({"message":"Record Not Found","errors":[{"resource":"Upload","field":"id","code":"invalid"}]})" };
                }
                listed.push_back(strava_activity(window[ix].id, utc(window[ix].start), "Ride"));
            }
            return HttpResponse { .status = 200, .body = list_of(listed) };
        };
    }
};

// A history of `count` activities an hour apart, newest first, with the
// ones in `lost` lost.
History history(size_t count, std::set<size_t> const &lost)
{
    History ret;
    for (auto ix = 0uz; ix < count; ++ix) {
        ret.entries.push_back({ .id = 1 + ix, .start = Now - 3600 * static_cast<i64>(1 + ix), .lost = lost.contains(ix) });
    }
    return ret;
}

// Strava answers HTTP 404 for a page with an activity whose upload it has
// lost, as Jan's history has. Smaller pages find it, it's skipped, and
// the list goes on past it.
void test_list_skips_lost()
{
    FakeStrava fake;
    TempDir    dir { "strava-list-lost" };
    auto       strava = logged_in(fake, dir);
    auto       strava_history = history(500, { 336 });
    strava.fetch = strava_history.fetch();
    auto const listed = strava.list_activities(0, Now);
    CHECK(listed.has_value() && !listed->stopped.has_value());
    if (!listed) {
        return;
    }
    CHECK_EQ(listed->activities.size(), 499uz);
    CHECK(std::ranges::none_of(listed->activities, [](auto const &activity) { return activity.id == 337; }));
    // The first page, the second's 404, 7 smaller ones to find the lost
    // one, the one after it on its own, and the rest of the list.
    CHECK_EQ(strava_history.urls.size(), 11uz);
}

// Lost activities at the start of a page, next to each other, and at the
// very end of the list.
void test_list_skips_lost_at_the_edges()
{
    FakeStrava fake;
    TempDir    dir { "strava-list-lost-edges" };
    auto       strava = logged_in(fake, dir);
    auto       strava_history = history(450, { 0, 1, 250, 449 });
    strava.fetch = strava_history.fetch();
    auto const listed = strava.list_activities(0, Now);
    CHECK(listed.has_value() && !listed->stopped.has_value());
    if (!listed) {
        return;
    }
    CHECK_EQ(listed->activities.size(), 446uz);
    std::set<u64> ids;
    for (auto const &activity : listed->activities) {
        ids.insert(activity.id);
    }
    CHECK_EQ(ids.size(), 446uz);
    CHECK(!ids.contains(1) && !ids.contains(2) && !ids.contains(251) && !ids.contains(450));
    CHECK(ids.contains(3) && ids.contains(252) && ids.contains(449));
}

// A 404 that smaller pages don't explain stops the list, as any error.
void test_list_lost_everything()
{
    FakeStrava       fake;
    TempDir          dir { "strava-list-lost-everything" };
    auto             strava = logged_in(fake, dir);
    std::set<size_t> all;
    for (auto ix = 0uz; ix < 300; ++ix) {
        all.insert(ix);
    }
    auto strava_history = history(300, all);
    strava.fetch = strava_history.fetch();
    auto const listed = strava.list_activities(0, Now);
    CHECK(!listed.has_value());
}

// A page that fails after the first keeps the pages before it, and says
// why it stopped. On the first page, the failure is the list's.
void test_list_stopped_short()
{
    FakeStrava               fake;
    TempDir                  dir { "strava-list-short" };
    auto                     strava = logged_in(fake, dir);
    std::vector<std::string> full;
    for (auto ix = 0u; ix < 200; ++ix) {
        full.push_back(strava_activity(1000 + ix, "2026-10-20T12:00:00Z", std::format("Ride {}", ix)));
    }
    // Not asked again: see test_retries.
    strava.retries = 0;
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of(full) });
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 500, .body = "" });
    auto const listed = strava.list_activities(0, Now);
    CHECK(listed.has_value());
    if (!listed) {
        return;
    }
    CHECK_EQ(listed->activities.size(), 200uz);
    CHECK(listed->stopped.has_value() && listed->stopped->status == 500);

    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 500, .body = "" });
    auto const first = strava.list_activities(0, Now);
    CHECK(!first.has_value() && first.error().status == 500);
}

// A request Strava doesn't answer in time, or answers with a server error,
// is asked again after a wait, and the list carries on. A download of the
// whole history used to end with Strava's timeout.
void test_retries()
{
    FakeStrava fake;
    TempDir    dir { "strava-retries" };
    auto       strava = logged_in(fake, dir);
    size_t     waits { 0 };
    strava.wait = [&waits](std::chrono::seconds pause) {
        CHECK_EQ(pause, std::chrono::seconds { 30 });
        ++waits;
    };
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 0 });
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 502, .body = "Bad gateway" });
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of({ strava_activity(1, "2026-10-01T06:30:15Z", "Morning ride") }) });
    auto const listed = strava.list_activities(0, Now);
    CHECK(listed.has_value() && !listed->stopped.has_value());
    if (listed) {
        CHECK_EQ(listed->activities.size(), 1uz);
    }
    CHECK_EQ(waits, 2uz);
    CHECK_EQ(fake.requests.size(), 3uz);
}

// Past the retries, the error is the request's, said briefly: the URL is
// in the log, not in the message.
void test_retries_run_out()
{
    FakeStrava fake;
    TempDir    dir { "strava-retries-run-out" };
    auto       strava = logged_in(fake, dir);
    size_t     waits { 0 };
    strava.wait = [&waits](std::chrono::seconds) { ++waits; };
    for (auto ix = 0; ix < 4; ++ix) {
        fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 0 });
    }
    auto const listed = strava.list_activities(0, Now);
    CHECK(!listed.has_value());
    if (!listed) {
        CHECK_EQ(listed.error().message, "Could not reach Strava: Timeout was reached");
    }
    CHECK_EQ(waits, 3uz);
    CHECK_EQ(fake.requests.size(), 4uz);
}

void test_description()
{
    FakeStrava fake;
    TempDir    dir { "strava-description" };
    auto       strava = logged_in(fake, dir);
    fake.answer("/api/v3/activities/5", HttpResponse { .status = 200, .body = R"({"id":5,"name":"Ride","description":null})" });
    CHECK_EQ(strava.description(5).value_or("?"), "");
    fake.answer("/api/v3/activities/6", HttpResponse { .status = 200, .body = R"({"id":6,"name":"Ride"})" });
    CHECK_EQ(strava.description(6).value_or("?"), "");
    auto const missing = strava.description(8);
    CHECK(!missing.has_value() && missing.error().status == 404);
}

HttpResponse too_many(std::string_view usage)
{
    return HttpResponse {
        .status = 429,
        .body = R"({"message":"Rate Limit Exceeded"})",
        .headers = { { "X-ReadRateLimit-Limit", "100,1000" }, { "X-ReadRateLimit-Usage", std::string { usage } } },
    };
}

void test_rate_limited()
{
    FakeStrava fake;
    TempDir    dir { "strava-limit" };
    auto       strava = logged_in(fake, dir);
    fake.answer("/api/v3/activities/5", too_many("101,400"));
    auto const limited = strava.description(5);
    CHECK(!limited.has_value() && limited.error().kind == StravaError::Kind::RateLimited);
    // 12:05 is limited until 12:15.
    CHECK_EQ(limited.error().resets_at, Now + 10 * 60);
}

void test_rate_limit_reset()
{
    // The 15-minute limit, at the next quarter hour.
    CHECK_EQ(rate_limit_reset(too_many("101,400"), Now), Now + 10 * 60);
    CHECK_EQ(rate_limit_reset(too_many("101,400"), Now + 10 * 60), Now + 25 * 60);
    // The day's, at midnight UTC: 2026-10-30T00:00:00Z.
    CHECK_EQ(rate_limit_reset(too_many("40,1000"), Now), 1793318400);
    CHECK_EQ(rate_limit_reset(too_many("40, 1003"), Now), 1793318400);
    // Without the headers, or with ones it can't read, the quarter hour.
    CHECK_EQ(rate_limit_reset(HttpResponse { .status = 429 }, Now), Now + 10 * 60);
    CHECK_EQ(rate_limit_reset(too_many("lots"), Now), Now + 10 * 60);
}

void test_tokens_file()
{
    TempDir dir { "strava-tokens" };
    CHECK_EQ(strava_tokens_file(dir.path).filename().string(), "strava-tokens.json");
    CHECK(write_tokens(strava_tokens_file(dir.path), fresh_tokens()).has_value());
    auto const perms = fs::status(strava_tokens_file(dir.path)).permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
}

}

int main()
{
    test_authorize_url();
    test_exchange_code();
    test_exchange_code_refused();
    test_refresh_saves_the_new_refresh_token();
    test_not_logged_in();
    test_list_activities();
    test_list_moves_window();
    test_list_moves_window_back();
    test_list_skips_lost();
    test_list_skips_lost_at_the_edges();
    test_list_lost_everything();
    test_retries();
    test_retries_run_out();
    test_list_stopped_short();
    test_description();
    test_rate_limited();
    test_rate_limit_reset();
    test_tokens_file();
    return ST::Test::test_result();
}
