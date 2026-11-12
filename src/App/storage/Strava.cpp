/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <charconv>
#include <format>
#include <optional>
#include <set>
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
    return failed(std::format("Could not reach Strava: {}", error.description));
}

StravaError http_error(HttpResponse const &response, std::string_view what, i64 now)
{
    if (response.status == 429) {
        auto const usage = rate_limit_usage(response);
        return StravaError {
            .kind = StravaError::Kind::RateLimited,
            .message = "Strava's limit on requests is reached.",
            .status = response.status,
            .resets_at = rate_limit_reset(response, now),
            .daily = usage && usage->day_used_up(),
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

std::optional<StravaUsage> rate_limit_usage(HttpResponse const &response)
{
    auto const usage = two_numbers(response.header("X-ReadRateLimit-Usage"));
    auto const limit = two_numbers(response.header("X-ReadRateLimit-Limit"));
    if (!usage || !limit) {
        return { };
    }
    return StravaUsage {
        .quarter_used = usage->first,
        .quarter_limit = limit->first,
        .day_used = usage->second,
        .day_limit = limit->second,
    };
}

i64 next_quarter_hour(i64 now)
{
    return (now / QuarterHour + 1) * QuarterHour;
}

i64 next_strava_day(i64 now)
{
    return (now / Day + 1) * Day;
}

i64 rate_limit_reset(HttpResponse const &response, i64 now)
{
    if (auto const usage = rate_limit_usage(response); usage && usage->day_used_up()) {
        return next_strava_day(now);
    }
    return next_quarter_hour(now);
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
    HttpRequest const request {
        .url = std::format("{}{}", StravaBaseUrl, path),
        .headers = { "Accept: application/json", std::format("Authorization: Bearer {}", tokens->access_token) },
    };
    auto response = fetch(request);
    for (auto retry = 0; retry < retries && (!response || response->status >= 500); ++retry) {
        warning(Strava, "Strava didn't answer `{}` ({}): asking again in {}", path,
            response ? std::format("HTTP {}", response->status) : response.error().to_string(), retry_pause);
        if (wait) {
            wait(retry_pause);
        }
        response = fetch(request);
    }
    if (!response) {
        return std::unexpected(unreachable(response.error()));
    }
    if (auto const said = rate_limit_usage(*response); said) {
        usage = said;
    }
    if (response->status == 401) {
        return std::unexpected(not_logged_in());
    }
    if (response->status != 200) {
        return std::unexpected(http_error(*response, std::format("for `{}`", path), now()));
    }
    return std::move(*response);
}

// The list is what the user asked for, and its pages are few: a page
// that runs into the 15-minute limit waits for it, rather than stopping
// the titles. The day's limit is too long to wait for.
StravaResult<StravaPage> Strava::list_page(i64 after, i64 before, size_t count, size_t page)
{
    auto const quarter_limited = [](StravaResult<StravaPage> const &page) {
        return !page && page.error().kind == StravaError::Kind::RateLimited && !page.error().daily;
    };
    auto ret = fetch_page(after, before, count, page);
    for (auto waited = 0; wait_for_limit && waited < StravaLimitWaits && quarter_limited(ret); ++waited) {
        warning(Strava, "Strava's 15-minute limit is reached: waiting until {}", ret.error().resets_at);
        wait_for_limit(ret.error().resets_at);
        ret = fetch_page(after, before, count, page);
    }
    return ret;
}

StravaResult<StravaPage> Strava::fetch_page(i64 after, i64 before, size_t count, size_t page)
{
    auto const response = get(std::format("{}?after={}&before={}&per_page={}{}", ActivitiesPath, after, before, count,
        page == 1 ? std::string { } : std::format("&page={}", page)));
    if (!response) {
        return std::unexpected(response.error());
    }
    auto const json = JSONValue::deserialize(response->body);
    if (!json) {
        return std::unexpected(failed(std::format("Strava's list of activities isn't JSON: {}", json.error().to_string())));
    }
    auto listed = parse_activities(*json);
    if (!listed) {
        return std::unexpected(failed(std::format("Unexpected list of activities from Strava: {}", listed.error().to_string())));
    }
    // A short page is the last: counted before entries were skipped.
    return StravaPage { .activities = std::move(*listed), .last = json->size() < count };
}

// Strava answers HTTP 404, "Record Not Found", for any page with an
// activity whose upload it has lost. Jan's history has one from 2010. So
// smaller pages are asked for, halving the difference, to find it: the
// activities before it are listed, and then the one after it, asked for
// on its own, past any more lost ones.
StravaResult<StravaPage> Strava::list_around_lost(i64 after, i64 before)
{
    // A page of `good` activities comes back, one of `bad` doesn't: the
    // lost activity is the one at `good`.
    size_t     good { 0 };
    size_t     bad { PageSize };
    StravaPage ret;
    while (bad - good > 1) {
        auto const middle = (good + bad) / 2;
        auto       page = list_page(after, before, middle);
        if (page) {
            good = middle;
            ret = std::move(*page);
            // The list ended before the 404's page did: it wasn't a lost
            // activity.
            if (ret.last) {
                return ret;
            }
        } else if (page.error().status == 404) {
            bad = middle;
        } else {
            return std::unexpected(page.error());
        }
    }
    for (auto index = good + 1; index < PageSize; ++index) {
        // The activity at `index`, on its own.
        auto const page = list_page(after, before, 1, index + 1);
        if (!page && page.error().status == 404) {
            continue;
        }
        if (!page) {
            return std::unexpected(page.error());
        }
        auto const lost = index - good;
        warning(Strava, "Strava has lost the upload of {} {}: skipped", lost == 1 ? "an activity" : std::format("{} activities", lost),
            ret.activities.empty() ? std::string { "at the start of the list" } : std::format("after {}", ret.activities.back().id));
        ret.activities.append_range(page->activities);
        ret.last = page->last;
        return ret;
    }
    return std::unexpected(failed("Strava answered HTTP 404 for a whole page of its list, one activity at a time"));
}

StravaResult<StravaListing> Strava::list_activities(i64 after, i64 before)
{
    StravaListing ret;
    // The first page's error is the list's; a later one's leaves what
    // came before it.
    auto const stop = [&ret](StravaError error) -> StravaResult<StravaListing> {
        if (ret.activities.empty()) {
            return std::unexpected(std::move(error));
        }
        ret.stopped = std::move(error);
        return std::move(ret);
    };
    // Each request asks for the first page past the last activity listed
    // so far, rather than for the next page: that way a lost activity (see
    // list_around_lost) can be found with smaller pages, which only works
    // from the start of the list. An activity that starts in the same
    // second as the last one is listed again, so it isn't missed, and the
    // second time it's left out.
    std::set<u64> listed_ids;
    // Strava lists the newest first, even with `after`; which way a page
    // goes says which end of the window to move.
    bool newest_first { true };
    while (true) {
        auto page = list_page(after, before, PageSize);
        if (!page && page.error().status == 404) {
            page = list_around_lost(after, before);
        }
        if (!page) {
            return stop(page.error());
        }
        auto const known = listed_ids.size();
        for (auto const &activity : page->activities) {
            if (listed_ids.insert(activity.id).second) {
                ret.activities.push_back(activity);
            }
        }
        // A page with nothing new can't move the window on: a whole page
        // of activities that start in the same second.
        if (page->last || listed_ids.size() == known) {
            return ret;
        }
        auto const &first = page->activities.front();
        auto const &last = page->activities.back();
        if (first.start_time != last.start_time) {
            newest_first = first.start_time > last.start_time;
        }
        if (newest_first) {
            before = last.start_time + 1;
        } else {
            after = last.start_time - 1;
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
