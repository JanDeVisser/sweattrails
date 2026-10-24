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

std::string sentence(SourceOutcome const &outcome)
{
    switch (outcome.kind) {
    case SourceOutcome::Kind::Downloaded:
        if (outcome.count == 0) {
            return std::format("{}: nothing new.", outcome.source);
        }
        return std::format("{}: {} new {}.", outcome.source, outcome.count, (outcome.count == 1) ? "activity" : "activities");
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
    if (!settings.is_object()) {
        return { };
    }
    auto const wahoo = settings.get("wahoo");
    if (!wahoo || !wahoo->is_object()) {
        return { };
    }
    auto const client_id = wahoo->try_get<std::string>("client_id");
    if (!client_id || strip(*client_id).empty()) {
        return { };
    }
    return std::string { strip(*client_id) };
}

DownloadRequest make_download_request(JSONValue const &settings, JSONValue const &arguments, bool garmin, bool wahoo)
{
    DownloadRequest request {
        .garmin = garmin,
        .wahoo = wahoo,
        .garmin_days = days_setting(settings, "garmin", GarminDefaultDays),
        .wahoo_days = days_setting(settings, "wahoo", WahooDefaultDays),
        .wahoo_client_id = wahoo_client_id(settings),
    };
    if (arguments.is_object()) {
        if (auto const days = arguments.try_get<int>("days"); days) {
            request.garmin_days = *days;
            request.wahoo_days = *days;
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
        if (outcome.kind == SourceOutcome::Kind::NeedsLogin) {
            report.ask_login = true;
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
