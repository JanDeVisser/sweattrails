/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the Wahoo download with canned responses: nothing here talks to
 * Wahoo.
 */

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

#include <IO.h>
#include <JSON.h>
#include <OAuth.h>
#include <TempStore.h>

#include <Http.h>
#include <storage/Wahoo.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

constexpr i64 Now = 1792500000;

// Wahoo, as far as the tests need it: answers by the request's path,
// without its query, or by the whole URL for a FIT file on the CDN, in the
// order the answers were given, and remembers the requests. Out of
// answers, the list of workouts is empty, and anything else isn't found.
struct FakeWahoo {
    std::map<std::string, std::vector<HttpResponse>> responses { };
    std::vector<HttpRequest>                         requests { };
    bool                                             offline { false };

    void answer(std::string const &path, HttpResponse response)
    {
        responses[path].push_back(std::move(response));
    }

    HttpFetch fetch()
    {
        return [this](HttpRequest const &request) -> std::expected<HttpResponse, LibCError> {
            requests.push_back(request);
            if (offline) {
                return std::unexpected(LibCError { std::string { "Could not resolve host" } });
            }
            auto path = request.url.starts_with(WahooBaseUrl) ? request.url.substr(WahooBaseUrl.size()) : request.url;
            path = path.substr(0, path.find('?'));
            auto &queue = responses[path];
            if (queue.empty()) {
                if (path == "/v1/workouts") {
                    return HttpResponse { .status = 200, .body = R"({"workouts":[],"total":0,"page":1,"per_page":30})" };
                }
                return HttpResponse { .status = 404, .body = "Not found" };
            }
            auto response = queue.front();
            queue.erase(queue.begin());
            return response;
        };
    }

    [[nodiscard]] size_t count(std::string_view path) const
    {
        return static_cast<size_t>(std::ranges::count_if(requests, [path](HttpRequest const &request) {
            return request.url.contains(path);
        }));
    }
};

bool has_header(HttpRequest const &request, std::string_view header)
{
    return std::ranges::find(request.headers, header) != request.headers.end();
}

bool has_authorization(HttpRequest const &request)
{
    return std::ranges::any_of(request.headers, [](std::string const &header) { return header.starts_with("Authorization:"); });
}

WahooTokens fresh_tokens()
{
    return WahooTokens {
        .access_token = "access",
        .refresh_token = "refresh",
        .expires_at = Now + 7200,
        .code_verifier = "verifier",
    };
}

// A Wahoo client logged in with `tokens`, which it saves in `dir`.
Wahoo logged_in(FakeWahoo &fake, TempDir const &dir, std::optional<WahooTokens> tokens = fresh_tokens())
{
    return Wahoo {
        .fetch = fake.fetch(),
        .client_id = "client",
        .tokens_file = wahoo_tokens_file(dir.path),
        .tokens = std::move(tokens),
        .now = [] { return Now; },
    };
}

std::string token_response(std::string_view access, std::string_view refresh)
{
    return std::format(R"({{"access_token":"{}","token_type":"Bearer","expires_in":7200,"refresh_token":"{}","scope":"workouts_read user_read offline_data","created_at":{}}})",
        access, refresh, Now);
}

std::string cdn_url(u64 id)
{
    return std::format("https://cdn.wahoo.example/{}.fit", id);
}

// A workout, trimmed from Wahoo's documentation's example. Without a URL,
// it has no summary.
JSONValue workout(u64 id, std::string_view starts, std::string_view name, bool with_file = true)
{
    auto json = MUST_EVAL(JSONValue::deserialize(std::format(R"({{"id":{},"starts":"{}","minutes":60,"name":"{}","workout_type_id":0}})", id, starts, name)));
    if (with_file) {
        json["workout_summary"] = MUST_EVAL(JSONValue::deserialize(std::format(R"({{"id":{},"file":{{"url":"{}"}}}})", id + 1000, cdn_url(id))));
    } else {
        json["workout_summary"] = JSONValue { };
    }
    return json;
}

std::string page_of(std::vector<JSONValue> const &workouts, i64 total, i64 page = 1)
{
    auto json = JSONValue::object();
    auto list = JSONValue::array();
    for (auto const &w : workouts) {
        list.append(w);
    }
    json["workouts"] = list;
    json["total"] = JSONValue { total };
    json["page"] = JSONValue { page };
    json["per_page"] = JSONValue { static_cast<i64>(30) };
    return json.serialize();
}

// 2026-10-20T12:00:00Z, 40 minutes before Now.
constexpr i64 Start = 1792497600;

