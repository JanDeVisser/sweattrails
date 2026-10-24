/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the Garmin Connect download with canned responses: nothing here
 * talks to Garmin.
 */

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <map>
#include <string>
#include <vector>

#include <IO.h>
#include <JSON.h>
#include <TempStore.h>

#include <Http.h>
#include <storage/Activity.h>
#include <storage/Garmin.h>
#include <storage/Storage.h>

#include <TestApp.h>
#include <TestFit.h>
#include <TestZip.h>

using namespace ST;
using namespace ST::Test;

namespace {

namespace fs = std::filesystem;

// Garmin Connect, as far as the tests need it: answers by the request's
// path, without its query, in the order the answers were given, and
// remembers the requests. Out of answers, the activity list is empty, as
// past its last page, and anything else isn't found.
struct FakeGarmin {
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
            auto path = request.url.substr(GarminBaseUrl.size());
            path = path.substr(0, path.find('?'));
            auto &queue = responses[path];
            if (queue.empty()) {
                if (path == "/activitylist-service/activities/search/activities") {
                    return HttpResponse { .status = 200, .body = "[]" };
                }
                return HttpResponse { .status = 404, .body = "Not found" };
            }
            auto response = queue.front();
            queue.erase(queue.begin());
            return response;
        };
    }
};

std::string const TokenPath = "/personalapitoken-service/personal-api/personal-api-key/token";
std::string const ListPath = "/activitylist-service/activities/search/activities";

std::string download_path(u64 id)
{
    return std::format("/download-service/files/activity/{}", id);
}

// How many activities were downloaded, as opposed to listed.
size_t downloads(FakeGarmin const &fake)
{
    return static_cast<size_t>(std::ranges::count_if(fake.requests, [](HttpRequest const &request) {
        return request.url.contains("/download-service/");
    }));
}

bool has_header(HttpRequest const &request, std::string_view header)
{
    return std::ranges::find(request.headers, header) != request.headers.end();
}

// A list entry, trimmed from a real one: the list has 101 fields.
JSONValue list_entry(u64 id, i64 start_time, std::string_view name, bool manual = false)
{
    auto entry = JSONValue::object();
    entry["activityId"] = JSONValue { static_cast<i64>(id) };
    entry["activityName"] = JSONValue { name };
    entry["beginTimestamp"] = JSONValue { start_time * 1000 };
    entry["startTimeGMT"] = JSONValue { "2026-10-20 12:09:31" };
    entry["manualActivity"] = JSONValue { manual };
    return entry;
}

std::string list_of(std::vector<JSONValue> const &entries)
{
    auto list = JSONValue::array();
    for (auto const &entry : entries) {
        list.append(entry);
    }
    return list.serialize();
}

std::string fit_zip(u64 id, std::string const &fit)
{
    return make_zip({ { .name = std::format("{}_ACTIVITY.fit", id), .contents = fit, .deflated = true } });
}

void test_key_from_the_environment_or_the_file()
{
    TempDir    dir { "key" };
    auto const file = dir.path / "connect-personal-api-key";
    CHECK(!read_api_key({ }, file).has_value());

    MUST(write_file_by_name(file.string(), std::string_view { "  file-key\n" }));
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
    CHECK_EQ(read_api_key({ }, file).value_or(""), "file-key");
    CHECK_EQ(read_api_key("env-key\n", file).value_or(""), "env-key");
    // An empty variable is as good as none.
    CHECK_EQ(read_api_key("  ", file).value_or(""), "file-key");

    MUST(write_file_by_name(file.string(), std::string_view { "\n" }));
    CHECK(!read_api_key({ }, file).has_value());
}

void test_parse_activity_list()
{
    auto const json = MUST_EVAL(JSONValue::deserialize(list_of({
        list_entry(24790584050, 1792498171, "Zwift - Pacer Group Ride with Maria"),
        list_entry(24790584051, 1792500000, "Yoga", true),
    })));
    auto const activities = parse_activity_list(json);
    CHECK(activities.has_value());
    if (!activities || activities->size() != 2) {
        CHECK(false);
        return;
    }
    CHECK_EQ(activities->at(0).id, 24790584050ull);
    CHECK_EQ(activities->at(0).start_time, 1792498171ll);
    CHECK(activities->at(0).has_file);
    CHECK(!activities->at(1).has_file);
    CHECK_EQ(activities->at(0).summary["activityName"].to_string(), "Zwift - Pacer Group Ride with Maria");

    CHECK(!parse_activity_list(JSONValue::object()).has_value());
}

