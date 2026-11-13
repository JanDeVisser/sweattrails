/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>

#include <StringUtil.h>

#include <storage/Download.h>
#include <storage/Garmin.h>
#include <storage/Strava.h>
#include <storage/Wahoo.h>

namespace ST {

namespace {

int days_setting(JSONValue const &settings, std::string_view source, int days)
{
    if (!settings.is_object()) {
        return days;
    }
    auto const section = settings.get(source);
    if (!section || !section->is_object()) {
        return days;
    }
    return section->try_get<int>("days").value_or(days);
}

std::string plural(size_t count, std::string_view one, std::string_view more)
{
    return std::format("{} {}", count, (count == 1) ? one : more);
}

// The setting `key` in `section`, stripped, if it isn't empty.
std::optional<std::string> string_setting(JSONValue const &settings, std::string_view section, std::string_view key)
{
    if (!settings.is_object()) {
        return { };
    }
    auto const values = settings.get(section);
    if (!values || !values->is_object()) {
        return { };
    }
    auto const value = values->try_get<std::string>(key);
    if (!value || strip(*value).empty()) {
        return { };
    }
    return std::string { strip(*value) };
}

std::string titled_sentence(SourceOutcome const &outcome)
{
    std::string ret;
    if (outcome.count > 0) {
        ret = std::format("{}: {}.", outcome.source, plural(outcome.count, "new title", "new titles"));
    } else {
        ret = std::format("{}: nothing new.", outcome.source);
    }
    if (outcome.waiting > 0) {
        ret += std::format(" {} in the background.", plural(outcome.waiting, "description follows", "descriptions follow"));
    }
    if (!outcome.message.empty()) {
        ret += std::format(" {}{}", outcome.message, outcome.message.ends_with('.') ? "" : ".");
    }
    return ret;
}

std::string sentence(SourceOutcome const &outcome)
{
    switch (outcome.kind) {
    case SourceOutcome::Kind::Titled:
        return titled_sentence(outcome);
    case SourceOutcome::Kind::Downloaded:
        if (outcome.count == 0) {
            return std::format("{}: nothing new.", outcome.source);
        }
        return std::format("{}: {}.", outcome.source, plural(outcome.count, "new activity", "new activities"));
    case SourceOutcome::Kind::NeedsLogin:
        return std::format("{}: not logged in.", outcome.source);
    case SourceOutcome::Kind::NotSetUp:
    case SourceOutcome::Kind::Failed:
        return outcome.message;
    }
    return { };
}

}

std::optional<std::string> wahoo_client_id(JSONValue const &settings)
{
    return string_setting(settings, "wahoo", "client_id");
}

std::optional<StravaClient> strava_client(JSONValue const &settings)
{
    auto id = string_setting(settings, "strava", "client_id");
    if (!id) {
        return { };
    }
    return StravaClient { .id = std::move(*id), .secret = string_setting(settings, "strava", "client_secret").value_or("") };
}

bool strava_descriptions(JSONValue const &settings)
{
    if (!settings.is_object()) {
        return true;
    }
    auto const section = settings.get("strava");
    if (!section || !section->is_object()) {
        return true;
    }
    return section->try_get<bool>("descriptions").value_or(true);
}

std::optional<std::string> strava_setup_problem(std::optional<StravaClient> const &client)
{
    if (!client) {
        return "Strava isn't set up. Register an app with Strava, and put its client ID and secret in the settings, as "
               "\"strava\": { \"client_id\": \"...\", \"client_secret\": \"...\" }. The README says how.";
    }
    if (client->secret.empty()) {
        return "Strava's client secret is missing. Put it in the settings with the client ID, as "
               "\"strava\": { \"client_id\": \"...\", \"client_secret\": \"...\" }.";
    }
    return { };
}

std::string download_status(DownloadStep step, std::string_view source, size_t index, size_t count, std::string_view until)
{
    switch (step) {
    case DownloadStep::Starting:
        return "Starting the download…";
    case DownloadStep::Listing:
        return std::format("Asking {} what there is to download…", source);
    case DownloadStep::Downloading:
        return std::format("Downloading {} of {} from {}", index + 1, count, source);
    case DownloadStep::Pausing:
        return std::format("Pausing a minute between downloads from {}", source);
    case DownloadStep::Retrying:
        return std::format("{} didn't answer: asking again shortly", source);
    case DownloadStep::Waiting:
        return std::format("Waiting for {}'s limit until {}", source, until);
    }
    return { };
}

std::string describing_status(size_t to_go)
{
    return std::format("Strava descriptions: {} to go", to_go);
}

bool downloads_activities(JSONValue const &settings, bool has_garmin_key)
{
    return has_garmin_key || wahoo_client_id(settings).has_value();
}

std::vector<DownloadPeriod> download_periods(i64 now)
{
    auto const today = static_cast<time_t>(now);
    std::tm    tm { };
    localtime_r(&today, &tm);
    // The downloads look back whole days from now, keeping the time of
    // day, so a day more than the day of the year reaches all of January
    // 1, and Garmin's start date, a date, is December 31.
    auto const this_year = tm.tm_yday + 1;
    // Days to January 1 of EverythingSinceYear, counted in whole days at
    // noon so that a change to or from daylight saving time doesn't matter.
    std::tm since { .tm_hour = 12, .tm_mday = 1, .tm_mon = 0, .tm_year = EverythingSinceYear - 1900, .tm_isdst = -1 };
    std::tm noon = tm;
    noon.tm_hour = 12;
    noon.tm_min = 0;
    noon.tm_sec = 0;
    noon.tm_isdst = -1;
    auto const everything = static_cast<int>(std::lround(std::difftime(std::mktime(&noon), std::mktime(&since)) / (24 * 60 * 60))) + 1;
    return {
        DownloadPeriod { .text = "The last 30 days", .days = 30 },
        DownloadPeriod { .text = "This year", .days = this_year },
        DownloadPeriod { .text = "The last year (365 days)", .days = 365 },
        DownloadPeriod { .text = "Everything", .days = everything },
    };
}

DownloadRequest make_download_request(JSONValue const &settings, JSONValue const &arguments, Sources sources)
{
    DownloadRequest request {
        .garmin = sources.garmin,
        .wahoo = sources.wahoo,
        .strava = sources.strava,
        .garmin_days = days_setting(settings, "garmin", GarminDefaultDays),
        .wahoo_days = days_setting(settings, "wahoo", WahooDefaultDays),
        .wahoo_overlap_days = days_setting(settings, "wahoo", WahooDefaultDays),
        .strava_days = days_setting(settings, "strava", StravaDefaultDays),
        .wahoo_client_id = wahoo_client_id(settings),
        .strava_client = strava_client(settings),
        .strava_descriptions = strava_descriptions(settings),
    };
    if (arguments.is_object()) {
        if (auto const days = arguments.try_get<int>("days"); days) {
            request.garmin_days = *days;
            request.wahoo_days = *days;
            request.strava_days = *days;
        }
        request.quiet = arguments.try_get<bool>("quiet").value_or(false);
    }
    return request;
}

DownloadReport download_report(std::vector<SourceOutcome> const &outcomes, bool quiet)
{
    auto const     set_up = std::ranges::count_if(outcomes, [](SourceOutcome const &outcome) {
        return outcome.kind != SourceOutcome::Kind::NotSetUp;
    });
    DownloadReport report;
    for (auto const &outcome : outcomes) {
        if (quiet && outcome.kind != SourceOutcome::Kind::Failed) {
            continue;
        }
        if (outcome.kind == SourceOutcome::Kind::NotSetUp && outcomes.size() > 1) {
            continue;
        }
        if (outcome.kind == SourceOutcome::Kind::NeedsLogin && report.ask_login.empty()) {
            report.ask_login = outcome.source;
        }
        if (quiet && outcome.kind == SourceOutcome::Kind::Failed && outcome.limited) {
            report.status += std::format("{}{}", report.status.empty() ? "" : " ", sentence(outcome));
            continue;
        }
        if (!report.text.empty()) {
            report.text += ' ';
        }
        report.text += sentence(outcome);
    }
    // Asked for every source, and none is set up: how to set up the first,
    // Garmin Connect, as before there was a second. A user who doesn't use
    // Wahoo never hears of it.
    if (!quiet && outcomes.size() > 1 && set_up == 0) {
        report.text = outcomes.front().message;
    }
    return report;
}

JSONValue login_arguments(DownloadRequest const &request, std::string_view source)
{
    auto ret = JSONValue::object();
    if (source == "Wahoo") {
        ret.set("days", JSONValue { request.wahoo_days });
    } else if (source == "Strava") {
        ret.set("days", JSONValue { request.strava_days });
    }
    return ret;
}

}