// The start of a workout `days_before` days before Start, as Wahoo writes
// it.
std::string starts(i64 days_before)
{
    return std::format("2026-10-{:02}T12:00:00.000Z", 20 - days_before);
}

void test_tokens_file()
{
    TempDir    dir { "wahoo-tokens" };
    auto const file = wahoo_tokens_file(dir.path);
    CHECK_EQ(file.filename().string(), "wahoo-tokens.json");
    CHECK(!read_tokens(file).has_value());

    CHECK(write_tokens(file, fresh_tokens()).has_value());
    CHECK(fs::status(file).permissions() == (fs::perms::owner_read | fs::perms::owner_write));
    auto const tokens = read_tokens(file);
    CHECK(tokens.has_value());
    if (tokens) {
        CHECK_EQ(tokens->access_token, "access");
        CHECK_EQ(tokens->refresh_token, "refresh");
        CHECK_EQ(tokens->expires_at, Now + 7200);
        CHECK_EQ(tokens->code_verifier, "verifier");
    }

    MUST(write_file_by_name(file.string(), std::string_view { "{ not json" }));
    CHECK(!read_tokens(file).has_value());
    MUST(write_file_by_name(file.string(), std::string_view { R"({"access_token":"a"})" }));
    CHECK(!read_tokens(file).has_value());
}

void test_tokens_folder_is_made()
{
    TempDir    dir { "wahoo-folder" };
    auto const file = wahoo_tokens_file(dir.path / "sweattrails");
    CHECK(write_tokens(file, fresh_tokens()).has_value());
    CHECK(read_tokens(file).has_value());
}

void test_client_id()
{
    auto settings = JSONValue::object();
    CHECK(!wahoo_client_id(settings).has_value());
    settings["wahoo"] = JSONValue::object();
    CHECK(!wahoo_client_id(settings).has_value());
    settings["wahoo"]["client_id"] = JSONValue { "  " };
    CHECK(!wahoo_client_id(settings).has_value());
    settings["wahoo"]["client_id"] = JSONValue { " abc123 " };
    CHECK_EQ(wahoo_client_id(settings).value_or(""), "abc123");
    CHECK(!wahoo_client_id(JSONValue { }).has_value());
}

void test_authorize_url()
{
    FakeWahoo  fake;
    TempDir    dir { "wahoo-authorize" };
    auto const wahoo = logged_in(fake, dir, std::nullopt);
    auto const url = wahoo.authorize_url("the-challenge", "the state");
    CHECK(url.starts_with("https://api.wahooligan.com/oauth/authorize?"));
    auto const query = parse_query(url.substr(url.find('?') + 1));
    CHECK_EQ(query.at("client_id"), "client");
    CHECK_EQ(query.at("redirect_uri"), "https://localhost:8090/callback");
    CHECK_EQ(query.at("response_type"), "code");
    CHECK_EQ(query.at("scope"), "workouts_read user_read offline_data");
    CHECK_EQ(query.at("state"), "the state");
    CHECK_EQ(query.at("code_challenge"), "the-challenge");
    CHECK_EQ(query.at("code_challenge_method"), "S256");
    CHECK(fake.requests.empty());
}

void test_exchange_code()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-exchange" };
    auto      wahoo = logged_in(fake, dir, std::nullopt);
    fake.answer("/oauth/token", { .status = 200, .body = token_response("new-access", "new-refresh") });
    CHECK(wahoo.exchange_code("the code", "the-verifier").has_value());

    CHECK_EQ(fake.requests.size(), 1uz);
    auto const &request = fake.requests.front();
    CHECK_EQ(request.method, "POST");
    CHECK_EQ(request.url, "https://api.wahooligan.com/oauth/token");
    CHECK(has_header(request, "Content-Type: application/x-www-form-urlencoded"));
    CHECK(!has_authorization(request));
    auto const form = parse_query(request.body);
    CHECK_EQ(form.at("grant_type"), "authorization_code");
    CHECK_EQ(form.at("client_id"), "client");
    CHECK_EQ(form.at("code"), "the code");
    CHECK_EQ(form.at("redirect_uri"), "https://localhost:8090/callback");
    CHECK_EQ(form.at("code_verifier"), "the-verifier");
    CHECK(!form.contains("client_secret"));

    auto const saved = read_tokens(wahoo.tokens_file);
    CHECK(saved.has_value());
    if (saved) {
        CHECK_EQ(saved->access_token, "new-access");
        CHECK_EQ(saved->refresh_token, "new-refresh");
        CHECK_EQ(saved->expires_at, Now + 7200);
        CHECK_EQ(saved->code_verifier, "the-verifier");
    }
    CHECK(wahoo.tokens.has_value() && wahoo.tokens->access_token == "new-access");
}