// An entry without an ID or start time is skipped; the others are kept.
void test_odd_entries_are_skipped()
{
    auto const json = MUST_EVAL(JSONValue::deserialize(R"([
        {"beginTimestamp": 1792000000000},
        {"activityId": 2, "beginTimestamp": "yesterday"},
        {"activityId": 3},
        {"activityId": 4, "beginTimestamp": 1792000000000}
    ])"));
    auto const activities = parse_activity_list(json);
    CHECK(activities.has_value());
    CHECK_EQ(activities.value_or(std::vector<GarminActivity> { }).size(), 1uz);
    if (activities && activities->size() == 1) {
        CHECK_EQ(activities->at(0).id, 4ull);
    }
}

void test_start_date()
{
    setenv("TZ", "America/Toronto", 1);
    tzset();
    // October 23, 2026, 07:18 in Toronto: 30 days earlier is September 23.
    CHECK_EQ(garmin_start_date(30, 1792754314), "2026-09-23");
    // 23:30 on October 22 in Toronto is already the 23rd in UTC.
    CHECK_EQ(garmin_start_date(0, 1792726200), "2026-10-22");
}

void test_token()
{
    FakeGarmin fake;
    fake.answer(TokenPath, { .status = 200, .body = "header.payload.signature\n" });
    GarminConnect garmin { .fetch = fake.fetch() };
    CHECK(garmin.obtain_token("the-key").has_value());
    CHECK_EQ(garmin.token, "header.payload.signature");
    CHECK_EQ(fake.requests.size(), 1uz);
    if (!fake.requests.empty()) {
        CHECK_EQ(fake.requests[0].method, "POST");
        CHECK(has_header(fake.requests[0], "Authorization: ApiKey the-key"));
    }
}

void test_rejected_key()
{
    for (long const status : { 401, 403 }) {
        FakeGarmin fake;
        fake.answer(TokenPath, { .status = status, .body = "Unauthorized" });
        GarminConnect garmin { .fetch = fake.fetch() };
        auto const    result = garmin.obtain_token("old-key");
        CHECK(!result.has_value() && result.error().kind == GarminError::Kind::KeyRejected);
    }
}

void test_offline_and_server_errors()
{
    FakeGarmin fake;
    fake.offline = true;
    GarminConnect garmin { .fetch = fake.fetch() };
    auto const    offline = garmin.obtain_token("key");
    CHECK(!offline.has_value() && offline.error().kind == GarminError::Kind::Failed);

    FakeGarmin broken;
    broken.answer(TokenPath, { .status = 500, .body = "Oops" });
    GarminConnect server { .fetch = broken.fetch() };
    auto const    error = server.obtain_token("key");
    CHECK(!error.has_value() && error.error().kind == GarminError::Kind::Failed);
}

std::string page_of(size_t count, u64 first_id)
{
    std::vector<JSONValue> entries;
    for (u64 ix = 0; ix < count; ++ix) {
        entries.push_back(list_entry(first_id + ix, 1792000000 - static_cast<i64>(first_id + ix), "Run"));
    }
    return list_of(entries);
}

// Pages are asked for until one comes back empty.
void test_list_pages()
{
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 200, .body = page_of(100, 1000) });
    fake.answer(ListPath, { .status = 200, .body = page_of(1, 2000) });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    auto const    activities = garmin.list_activities("2026-09-23");
    CHECK(activities.has_value());
    CHECK_EQ(activities.value_or(std::vector<GarminActivity> { }).size(), 101uz);
    CHECK_EQ(fake.requests.size(), 3uz);
    if (fake.requests.size() == 3) {
        CHECK(fake.requests[0].url.ends_with("?start=0&limit=100&startDate=2026-09-23"));
        CHECK(fake.requests[1].url.ends_with("?start=100&limit=100&startDate=2026-09-23"));
        CHECK(fake.requests[2].url.ends_with("?start=101&limit=100&startDate=2026-09-23"));
        CHECK(has_header(fake.requests[0], "Authorization: Bearer token"));
    }
}

// A server that sends 20 at a time, whatever it's asked for: the next page
// starts after the 20, and a short page isn't the last.
void test_list_pages_smaller_than_asked_for()
{
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 200, .body = page_of(20, 1000) });
    fake.answer(ListPath, { .status = 200, .body = page_of(20, 1020) });
    fake.answer(ListPath, { .status = 200, .body = page_of(5, 1040) });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    auto const    activities = garmin.list_activities("2025-10-23");
    CHECK_EQ(activities.value_or(std::vector<GarminActivity> { }).size(), 45uz);
    CHECK_EQ(fake.requests.size(), 4uz);
    if (fake.requests.size() == 4) {
        CHECK(fake.requests[1].url.find("?start=20&") != std::string::npos);
        CHECK(fake.requests[3].url.find("?start=45&") != std::string::npos);
    }
}

void test_expired_token_while_listing()
{
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 401, .body = "" });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    auto const    activities = garmin.list_activities("2026-09-23");
    CHECK(!activities.has_value() && activities.error().kind == GarminError::Kind::KeyRejected);
}

