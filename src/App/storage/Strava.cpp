/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Date.h>
#include <StringUtil.h>

#include <storage/Strava.h>

namespace ST {

namespace {

constexpr std::string_view AuthorizePath = "/oauth/authorize";
constexpr std::string_view TokenPath = "/oauth/token";
constexpr std::string_view ActivitiesPath = "/api/v3/athlete/activities";
constexpr std::string_view ActivityPath = "/api/v3/activities";

// Strava's largest page.
constexpr size_t PageSize = 200;

// An access token this close to expiring is refreshed first, so that it
// doesn't expire halfway through.
constexpr i64 RefreshMargin = 60;

constexpr i64 QuarterHour = 15 * 60;
constexpr i64 Day = 24 * 60 * 60;

StravaError failed(std::string message)
{
    return StravaError { .kind = StravaError::Kind::Failed, .message = std::move(message) };
}

StravaError not_logged_in()
{
    return StravaError {
        .kind = StravaError::Kind::NotLoggedIn,
        .message = "SweatTrails isn't logged in to Strava, or Strava no longer accepts the login. Log in to Strava again.",
    };
}

StravaError unreachable(LibCError const &error)
{
    return failed(std::format("Could not reach Strava: {}", error.to_string()));
}

StravaError http_error(HttpResponse const &response, std::string_view what, i64 now)
{
    if (response.status == 429) {
        return StravaError {
            .kind = StravaError::Kind::RateLimited,
            .message = "Strava's limit on requests is reached.",
            .status = response.status,
            .resets_at = rate_limit_reset(response, now),
        };
    }
    return StravaError {
        .kind = StravaError::Kind::Failed,
        .message = std::format("Strava answered HTTP {} {}", response.status, what),
        .status = response.status,
    };
}

// What a 400 or 401 from the token request means. A code or refresh token
// Strava doesn't take, which it reports as an "invalid" error on that
// field, means logging in again. Anything else, such as a client secret
// Strava doesn't know, logging in again won't fix, so the user sees what
// Strava said.
StravaError token_error(HttpResponse const &response)
{
    auto const json = JSONValue::deserialize(response.body);
    if (json && json->is_object()) {
        if (auto const errors = json->get("errors"); errors && errors->is_array()) {
            for (auto const &error : *errors) {
                auto const field = error.try_get<std::string>("field").value_or("");
                if ((field == "code" || field == "refresh_token") && error.try_get<std::string>("code").value_or("") == "invalid") {
                    return not_logged_in();
                }
            }
        }
    }
    auto const message = (json && json->is_object()) ? json->try_get<std::string>("message").value_or("") : std::string { };
    return failed(std::format("Strava refused the login: {}", message.empty() ? std::format("HTTP {}", response.status) : message));
}

// `<15-minute>,<daily>`, as in X-ReadRateLimit-Usage.
std::optional<std::pair<i64, i64>> two_numbers(std::optional<std::string> const &value)
{
    if (!value) {
        return { };
    }
    auto const comma = value->find(',');
    if (comma == std::string::npos) {
        return { };
    }
    auto const number = [](std::string_view text) -> std::optional<i64> {
        text = strip(text);
        i64        n { 0 };
        auto const result = std::from_chars(text.data(), text.data() + text.size(), n);
        if (result.ec != std::errc { } || result.ptr != text.data() + text.size()) {
            return { };
        }
        return n;
    };
    auto const first = number(std::string_view { *value }.substr(0, comma));
    auto const second = number(std::string_view { *value }.substr(comma + 1));
    if (!first || !second) {
        return { };
    }
    return std::pair { *first, *second };
}

}

fs::path strava_tokens_file(fs::path const &config_dir)
{
    return config_dir / "strava-tokens.json";
}

i64 rate_limit_reset(HttpResponse const &response, i64 now)
{
    auto const usage = two_numbers(response.header("X-ReadRateLimit-Usage"));
    auto const limit = two_numbers(response.header("X-ReadRateLimit-Limit"));
    if (usage && limit && usage->second >= limit->second) {
        return (now / Day + 1) * Day;
    }
    return (now / QuarterHour + 1) * QuarterHour;
}

Decoded<std::vector<StravaActivity>> parse_activities(JSONValue const &page)
{
    if (!page.is_array()) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, "the page isn't a list of activities" });
    }
    std::vector<StravaActivity> activities;
    for (auto const &entry : page) {
        auto const id = entry.try_get<u64>("id");
        auto const start_date = entry.try_get<std::string>("start_date");
        auto const start = start_date ? DateTime::parse_iso8601(*start_date) : std::unexpected(LibCError { std::string { "no start" } });
        // One odd entry shouldn't keep the others from their titles.
        if (!id || !start) {
            warning(Strava, "Skipping an activity without an ID or start time: {}", entry.serialize());
            continue;
        }
        activities.push_back(StravaActivity {
            .id = *id,
            .start_time = static_cast<i64>(start->timestamp),
            .name = entry.try_get<std::string>("name").value_or(""),
        });
    }
    return activities;
}