void test_rejected_code()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-rejected-code" };
    auto      wahoo = logged_in(fake, dir, std::nullopt);
    fake.answer("/oauth/token", { .status = 400, .body = R"({"error":"invalid_grant"})" });
    auto const exchanged = wahoo.exchange_code("stale", "verifier");
    CHECK(!exchanged.has_value() && exchanged.error().kind == WahooError::Kind::NotLoggedIn);
    CHECK(!fs::exists(wahoo.tokens_file));

    fake.answer("/oauth/token", { .status = 200, .body = R"({"error":"what"})" });
    auto const no_tokens = wahoo.exchange_code("code", "verifier");
    CHECK(!no_tokens.has_value() && no_tokens.error().kind == WahooError::Kind::Failed);
    CHECK(!fs::exists(wahoo.tokens_file));
}

void test_not_logged_in()
{
    FakeWahoo  fake;
    TempDir    dir { "wahoo-not-logged-in" };
    auto       wahoo = logged_in(fake, dir, std::nullopt);
    auto const listed = wahoo.list_workouts(0);
    CHECK(!listed.has_value() && listed.error().kind == WahooError::Kind::NotLoggedIn);
    CHECK(fake.requests.empty());
}

void test_fresh_token_is_used_as_it_is()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-fresh" };
    auto      wahoo = logged_in(fake, dir);
    CHECK(wahoo.list_workouts(0).has_value());
    CHECK_EQ(fake.count("/oauth/token"), 0uz);
    CHECK(has_header(fake.requests.front(), "Authorization: Bearer access"));
}

void test_expiring_token_is_refreshed_first()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-refresh" };
    auto      tokens = fresh_tokens();
    tokens.expires_at = Now + 30;
    auto wahoo = logged_in(fake, dir, tokens);
    fake.answer("/oauth/token", { .status = 200, .body = token_response("access-2", "refresh-2") });
    CHECK(wahoo.list_workouts(0).has_value());

    CHECK_EQ(fake.requests.size(), 2uz);
    auto const form = parse_query(fake.requests.front().body);
    CHECK_EQ(form.at("grant_type"), "refresh_token");
    CHECK_EQ(form.at("refresh_token"), "refresh");
    CHECK_EQ(form.at("client_id"), "client");
    // Wahoo wants the verifier with a refresh too.
    CHECK_EQ(form.at("code_verifier"), "verifier");
    CHECK(has_header(fake.requests.back(), "Authorization: Bearer access-2"));

    // The new tokens were saved, with the verifier: the next refresh needs
    // both.
    auto const saved = read_tokens(wahoo.tokens_file);
    CHECK(saved && saved->refresh_token == "refresh-2" && saved->code_verifier == "verifier");
}

void test_rejected_refresh()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-rejected-refresh" };
    auto      tokens = fresh_tokens();
    tokens.expires_at = Now - 1000;
    auto wahoo = logged_in(fake, dir, tokens);
    fake.answer("/oauth/token", { .status = 400, .body = R"({"error":"invalid_grant"})" });
    auto const listed = wahoo.list_workouts(0);
    CHECK(!listed.has_value() && listed.error().kind == WahooError::Kind::NotLoggedIn);
    CHECK_EQ(fake.count("/v1/workouts"), 0uz);
}

void test_revoked_token()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-revoked" };
    auto      wahoo = logged_in(fake, dir);
    fake.answer("/v1/workouts", { .status = 401, .body = R"({"error":"invalid_token"})" });
    auto const listed = wahoo.list_workouts(0);
    CHECK(!listed.has_value() && listed.error().kind == WahooError::Kind::NotLoggedIn);
}

void test_rate_limit()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-rate-limit" };
    auto      wahoo = logged_in(fake, dir);
    fake.answer("/v1/workouts", { .status = 429, .body = "", .headers = { { "X-RateLimit-Limit", "25" }, { "x-ratelimit-reset", "250" } } });
    auto const listed = wahoo.list_workouts(0);
    CHECK(!listed.has_value());
    if (!listed) {
        CHECK(listed.error().kind == WahooError::Kind::Failed);
        CHECK_EQ(listed.error().message, "Wahoo's limit on requests is reached. Try again in 5 minutes.");
    }
    fake.answer("/v1/workouts", { .status = 429, .body = "" });
    auto const no_reset = wahoo.list_workouts(0);
    CHECK(!no_reset.has_value() && no_reset.error().message.ends_with("Try again later."));
}