// After the token, a 403 isn't about the key: it's an error, not a
// rejection.
void test_forbidden_list_is_not_a_rejected_key()
{
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 403, .body = "" });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    auto const    activities = garmin.list_activities("2026-09-23");
    CHECK(!activities.has_value() && activities.error().kind == GarminError::Kind::Failed);
}

// An activity Garmin Connect won't hand over is skipped; the others come.
void test_unavailable_activity_is_skipped()
{
    TempStore  store;
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 200, .body = list_of({ list_entry(6, 1792700000, "C"), list_entry(5, 1792600000, "B"), list_entry(4, 1792500000, "A") }) });
    fake.answer(download_path(6), { .status = 200, .body = fit_zip(6, "c") });
    fake.answer(download_path(5), { .status = 403, .body = "Forbidden" });
    fake.answer(download_path(4), { .status = 404, .body = "Not found" });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    CHECK_EQ(download_new_activities(garmin, store.storage, "2026-09-23").value_or(99), 1uz);
    CHECK_EQ(store.inbox(), (std::vector<std::string> { "garmin-6.fit", "garmin-6.json" }));
}

void test_download_unzips_the_fit_file()
{
    FakeGarmin fake;
    fake.answer(download_path(42), { .status = 200, .body = fit_zip(42, "the FIT data") });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    CHECK_EQ(garmin.download_fit(42).value_or(""), "the FIT data");
}

void test_download_without_a_fit_file()
{
    FakeGarmin fake;
    fake.answer(download_path(42), { .status = 200, .body = make_zip({ { .name = "42.gpx", .contents = "<gpx/>" } }) });
    fake.answer(download_path(43), { .status = 200, .body = "<html>Maintenance</html>" });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    CHECK(!garmin.download_fit(42).has_value());
    CHECK(!garmin.download_fit(43).has_value());
}

// Three activities on Garmin Connect: one the store has, one entered by
// hand, and one new one, which is the only one downloaded.
void test_download_new_activities()
{
    TempStore store;
    store.add_activity(1792430925);
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 200, .body = list_of({
                                               list_entry(3, 1792524471, "Kitchener Running"),
                                               list_entry(2, 1792500000, "Strength", true),
                                               list_entry(1, 1792430925, "Kitchener Running"),
                                           }) });
    fake.answer(download_path(3), { .status = 200, .body = fit_zip(3, "run") });
    GarminConnect                          garmin { .fetch = fake.fetch(), .token = "token" };
    std::vector<std::pair<size_t, size_t>> progress;
    auto const                             count = download_new_activities(garmin, store.storage, "2026-09-23",
        [&progress](size_t index, size_t count) { progress.emplace_back(index, count); });
    CHECK_EQ(count.value_or(99), 1uz);
    CHECK_EQ(store.inbox(), (std::vector<std::string> { "garmin-3.fit", "garmin-3.json" }));
    CHECK_EQ(read_file_by_name((store.storage.inbox_dir / "garmin-3.fit").string()).value_or(""), "run");
    auto const sidecar = JSONValue::read_file((store.storage.inbox_dir / "garmin-3.json").string());
    CHECK(sidecar.has_value());
    if (sidecar) {
        CHECK_EQ((*sidecar)["activityName"].to_string(), "Kitchener Running");
    }
    CHECK_EQ(progress.size(), 1uz);
    CHECK(progress == (std::vector<std::pair<size_t, size_t>> { { 0, 1 } }));
}

// Run again before the inbox is processed, the download doesn't fetch what
// the inbox already has.
void test_download_skips_what_the_inbox_has()
{
    TempStore store;
    MUST(write_file_by_name((store.storage.inbox_dir / "garmin-3.fit").string(), std::string_view { "run" }));
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 200, .body = list_of({ list_entry(3, 1792524471, "Run") }) });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    CHECK_EQ(download_new_activities(garmin, store.storage, "2026-09-23").value_or(99), 0uz);
    CHECK_EQ(downloads(fake), 0uz);
}

// A failed download stops the download, but what came before it stays.
void test_failed_download_keeps_the_earlier_ones()
{
    TempStore  store;
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 200, .body = list_of({ list_entry(5, 1792600000, "B"), list_entry(4, 1792500000, "A") }) });
    fake.answer(download_path(5), { .status = 200, .body = fit_zip(5, "b") });
    fake.answer(download_path(4), { .status = 503, .body = "Busy" });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    auto const    count = download_new_activities(garmin, store.storage, "2026-09-23");
    CHECK(!count.has_value());
    CHECK_EQ(store.inbox(), (std::vector<std::string> { "garmin-5.fit", "garmin-5.json" }));
}

