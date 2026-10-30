/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests what the download tasks ask for, and what the download tells the
 * user when it's done.
 */

#include <JSON.h>

#include <storage/Download.h>
#include <storage/Garmin.h>
#include <storage/Strava.h>
#include <storage/Wahoo.h>

#include <TestApp.h>

using namespace ST;

namespace {

using Kind = SourceOutcome::Kind;

SourceOutcome garmin(Kind kind, size_t count = 0, std::string message = { })
{
    return SourceOutcome { .source = "Garmin Connect", .kind = kind, .count = count, .message = std::move(message) };
}

SourceOutcome wahoo(Kind kind, size_t count = 0, std::string message = { })
{
    return SourceOutcome { .source = "Wahoo", .kind = kind, .count = count, .message = std::move(message) };
}

SourceOutcome strava(Kind kind, size_t titles = 0, size_t descriptions = 0, size_t waiting = 0, std::string resets_at = { })
{
    return SourceOutcome {
        .source = "Strava",
        .kind = kind,
        .count = titles,
        .descriptions = descriptions,
        .waiting = waiting,
        .resets_at = std::move(resets_at),
    };
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

void test_strava_client()
{
    CHECK(!strava_client(JSONValue { }).has_value());
    CHECK(strava_setup_problem(std::nullopt).value_or("").starts_with("Strava isn't set up."));
    auto settings = MUST_EVAL(JSONValue::deserialize(R"({"strava": {"client_secret": "s3cr3t"}})"));
    CHECK(!strava_client(settings).has_value());
    // An ID without the secret: the user uses Strava, and hears what's
    // missing.
    settings = MUST_EVAL(JSONValue::deserialize(R"({"strava": {"client_id": " 12345 "}})"));
    CHECK(strava_client(settings).has_value());
    CHECK(strava_setup_problem(strava_client(settings)).value_or("").starts_with("Strava's client secret is missing."));
    settings["strava"]["client_secret"] = JSONValue { " " };
    CHECK(strava_setup_problem(strava_client(settings)).has_value());
    settings["strava"]["client_secret"] = JSONValue { "s3cr3t" };
    auto const client = strava_client(settings);
    CHECK(client.has_value());
    CHECK_EQ(client->id, "12345");
    CHECK_EQ(client->secret, "s3cr3t");
    CHECK(!strava_setup_problem(client).has_value());
}

void test_download_request()
{
    auto const settings = MUST_EVAL(JSONValue::deserialize(R"({"garmin": {"days": 90}, "wahoo": {"days": 7, "client_id": "abc"}})"));
    auto const defaults = make_download_request(JSONValue::object(), JSONValue { }, Sources { .garmin = true, .wahoo = true });
    CHECK_EQ(defaults.garmin_days, GarminDefaultDays);
    CHECK_EQ(defaults.wahoo_days, WahooDefaultDays);
    CHECK(!defaults.wahoo_client_id.has_value());
    CHECK(!defaults.quiet);

    auto const from_settings = make_download_request(settings, JSONValue { }, Sources { .garmin = true });
    CHECK(from_settings.garmin && !from_settings.wahoo);
    CHECK_EQ(from_settings.garmin_days, 90);
    CHECK_EQ(from_settings.wahoo_days, 7);
    CHECK_EQ(from_settings.wahoo_client_id.value_or(""), "abc");

    // The task's days are for every source.
    auto const year = make_download_request(settings, MUST_EVAL(JSONValue::deserialize(R"({"days": 365})")), Sources { .wahoo = true });
    CHECK(!year.garmin && year.wahoo);
    CHECK_EQ(year.garmin_days, 365);
    CHECK_EQ(year.wahoo_days, 365);
    CHECK(!year.quiet);

    auto const startup = make_download_request(settings, MUST_EVAL(JSONValue::deserialize(R"({"quiet": true})")), Sources { .garmin = true, .wahoo = true });
    CHECK(startup.quiet);
    CHECK_EQ(startup.garmin_days, 90);
}

void test_strava_download_request()
{
    auto const settings = MUST_EVAL(JSONValue::deserialize(R"({"strava": {"days": 14, "client_id": "1", "client_secret": "2"}})"));
    auto const request = make_download_request(settings, JSONValue { }, Sources { .garmin = true, .wahoo = true, .strava = true });
    CHECK(request.strava);
    CHECK_EQ(request.strava_days, 14);
    CHECK(request.strava_client.has_value());
    CHECK_EQ(make_download_request(JSONValue::object(), JSONValue { }, Sources { .strava = true }).strava_days, StravaDefaultDays);
    auto const year = make_download_request(settings, MUST_EVAL(JSONValue::deserialize(R"({"days": 365})")), Sources { .strava = true });
    CHECK_EQ(year.strava_days, 365);
    CHECK(!year.garmin && !year.wahoo);
}

// A key binding passes the key and its modifiers as the arguments: the
// settings still say how far back to look, and Ctrl+G isn't quiet.
void test_download_request_from_a_key_binding()
{
    auto const settings = MUST_EVAL(JSONValue::deserialize(R"({"garmin": {"days": 90}})"));
    auto const binding = MUST_EVAL(JSONValue::deserialize(R"({"key": 71, "modifier": 2})"));
    auto const request = make_download_request(settings, binding, Sources { .garmin = true, .wahoo = true });
    CHECK_EQ(request.garmin_days, 90);
    CHECK(!request.quiet);
}

void test_report()
{
    auto const report = download_report({ garmin(Kind::Downloaded, 2), wahoo(Kind::Downloaded, 0) }, false);
    CHECK_EQ(report.text, "Garmin Connect: 2 new activities. Wahoo: nothing new.");
    CHECK(report.ask_login.empty());
    CHECK_EQ(download_report({ garmin(Kind::Downloaded, 1) }, false).text, "Garmin Connect: 1 new activity.");
    CHECK_EQ(download_report({ wahoo(Kind::Downloaded, 0) }, false).text, "Wahoo: nothing new.");
}

void test_report_failures()
{
    auto const report = download_report({ garmin(Kind::Failed, 0, "Could not reach Garmin Connect: offline"), wahoo(Kind::Downloaded, 3) }, false);
    CHECK_EQ(report.text, "Could not reach Garmin Connect: offline Wahoo: 3 new activities.");
}

void test_report_leaves_out_sources_not_set_up()
{
    // Someone who doesn't use Wahoo never hears of it.
    auto const report = download_report({ garmin(Kind::Downloaded, 1), wahoo(Kind::NotSetUp, 0, "Wahoo isn't set up.") }, false);
    CHECK_EQ(report.text, "Garmin Connect: 1 new activity.");
    // Unless it was asked for by itself.
    CHECK_EQ(download_report({ wahoo(Kind::NotSetUp, 0, "Wahoo isn't set up.") }, false).text, "Wahoo isn't set up.");
    // Nothing set up at all.
    auto const nothing = download_report({ garmin(Kind::NotSetUp, 0, "No key."), wahoo(Kind::NotSetUp, 0, "No client ID.") }, false);
    CHECK_EQ(nothing.text, "No key.");
}

void test_report_offers_the_login()
{
    auto const report = download_report({ garmin(Kind::Downloaded, 2), wahoo(Kind::NeedsLogin) }, false);
    CHECK_EQ(report.ask_login, "Wahoo");
    CHECK_EQ(report.text, "Garmin Connect: 2 new activities. Wahoo: not logged in.");
}

// Each source that's set up gets its sentence, whatever the other did.
void test_report_combinations()
{
    auto const only_wahoo = download_report({ garmin(Kind::NotSetUp, 0, "No key."), wahoo(Kind::NeedsLogin) }, false);
    CHECK_EQ(only_wahoo.text, "Wahoo: not logged in.");
    CHECK_EQ(only_wahoo.ask_login, "Wahoo");

    auto const both_wrong = download_report({ garmin(Kind::Failed, 0, "Could not reach Garmin Connect."), wahoo(Kind::NeedsLogin) }, false);
    CHECK_EQ(both_wrong.text, "Could not reach Garmin Connect. Wahoo: not logged in.");
    CHECK_EQ(both_wrong.ask_login, "Wahoo");

    CHECK_EQ(download_report({ garmin(Kind::NotSetUp, 0, "No key."), wahoo(Kind::Failed, 0, "Could not reach Wahoo.") }, false).text, "Could not reach Wahoo.");
    CHECK_EQ(download_report({ garmin(Kind::NotSetUp, 0, "No key."), wahoo(Kind::Downloaded, 0) }, false).text, "Wahoo: nothing new.");
    CHECK_EQ(download_report({ garmin(Kind::Downloaded, 0), wahoo(Kind::NotSetUp, 0, "No ID.") }, false).text, "Garmin Connect: nothing new.");
    CHECK(download_report({ garmin(Kind::Downloaded, 0), wahoo(Kind::Downloaded, 0) }, false).ask_login.empty());
}

void test_strava_report()
{
    CHECK_EQ(download_report({ strava(Kind::Titled, 3, 3) }, false).text, "Strava: 3 new titles.");
    CHECK_EQ(download_report({ strava(Kind::Titled, 1) }, false).text, "Strava: 1 new title.");
    CHECK_EQ(download_report({ strava(Kind::Titled) }, false).text, "Strava: nothing new.");
    CHECK_EQ(download_report({ strava(Kind::Titled, 0, 4) }, false).text, "Strava: 4 new descriptions.");
    CHECK_EQ(download_report({ strava(Kind::Titled, 40, 28, 12, "14:45") }, false).text,
        "Strava: 40 new titles. 12 descriptions are left for after 14:45.");
    CHECK_EQ(download_report({ strava(Kind::Titled, 0, 0, 1) }, false).text, "Strava: nothing new. 1 description is left for next time.");
    auto stopped = strava(Kind::Titled, 40, 0, 12);
    stopped.message = "Could not reach Strava: timed out";
    CHECK_EQ(download_report({ stopped }, false).text,
        "Strava: 40 new titles. 12 descriptions are left for next time. Could not reach Strava: timed out.");
    auto const all = download_report({ garmin(Kind::Downloaded, 2), wahoo(Kind::NotSetUp, 0, "No ID."), strava(Kind::Titled, 2) }, false);
    CHECK_EQ(all.text, "Garmin Connect: 2 new activities. Strava: 2 new titles.");
    CHECK(download_report({ strava(Kind::Titled, 2) }, true).text.empty());
}

// The question names the source to log in to: the first, of two.
void test_strava_report_offers_the_login()
{
    auto const report = download_report({ garmin(Kind::Downloaded, 0), strava(Kind::NeedsLogin) }, false);
    CHECK_EQ(report.ask_login, "Strava");
    CHECK_EQ(report.text, "Garmin Connect: nothing new. Strava: not logged in.");
    CHECK_EQ(download_report({ wahoo(Kind::NeedsLogin), strava(Kind::NeedsLogin) }, false).ask_login, "Wahoo");
    CHECK(download_report({ strava(Kind::NotSetUp, 0, 0, 0) }, false).ask_login.empty());
}

void test_quiet_report_of_one_source()
{
    CHECK(download_report({ wahoo(Kind::NotSetUp, 0, "No ID.") }, true).text.empty());
    auto const needs_login = download_report({ wahoo(Kind::NeedsLogin) }, true);
    CHECK(needs_login.text.empty());
    CHECK(needs_login.ask_login.empty());
    CHECK_EQ(download_report({ garmin(Kind::Failed, 0, "Could not reach Garmin Connect.") }, true).text, "Could not reach Garmin Connect.");
}

void test_quiet_report()
{
    // At startup, new activities show up by themselves, and the login isn't
    // offered: only failures are worth a word.
    auto const quiet = download_report({ garmin(Kind::Downloaded, 2), wahoo(Kind::NeedsLogin) }, true);
    CHECK(quiet.text.empty());
    CHECK(quiet.ask_login.empty());
    CHECK(download_report({ garmin(Kind::NotSetUp, 0, "No key."), wahoo(Kind::NotSetUp, 0, "No ID.") }, true).text.empty());
    CHECK_EQ(download_report({ garmin(Kind::Downloaded, 2), wahoo(Kind::Failed, 0, "Could not reach Wahoo.") }, true).text, "Could not reach Wahoo.");
}

}

int main()
{
    test_client_id();
    test_strava_client();
    test_download_request();
    test_strava_download_request();
    test_download_request_from_a_key_binding();
    test_report();
    test_report_failures();
    test_report_leaves_out_sources_not_set_up();
    test_report_offers_the_login();
    test_report_combinations();
    test_strava_report();
    test_strava_report_offers_the_login();
    test_quiet_report_of_one_source();
    test_quiet_report();
    return ST::Test::test_result();
}