void test_offline_and_server_errors()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-offline" };
    auto      wahoo = logged_in(fake, dir);
    fake.offline = true;
    auto const offline = wahoo.list_workouts(0);
    CHECK(!offline.has_value() && offline.error().message.starts_with("Could not reach Wahoo"));
    fake.offline = false;
    fake.answer("/v1/workouts", { .status = 500, .body = "" });
    auto const failed = wahoo.list_workouts(0);
    CHECK(!failed.has_value() && failed.error().kind == WahooError::Kind::Failed && failed.error().status == 500);
    fake.answer("/v1/workouts", { .status = 200, .body = "<html>" });
    CHECK(!wahoo.list_workouts(0).has_value());
}

void test_parse_workouts()
{
    auto json = MUST_EVAL(JSONValue::deserialize(page_of({
                                                             workout(1, "2026-10-20T12:00:00.000Z", "Morning ride"),
                                                             workout(2, "2026-10-19T12:00:00.000Z", "Indoor", false),
                                                         },
        2)));
    auto no_summary = MUST_EVAL(JSONValue::deserialize(R"({"id":3,"starts":"2026-10-18T12:00:00.000Z","name":"Bare"})"));
    auto no_file = MUST_EVAL(JSONValue::deserialize(R"({"id":4,"starts":"2026-10-17T12:00:00.000Z","workout_summary":{"id":9}})"));
    auto no_start = MUST_EVAL(JSONValue::deserialize(R"({"id":5,"name":"When?"})"));
    auto bad_start = MUST_EVAL(JSONValue::deserialize(R"({"id":6,"starts":"yesterday"})"));
    auto no_id = MUST_EVAL(JSONValue::deserialize(R"({"starts":"2026-10-17T12:00:00.000Z"})"));
    for (auto const &w : { no_summary, no_file, no_start, bad_start, no_id }) {
        json["workouts"].append(w);
    }
    auto const workouts = parse_workouts(json);
    CHECK(workouts.has_value());
    if (!workouts || workouts->size() != 4) {
        CHECK_EQ(workouts.value_or(std::vector<WahooWorkout> { }).size(), 4uz);
        return;
    }
    CHECK_EQ((*workouts)[0].id, 1u);
    CHECK_EQ((*workouts)[0].start_time, Start);
    CHECK_EQ((*workouts)[0].file_url, cdn_url(1));
    CHECK_EQ((*workouts)[0].json.try_get<std::string>("name").value_or(""), "Morning ride");
    CHECK_EQ((*workouts)[1].start_time, Start - 86400);
    CHECK((*workouts)[1].file_url.empty());
    CHECK((*workouts)[2].file_url.empty());
    CHECK((*workouts)[3].file_url.empty());

    CHECK(!parse_workouts(JSONValue::array()).has_value());
    CHECK(!parse_workouts(JSONValue::object()).has_value());
}

void test_list_stops_at_the_first_older_workout()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-list-older" };
    auto      wahoo = logged_in(fake, dir);
    fake.answer("/v1/workouts", { .status = 200, .body = page_of({ workout(1, starts(0), "a"), workout(2, starts(1), "b"), workout(3, starts(5), "c") }, 100) });
    auto const listed = wahoo.list_workouts(Start - 2 * 86400);
    CHECK(listed.has_value() && listed->size() == 2);
    CHECK_EQ(fake.count("/v1/workouts"), 1uz);
}

void test_list_pages()
{
    FakeWahoo              fake;
    TempDir                dir { "wahoo-list-pages" };
    auto                   wahoo = logged_in(fake, dir);
    std::vector<JSONValue> first;
    for (u64 ix = 0; ix < 30; ++ix) {
        first.push_back(workout(100 + ix, starts(0), "a"));
    }
    fake.answer("/v1/workouts", { .status = 200, .body = page_of(first, 31) });
    fake.answer("/v1/workouts", { .status = 200, .body = page_of({ workout(200, starts(1), "b") }, 31, 2) });
    auto const listed = wahoo.list_workouts(0);
    CHECK(listed.has_value() && listed->size() == 31);
    // The second page was the last, by the total: no third request.
    CHECK_EQ(fake.count("/v1/workouts"), 2uz);
    CHECK(fake.requests[0].url.ends_with("?page=1&per_page=30"));
    CHECK(fake.requests[1].url.ends_with("?page=2&per_page=30"));
}

void test_list_stops_at_an_empty_page()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-list-empty" };
    auto      wahoo = logged_in(fake, dir);
    // A total that's wrong, or missing: the empty page ends it anyway.
    fake.answer("/v1/workouts", { .status = 200, .body = R"({"workouts":[]})" });
    CHECK(wahoo.list_workouts(0).has_value());
    CHECK_EQ(fake.count("/v1/workouts"), 1uz);
}

