/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the Strava client with canned responses: nothing here talks to
 * Strava.
 */

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

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

// 2026-10-29T12:05:00Z.
constexpr i64 Now = 1793275500;

// Strava, as far as the tests need it: answers by the request's path,
// without its query, in the order the answers were given, and remembers
// the requests. Out of answers, the list of activities is empty, and
// anything else isn't found.
struct FakeStrava {
    std::map<std::string, std::vector<HttpResponse>> responses { };
    std::vector<HttpRequest>                         requests { };

    void answer(std::string const &path, HttpResponse response)
    {
        responses[path].push_back(std::move(response));
    }

    HttpFetch fetch()
    {
        return [this](HttpRequest const &request) -> std::expected<HttpResponse, LibCError> {
            requests.push_back(request);
            auto path = request.url.substr(StravaBaseUrl.size());
            path = path.substr(0, path.find('?'));
            auto &queue = responses[path];
            if (queue.empty()) {
                if (path == "/api/v3/athlete/activities") {
                    return HttpResponse { .status = 200, .body = "[]" };
                }
                return HttpResponse { .status = 404, .body = R"({"message":"Record Not Found"})" };
            }
            auto response = queue.front();
            queue.erase(queue.begin());
            return response;
        };
    }
};

OAuthTokens fresh_tokens()
{
    return OAuthTokens { .access_token = "access", .refresh_token = "refresh", .expires_at = Now + 6 * 3600 };
}

Strava logged_in(FakeStrava &fake, TempDir const &dir, std::optional<OAuthTokens> tokens = fresh_tokens())
{
    return Strava {
        .fetch = fake.fetch(),
        .client_id = "12345",
        .client_secret = "the-secret",
        .tokens_file = strava_tokens_file(dir.path),
        .tokens = std::move(tokens),
        .now = [] { return Now; },
    };
}

std::string token_response(std::string_view access, std::string_view refresh)
{
    return std::format(R"({{"token_type":"Bearer","expires_at":{},"expires_in":21600,"refresh_token":"{}","access_token":"{}","athlete":{{"id":1}}}})",
        Now + 21600, refresh, access);
}

std::string activity(u64 id, std::string_view start_date, std::string_view name)
{
    return std::format(R"({{"id":{},"name":"{}","start_date":"{}","elapsed_time":3600,"sport_type":"Ride"}})", id, name, start_date);
}

std::string list_of(std::vector<std::string> const &activities)
{
    std::string list = "[";
    for (auto const &a : activities) {
        list += (list.size() > 1 ? "," : "") + a;
    }
    return list + "]";
}

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
        full.push_back(activity(1000 + ix, "2026-10-20T12:00:00Z", std::format("Ride {}", ix)));
    }
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of(full) });
    fake.answer("/api/v3/athlete/activities", HttpResponse { .status = 200, .body = list_of({ activity(1, "2026-10-01T06:30:15Z", "Morning ride"), R"({"name":"No ID","start_date":"2026-10-01T06:30:15Z"})", R"({"id":3,"name":"No start"})" }) });
    auto const listed = strava.list_activities(100, Now);
    CHECK(listed.has_value());
    if (!listed) {
        return;
    }
    // Two pages: the second was short. The entries without an ID or a
    // start are skipped.
    CHECK_EQ(fake.requests.size(), 2uz);
    CHECK(fake.requests[0].url.contains("after=100") && fake.requests[0].url.contains(std::format("before={}", Now)));
    CHECK(fake.requests[0].url.contains("page=1") && fake.requests[0].url.contains("per_page=200"));
    CHECK(fake.requests[1].url.contains("page=2"));
    CHECK(has_header(fake.requests[0], "Authorization: Bearer access"));
    CHECK_EQ(listed->size(), 201uz);
    auto const &last = listed->back();
    CHECK_EQ(last.id, 1u);
    CHECK_EQ(last.name, "Morning ride");
    // 2026-10-01T06:30:15Z.
    CHECK_EQ(last.start_time, 1790836215);
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
    test_description();
    test_rate_limited();
    test_rate_limit_reset();
    test_tokens_file();
    return ST::Test::test_result();
}
