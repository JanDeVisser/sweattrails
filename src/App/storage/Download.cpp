/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
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
    } else if (outcome.descriptions > 0) {
        ret = std::format("{}: {}.", outcome.source, plural(outcome.descriptions, "new description", "new descriptions"));
    } else {
        ret = std::format("{}: nothing new.", outcome.source);
    }
    if (outcome.waiting > 0) {
        ret += std::format(" {} left for {}.", plural(outcome.waiting, "description is", "descriptions are"),
            outcome.resets_at.empty() ? std::string { "next time" } : std::format("after {}", outcome.resets_at));
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

DownloadRequest make_download_request(JSONValue const &settings, JSONValue const &arguments, Sources sources)
{
    DownloadRequest request {
        .garmin = sources.garmin,
        .wahoo = sources.wahoo,
        .strava = sources.strava,
        .garmin_days = days_setting(settings, "garmin", GarminDefaultDays),
        .wahoo_days = days_setting(settings, "wahoo", WahooDefaultDays),
        .strava_days = days_setting(settings, "strava", StravaDefaultDays),
        .wahoo_client_id = wahoo_client_id(settings),
        .strava_client = strava_client(settings),
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

}