std::string Strava::authorize_url(std::string_view state) const
{
    return std::format("{}{}?{}", StravaBaseUrl, AuthorizePath,
        form_encode({
            { "client_id", client_id },
            { "redirect_uri", std::string { StravaRedirectUri } },
            { "response_type", "code" },
            { "approval_prompt", "auto" },
            { "scope", std::string { StravaScopes } },
            { "state", std::string { state } },
        }));
}

StravaResult<void> Strava::exchange_code(std::string_view code)
{
    return request_tokens({
        { "client_id", client_id },
        { "client_secret", client_secret },
        { "code", std::string { code } },
        { "grant_type", "authorization_code" },
    });
}

StravaResult<void> Strava::request_tokens(std::vector<std::pair<std::string, std::string>> const &form)
{
    auto const response = fetch(HttpRequest {
        .method = "POST",
        .url = std::format("{}{}", StravaBaseUrl, TokenPath),
        .headers = { "Accept: application/json", "Content-Type: application/x-www-form-urlencoded" },
        .body = form_encode(form),
    });
    if (!response) {
        return std::unexpected(unreachable(response.error()));
    }
    if (response->status == 400 || response->status == 401) {
        return std::unexpected(token_error(*response));
    }
    if (response->status != 200) {
        return std::unexpected(http_error(*response, "when asked for tokens", now()));
    }
    auto const json = JSONValue::deserialize(response->body);
    auto const access_token = json ? json->try_get<std::string>("access_token") : std::unexpected(json.error());
    auto const refresh_token = json ? json->try_get<std::string>("refresh_token") : std::unexpected(json.error());
    auto const expires_at = json ? json->try_get<i64>("expires_at") : std::unexpected(json.error());
    if (!access_token || !refresh_token || !expires_at) {
        return std::unexpected(failed("Strava's answer to the login has no tokens"));
    }
    OAuthTokens fresh {
        .access_token = *access_token,
        .refresh_token = *refresh_token,
        .expires_at = *expires_at,
    };
    // Saved before anything else: Strava gives a new refresh token with
    // each refresh, and the old one stops working at once.
    if (auto const saved = write_tokens(tokens_file, fresh); !saved) {
        return std::unexpected(failed(std::format("Could not save the Strava login: {}", saved.error().to_string())));
    }
    tokens = std::move(fresh);
    return { };
}

StravaResult<void> Strava::refresh_if_expiring()
{
    if (!tokens) {
        return std::unexpected(not_logged_in());
    }
    if (tokens->expires_at - now() > RefreshMargin) {
        return { };
    }
    trace(Strava, "Refreshing the access token");
    return request_tokens({
        { "client_id", client_id },
        { "client_secret", client_secret },
        { "grant_type", "refresh_token" },
        { "refresh_token", tokens->refresh_token },
    });
}

StravaResult<HttpResponse> Strava::get(std::string const &path)
{
    if (auto const refreshed = refresh_if_expiring(); !refreshed) {
        return std::unexpected(refreshed.error());
    }
    auto response = fetch(HttpRequest {
        .url = std::format("{}{}", StravaBaseUrl, path),
        .headers = { "Accept: application/json", std::format("Authorization: Bearer {}", tokens->access_token) },
    });
    if (!response) {
        return std::unexpected(unreachable(response.error()));
    }
    if (response->status == 401) {
        return std::unexpected(not_logged_in());
    }
    if (response->status != 200) {
        return std::unexpected(http_error(*response, std::format("for `{}`", path), now()));
    }
    return std::move(*response);
}

StravaResult<std::vector<StravaActivity>> Strava::list_activities(i64 after, i64 before)
{
    std::vector<StravaActivity> activities;
    for (size_t page = 1;; ++page) {
        auto const response = get(std::format("{}?after={}&before={}&page={}&per_page={}", ActivitiesPath, after, before, page, PageSize));
        if (!response) {
            return std::unexpected(response.error());
        }
        auto const json = JSONValue::deserialize(response->body);
        if (!json) {
            return std::unexpected(failed(std::format("Strava's list of activities isn't JSON: {}", json.error().to_string())));
        }
        auto const listed = parse_activities(*json);
        if (!listed) {
            return std::unexpected(failed(std::format("Unexpected list of activities from Strava: {}", listed.error().to_string())));
        }
        activities.insert(activities.end(), listed->begin(), listed->end());
        // A short page is the last: counted before entries were skipped.
        if (json->size() < PageSize) {
            return activities;
        }
    }
}

StravaResult<std::string> Strava::description(u64 id)
{
    auto const response = get(std::format("{}/{}", ActivityPath, id));
    if (!response) {
        return std::unexpected(response.error());
    }
    auto const json = JSONValue::deserialize(response->body);
    if (!json || !json->is_object()) {
        return std::unexpected(failed(std::format("Strava's activity {} isn't JSON", id)));
    }
    // A null description is none.
    return json->try_get<std::string>("description").value_or("");
}

}
