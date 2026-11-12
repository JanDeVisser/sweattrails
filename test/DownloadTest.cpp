/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests what the download tasks ask for, and what the download tells the
 * user when it's done.
 */

#include <cstdlib>
#include <ctime>
#include <format>

#include <JSON.h>

#include <storage/Download.h>
#include <storage/Garmin.h>
#include <storage/Pacing.h>
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

SourceOutcome strava(Kind kind, size_t titles = 0, size_t waiting = 0)
{
    return SourceOutcome { .source = "Strava", .kind = kind, .count = titles, .waiting = waiting };
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

// Strava's descriptions come in the background unless the settings say
// false.
void test_strava_descriptions_setting()
{
    auto const off = MUST_EVAL(JSONValue::deserialize(R"({"strava": {"client_id": "1", "descriptions": false}})"));
    auto const on = MUST_EVAL(JSONValue::deserialize(R"({"strava": {"client_id": "1", "descriptions": true}})"));
    auto const unsaid = MUST_EVAL(JSONValue::deserialize(R"({"strava": {"client_id": "1"}})"));
    CHECK(!strava_descriptions(off));
    CHECK(strava_descriptions(on));
    CHECK(strava_descriptions(unsaid));
    CHECK(strava_descriptions(JSONValue::object()));
    CHECK(strava_descriptions(JSONValue { }));
    CHECK(!make_download_request(off, JSONValue { }, Sources { .strava = true }).strava_descriptions);
    CHECK(make_download_request(unsaid, JSONValue { }, Sources { .strava = true }).strava_descriptions);
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
    CHECK_EQ(download_report({ strava(Kind::Titled, 3) }, false).text, "Strava: 3 new titles.");
    CHECK_EQ(download_report({ strava(Kind::Titled, 1) }, false).text, "Strava: 1 new title.");
    CHECK_EQ(download_report({ strava(Kind::Titled) }, false).text, "Strava: nothing new.");
    CHECK_EQ(download_report({ strava(Kind::Titled, 154, 4644) }, false).text, "Strava: 154 new titles. 4644 descriptions follow in the background.");
    CHECK_EQ(download_report({ strava(Kind::Titled, 0, 1) }, false).text, "Strava: nothing new. 1 description follows in the background.");
    auto stopped = strava(Kind::Titled, 40, 12);
    stopped.message = "Strava's list stopped short: Could not reach Strava: timed out";
    CHECK_EQ(download_report({ stopped }, false).text,
        "Strava: 40 new titles. 12 descriptions follow in the background. Strava's list stopped short: Could not reach Strava: timed out.");
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
    CHECK(download_report({ strava(Kind::NotSetUp) }, false).ask_login.empty());
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

void test_downloads_activities()
{
    auto settings = JSONValue::object();
    CHECK(!downloads_activities(settings, false));
    CHECK(downloads_activities(settings, true));
    CHECK(!downloads_activities(JSONValue { }, false));
    // Strava brings in titles, not activities.
    settings = MUST_EVAL(JSONValue::deserialize(R"({"strava": {"client_id": "12345", "client_secret": "s3cr3t"}})"));
    CHECK(!downloads_activities(settings, false));
    settings["wahoo"] = MUST_EVAL(JSONValue::deserialize(R"({"client_id": "abc123"})"));
    CHECK(downloads_activities(settings, false));
    CHECK(downloads_activities(settings, true));
}

// The local date `days` days before `now`, as Garmin's start date is
// worked out (start_date(), which this test doesn't link).
std::string start_date(int days, i64 now)
{
    auto const start = static_cast<time_t>(now - static_cast<i64>(days) * 24 * 60 * 60);
    std::tm    tm { };
    localtime_r(&start, &tm);
    return std::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

// The days of "Everything", for a download at `now`.
int everything(i64 now)
{
    auto const periods = download_periods(now);
    return periods[3].days;
}

// The days of "This year", for a download at `now`.
int this_year(i64 now)
{
    auto const periods = download_periods(now);
    return periods[1].days;
}

void test_download_periods()
{
    setenv("TZ", "America/Toronto", 1);
    tzset();
    // November 11, 2026, 08:00 in Toronto.
    i64 const  now = 1794402000;
    auto const periods = download_periods(now);
    CHECK_EQ(periods.size(), 4uz);
    CHECK_EQ(periods[0].text, "The last 30 days");
    CHECK_EQ(periods[0].days, 30);
    CHECK_EQ(periods[1].text, "This year");
    CHECK_EQ(periods[2].text, "The last year (365 days)");
    CHECK_EQ(periods[2].days, 365);
    CHECK_EQ(periods[3].text, "Everything");
    // From December 31, 1989: a day early, as for "This year".
    CHECK_EQ(periods[3].days, 13464);
    CHECK_EQ(start_date(periods[3].days, now), "1989-12-31");

    // This year reaches all of January 1, whatever the time of day: Wahoo
    // keeps it, Garmin asks from December 31.
    i64 const new_years_morning = 1767268800; // January 1, 2026, 07:00
    CHECK_EQ(this_year(new_years_morning), 1);
    CHECK_EQ(start_date(this_year(new_years_morning), new_years_morning), "2025-12-31");
    i64 const ides_of_march = 1773583200; // March 15, 2026, 10:00, after the change to summer time
    CHECK_EQ(this_year(ides_of_march), 74);
    CHECK_EQ(start_date(this_year(ides_of_march), ides_of_march), "2025-12-31");
    i64 const new_years_eve = 1798777800; // December 31, 2026, 23:30, already 2027 in UTC
    CHECK_EQ(this_year(new_years_eve), 365);
    CHECK_EQ(start_date(this_year(new_years_eve), new_years_eve), "2025-12-31");
}

// The login offered after a download fetches as far back as the download.
void test_login_arguments()
{
    DownloadRequest const request { .garmin = true, .wahoo = true, .strava = true, .garmin_days = 13464, .wahoo_days = 13464, .strava_days = 365 };
    CHECK_EQ(login_arguments(request, "Wahoo").try_get<int>("days").value_or(0), 13464);
    CHECK_EQ(login_arguments(request, "Strava").try_get<int>("days").value_or(0), 365);
    CHECK(!login_arguments(request, "Garmin Connect").try_get<int>("days").has_value());
}

// Around the changes to and from summer time, and in a leap year, a day
// counts once: "This year" and "Everything" are the same all day.
void test_download_periods_at_daylight_saving_changes()
{
    setenv("TZ", "America/Toronto", 1);
    tzset();
    // March 8, 2026: 00:30 before the change, 03:30 after it.
    for (i64 const now : { 1772947800L, 1772955000L }) {
        CHECK_EQ(this_year(now), 67);
        CHECK_EQ(everything(now), 13464 - 248);
    }
    // November 1, 2026: 00:30 before the change back, 23:30 after it.
    for (i64 const now : { 1793507400L, 1793593800L }) {
        CHECK_EQ(this_year(now), 305);
        CHECK_EQ(everything(now), 13464 - 10);
    }
    // December 31, 2024, 23:30: the 366th day of a leap year.
    i64 const leap_new_years_eve = 1735705800;
    CHECK_EQ(this_year(leap_new_years_eve), 366);
    CHECK_EQ(start_date(366, leap_new_years_eve), "2023-12-31");

    // East of Greenwich, where it is still yesterday in UTC: November 11,
    // 2026, 00:30 in Auckland, is the same day as in Toronto.
    setenv("TZ", "Pacific/Auckland", 1);
    tzset();
    CHECK_EQ(everything(1794310200), 13464);
    CHECK_EQ(this_year(1794310200), 315);
}

// The login's days make it back into a download request.
void test_login_arguments_make_a_request()
{
    DownloadRequest const request { .wahoo = true, .strava = true, .wahoo_days = 400, .strava_days = 500 };
    auto const            wahoo = make_download_request(JSONValue::object(), login_arguments(request, "Wahoo"), Sources { .wahoo = true });
    CHECK_EQ(wahoo.wahoo_days, 400);
    CHECK(!wahoo.quiet);
    auto const strava = make_download_request(JSONValue::object(), login_arguments(request, "Strava"), Sources { .strava = true });
    CHECK_EQ(strava.strava_days, 500);
}

// A pause before each batch after the first; none with no batches.
void test_pause_before()
{
    Pacing const pacing { .batch = 25 };
    CHECK(!pause_before(pacing, 0));
    CHECK(!pause_before(pacing, 24));
    CHECK(pause_before(pacing, 25));
    CHECK(!pause_before(pacing, 26));
    CHECK(pause_before(pacing, 50));
    CHECK(!pause_before(Pacing { .batch = 0 }, 25));
}

void test_download_status()
{
    CHECK_EQ(download_status(DownloadStep::Starting), "Starting the download…");
    CHECK_EQ(download_status(DownloadStep::Listing, "Garmin Connect"), "Asking Garmin Connect what there is to download…");
    CHECK_EQ(download_status(DownloadStep::Downloading, "Wahoo", 0, 2), "Downloading 1 of 2 from Wahoo");
    CHECK_EQ(download_status(DownloadStep::Pausing, "Garmin Connect"), "Pausing a minute between downloads from Garmin Connect");
    CHECK_EQ(download_status(DownloadStep::Retrying, "Strava"), "Strava didn't answer: asking again shortly");
    CHECK_EQ(download_status(DownloadStep::Waiting, "Wahoo", 0, 0, "20:04"), "Waiting for Wahoo's limit until 20:04");
    CHECK_EQ(describing_status(4644), "Strava descriptions: 4644 to go");
}

int main()
{
    test_client_id();
    test_strava_client();
    test_download_request();
    test_strava_download_request();
    test_strava_descriptions_setting();
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
    test_downloads_activities();
    test_download_periods();
    test_login_arguments();
    test_pause_before();
    test_download_status();
    test_download_periods_at_daylight_saving_changes();
    test_login_arguments_make_a_request();
    return ST::Test::test_result();
}