void test_download_request()
{
    auto const settings = MUST_EVAL(JSONValue::deserialize(R"({"garmin": {"days": 90}})"));
    CHECK_EQ(make_download_request(JSONValue::object(), JSONValue { }).days, GarminDefaultDays);
    CHECK_EQ(make_download_request(settings, JSONValue { }).days, 90);
    auto const year = MUST_EVAL(JSONValue::deserialize(R"({"days": 365})"));
    CHECK_EQ(make_download_request(settings, year).days, 365);
    CHECK(!make_download_request(settings, year).quiet);
    auto const startup = MUST_EVAL(JSONValue::deserialize(R"({"quiet": true})"));
    CHECK(make_download_request(settings, startup).quiet);
    CHECK_EQ(make_download_request(settings, startup).days, 90);
}

// A key binding passes the key and its modifiers as the arguments: the
// settings still say how far back to look, and Ctrl+G isn't quiet.
void test_download_request_from_a_key_binding()
{
    auto const settings = MUST_EVAL(JSONValue::deserialize(R"({"garmin": {"days": 90}})"));
    auto const binding = MUST_EVAL(JSONValue::deserialize(R"({"key": 71, "modifier": 2})"));
    CHECK_EQ(make_download_request(settings, binding).days, 90);
    CHECK(!make_download_request(settings, binding).quiet);
}

// A downloaded activity goes through the inbox as any other: it's filed
// under the start time the list gave, so has_activity() finds it, its
// sidecar goes with it and gives it its title, and the next download
// doesn't fetch it again.
void test_downloaded_activity_goes_through_the_inbox()
{
    // October 31, 2021, 22:46 in Toronto, which test_start_date() set as the
    // time zone, and November 1 in UTC: the inbox and has_activity() have to
    // agree on the month.
    constexpr i64 StartTime = 1635734760;
    FITBuilder    fit;
    add_workout(fit, "Tempo intervals");
    fit.definition(0, 18,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 2, .size = 4, .base_type = Uint32 },
            { .num = 5, .size = 1, .base_type = Enum },
            { .num = 7, .size = 4, .base_type = Uint32 },
            { .num = 8, .size = 4, .base_type = Uint32 },
        });
    fit.message(0);
    auto const fit_start = static_cast<u32>(StartTime - DateTime::FIT_TIMESTAMP_OFFSET);
    fit.u32_le(fit_start + 3600);
    fit.u32_le(fit_start);
    fit.byte(1); // Running
    fit.u32_le(3600 * 1000);
    fit.u32_le(3600 * 1000);

    TempStore  store;
    FakeGarmin fake;
    fake.answer(ListPath, { .status = 200, .body = list_of({ list_entry(7, StartTime, "Westmount-Wallenstein") }) });
    fake.answer(download_path(7), { .status = 200, .body = fit_zip(7, fit.file()) });
    GarminConnect garmin { .fetch = fake.fetch(), .token = "token" };
    CHECK_EQ(download_new_activities(garmin, store.storage, "2021-10-01").value_or(99), 1uz);

    CHECK_EQ(store.storage.process_inbox().value_or(99), 1u);
    CHECK(store.inbox().empty());
    CHECK(store.storage.has_activity(StartTime));

    auto const newest = store.storage.find_newest();
    CHECK(newest.has_value() && newest->has_value());
    if (newest && *newest) {
        CHECK_EQ((*newest)->title, "Westmount-Wallenstein");
    }

    // Garmin Connect still lists it, but it isn't downloaded again.
    fake.answer(ListPath, { .status = 200, .body = list_of({ list_entry(7, StartTime, "Westmount-Wallenstein") }) });
    fake.answer(download_path(7), { .status = 200, .body = fit_zip(7, fit.file()) });
    CHECK_EQ(download_new_activities(garmin, store.storage, "2021-10-01").value_or(99), 0uz);
    CHECK_EQ(downloads(fake), 1uz);
}

void test_has_activity()
{
    TempStore store;
    CHECK(!store.storage.has_activity(1792498171));
    store.add_activity(1792498171);
    CHECK(store.storage.has_activity(1792498171));
    CHECK(!store.storage.has_activity(1792498172));
}

}

int main()
{
    test_key_from_the_environment_or_the_file();
    test_parse_activity_list();
    test_odd_entries_are_skipped();
    test_start_date();
    test_token();
    test_rejected_key();
    test_offline_and_server_errors();
    test_list_pages();
    test_list_pages_smaller_than_asked_for();
    test_expired_token_while_listing();
    test_forbidden_list_is_not_a_rejected_key();
    test_unavailable_activity_is_skipped();
    test_download_unzips_the_fit_file();
    test_download_without_a_fit_file();
    test_download_new_activities();
    test_download_skips_what_the_inbox_has();
    test_failed_download_keeps_the_earlier_ones();
    test_download_request();
    test_has_activity();
    test_download_request_from_a_key_binding();
    test_downloaded_activity_goes_through_the_inbox();
    return ST::Test::test_result();
}