void test_download_new_workouts()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-download" };
    TempStore store;
    auto      wahoo = logged_in(fake, dir);
    store.add_activity(Start - 86400);
    MUST(write_file_by_name((store.storage.inbox_dir / "wahoo-3.fit").string(), std::string_view { "waiting" }));
    fake.answer("/v1/workouts", { .status = 200, .body = page_of({
                                                                     workout(1, starts(0), "Morning ride"),
                                                                     workout(2, starts(1), "In the store"),
                                                                     workout(3, starts(2), "In the inbox"),
                                                                     workout(4, starts(3), "No file", false),
                                                                     workout(5, starts(4), "Another ride"),
                                                                 },
                                                     5) });
    fake.answer(cdn_url(1), { .status = 200, .body = "fit 1" });
    fake.answer(cdn_url(5), { .status = 200, .body = "fit 5" });
    std::vector<std::pair<size_t, size_t>> progress;
    auto const                             count = download_new_workouts(wahoo, store.storage, 0, [&progress](size_t index, size_t count) {
        progress.emplace_back(index, count);
    });
    CHECK_EQ(count.value_or(99), 2uz);
    CHECK((progress == std::vector<std::pair<size_t, size_t>> { { 0, 2 }, { 1, 2 } }));
    CHECK((store.inbox() == std::vector<std::string> { "wahoo-1.fit", "wahoo-1.json", "wahoo-3.fit", "wahoo-5.fit", "wahoo-5.json" }));
    CHECK_EQ(read_file_by_name((store.storage.inbox_dir / "wahoo-1.fit").string()).value_or(""), "fit 1");
    auto const sidecar = MUST_EVAL(JSONValue::deserialize(MUST_EVAL(read_file_by_name((store.storage.inbox_dir / "wahoo-1.json").string()))));
    CHECK_EQ(sidecar.try_get<std::string>("name").value_or(""), "Morning ride");
    // The CDN doesn't get the token.
    for (auto const &request : fake.requests) {
        if (request.url.starts_with("https://cdn.")) {
            CHECK(!has_authorization(request));
        }
    }
}

void test_unavailable_file_is_skipped()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-unavailable" };
    TempStore store;
    auto      wahoo = logged_in(fake, dir);
    fake.answer("/v1/workouts", { .status = 200, .body = page_of({ workout(1, starts(0), "Gone"), workout(2, starts(1), "Here") }, 2) });
    fake.answer(cdn_url(1), { .status = 403, .body = "" });
    fake.answer(cdn_url(2), { .status = 200, .body = "fit 2" });
    CHECK_EQ(download_new_workouts(wahoo, store.storage, 0).value_or(99), 1uz);
    CHECK((store.inbox() == std::vector<std::string> { "wahoo-2.fit", "wahoo-2.json" }));
}

void test_failed_download_keeps_the_earlier_ones()
{
    FakeWahoo fake;
    TempDir   dir { "wahoo-failed" };
    TempStore store;
    auto      wahoo = logged_in(fake, dir);
    fake.answer("/v1/workouts", { .status = 200, .body = page_of({ workout(1, starts(0), "a"), workout(2, starts(1), "b"), workout(3, starts(2), "c") }, 3) });
    fake.answer(cdn_url(1), { .status = 200, .body = "fit 1" });
    fake.answer(cdn_url(2), { .status = 500, .body = "" });
    auto const count = download_new_workouts(wahoo, store.storage, 0);
    CHECK(!count.has_value() && count.error().status == 500);
    CHECK((store.inbox() == std::vector<std::string> { "wahoo-1.fit", "wahoo-1.json" }));
    CHECK_EQ(fake.count(cdn_url(3)), 0uz);
}

}

int main()
{
    test_tokens_file();
    test_tokens_folder_is_made();
    test_client_id();
    test_authorize_url();
    test_exchange_code();
    test_rejected_code();
    test_not_logged_in();
    test_fresh_token_is_used_as_it_is();
    test_expiring_token_is_refreshed_first();
    test_rejected_refresh();
    test_revoked_token();
    test_rate_limit();
    test_offline_and_server_errors();
    test_parse_workouts();
    test_list_stops_at_the_first_older_workout();
    test_list_pages();
    test_list_stops_at_an_empty_page();
    test_download_new_workouts();
    test_unavailable_file_is_skipped();
    test_failed_download_keeps_the_earlier_ones();
    return ST::Test::test_result();
}
