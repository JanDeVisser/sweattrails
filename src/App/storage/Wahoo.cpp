/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <charconv>
#include <chrono>
#include <format>
#include <system_error>

#include <Date.h>
#include <IO.h>
#include <StringUtil.h>

#include <storage/Download.h>
#include <storage/Storage.h>
#include <storage/Wahoo.h>

namespace ST {

namespace {

constexpr std::string_view AuthorizePath = "/oauth/authorize";
constexpr std::string_view TokenPath = "/oauth/token";
constexpr std::string_view WorkoutsPath = "/v1/workouts";

// Wahoo's default page size.
constexpr i64 PageSize = 30;

// An access token this close to expiring is refreshed first, so that it
// doesn't expire halfway through a download.
constexpr i64 RefreshMargin = 60;

WahooError failed(std::string message)
{
    return WahooError { .kind = WahooError::Kind::Failed, .message = std::move(message) };
}

WahooError not_logged_in()
{
    return WahooError {
        .kind = WahooError::Kind::NotLoggedIn,
        .message = "SweatTrails isn't logged in to Wahoo, or Wahoo no longer accepts the login. Log in to Wahoo again.",
    };
}

// The seconds until the rate limit resets, if Wahoo said.
std::optional<i64> rate_limit_reset(HttpResponse const &response)
{
    auto const value = response.header("X-RateLimit-Reset");
    if (!value) {
        return { };
    }
    i64        seconds { 0 };
    auto const result = std::from_chars(value->data(), value->data() + value->size(), seconds);
    if (result.ec != std::errc { } || result.ptr != value->data() + value->size()) {
        return { };
    }
    return seconds;
}

// So that "Try again after 12:45" isn't a few seconds early.
i64 round_up_to_minute(i64 time)
{
    return (time + 59) / 60 * 60;
}

WahooError http_error(HttpResponse const &response, std::string_view what, i64 now)
{
    if (response.status == 429) {
        auto const reset = rate_limit_reset(response);
        auto const resets_at = reset ? now + *reset : 0;
        return WahooError {
            .kind = WahooError::Kind::Failed,
            .message = std::format("Wahoo's limit on requests is reached. Try again {}.",
                reset ? std::format("after {}", clock_time(round_up_to_minute(resets_at))) : std::string { "later" }),
            .status = response.status,
            .resets_at = resets_at,
        };
    }
    return WahooError {
        .kind = WahooError::Kind::Failed,
        .message = std::format("Wahoo answered HTTP {} {}", response.status, what),
        .status = response.status,
    };
}

// What a 400 or 401 from the token request means. invalid_grant is a code
// or refresh token Wahoo doesn't take: the user logs in again. Anything
// else, such as invalid_client for a client ID Wahoo doesn't know, logging
// in again won't fix, so the user sees what Wahoo said.
WahooError token_error(std::string_view body)
{
    auto const json = JSONValue::deserialize(body);
    auto const error = json ? json->try_get<std::string>("error").value_or("") : std::string { };
    if (error == "invalid_grant") {
        return not_logged_in();
    }
    auto const description = json ? json->try_get<std::string>("error_description").value_or("") : std::string { };
    return failed(std::format("Wahoo refused the login: {}{}{}",
        error.empty() ? std::string { "no reason given" } : error,
        description.empty() ? "" : ": ",
        description));
}

WahooError unreachable(LibCError const &error)
{
    return failed(std::format("Could not reach Wahoo: {}", error.description));
}

// Wahoo won't hand over this one FIT file.
bool unavailable(WahooError const &error)
{
    return error.status == 403 || error.status == 404;
}

WahooResult<void> write_whole_file(fs::path const &path, std::string_view contents)
{
    if (auto const written = write_file_atomically(path, contents); !written) {
        return std::unexpected(failed(written.error().to_string()));
    }
    return { };
}

}

std::string rate_limit_headers(HttpResponse const &response)
{
    constexpr std::string_view Prefix = "x-ratelimit";
    std::string                ret;
    for (auto const &[name, value] : response.headers) {
        if (to_lower(name).starts_with(Prefix)) {
            ret += std::format("{}{}: {}", ret.empty() ? "" : ", ", name, value);
        }
    }
    return ret;
}

fs::path wahoo_tokens_file(fs::path const &config_dir)
{
    return config_dir / "wahoo-tokens.json";
}

WahooResult<void> save_tokens(fs::path const &file, OAuthTokens const &tokens)
{
    if (auto const written = write_tokens(file, tokens); !written) {
        return std::unexpected(failed(std::format("Could not save the Wahoo login: {}", written.error().to_string())));
    }
    return { };
}

Decoded<std::vector<WahooWorkout>> parse_workouts(JSONValue const &page)
{
    auto const list = page.is_object() ? page.get("workouts") : std::nullopt;
    if (!list || !list->is_array()) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, "the page has no list of workouts" });
    }
    std::vector<WahooWorkout> workouts;
    for (auto const &entry : *list) {
        auto const id = entry.try_get<u64>("id");
        auto const starts = entry.try_get<std::string>("starts");
        auto const start = starts ? DateTime::parse_iso8601(*starts) : std::unexpected(LibCError { std::string { "no start" } });
        // One odd entry shouldn't keep the others from being downloaded.
        if (!id || !start) {
            warning(Wahoo, "Skipping a workout without an ID or start time: {}", entry.serialize());
            continue;
        }
        // A workout without a summary, or a summary without a file, has no
        // FIT file to download.
        std::string file_url { };
        if (auto const summary = entry.get("workout_summary"); summary && summary->is_object()) {
            if (auto const file = summary->get("file"); file && file->is_object()) {
                file_url = file->try_get<std::string>("url").value_or("");
            }
        }
        workouts.push_back(WahooWorkout {
            .id = *id,
            .start_time = static_cast<i64>(start->timestamp),
            .file_url = std::move(file_url),
            .json = entry,
        });
    }
    return workouts;
}

std::string Wahoo::authorize_url(std::string_view challenge, std::string_view state) const
{
    return std::format("{}{}?{}", WahooBaseUrl, AuthorizePath,
        form_encode({
            { "client_id", client_id },
            { "redirect_uri", std::string { WahooRedirectUri } },
            { "response_type", "code" },
            { "scope", std::string { WahooScopes } },
            { "state", std::string { state } },
            { "code_challenge", std::string { challenge } },
            { "code_challenge_method", "S256" },
        }));
}

WahooResult<void> Wahoo::exchange_code(std::string_view code, std::string_view verifier)
{
    return request_tokens(
        {
            { "grant_type", "authorization_code" },
            { "client_id", client_id },
            { "code", std::string { code } },
            { "redirect_uri", std::string { WahooRedirectUri } },
            { "code_verifier", std::string { verifier } },
        },
        std::string { verifier });
}

WahooResult<void> Wahoo::request_tokens(std::vector<std::pair<std::string, std::string>> const &form, std::string verifier)
{
    auto const response = fetch(HttpRequest {
        .method = "POST",
        .url = std::format("{}{}", WahooBaseUrl, TokenPath),
        .headers = { "Accept: application/json", "Content-Type: application/x-www-form-urlencoded" },
        .body = form_encode(form),
    });
    if (!response) {
        return std::unexpected(unreachable(response.error()));
    }
    trace(Wahoo, "Token request: HTTP {}; {}", response->status, rate_limit_headers(*response));
    if (response->status == 400 || response->status == 401) {
        return std::unexpected(token_error(response->body));
    }
    if (response->status != 200) {
        return std::unexpected(http_error(*response, "when asked for tokens", now()));
    }
    auto const json = JSONValue::deserialize(response->body);
    auto const access_token = json ? json->try_get<std::string>("access_token") : std::unexpected(json.error());
    auto const refresh_token = json ? json->try_get<std::string>("refresh_token") : std::unexpected(json.error());
    auto const expires_in = json ? json->try_get<i64>("expires_in") : std::unexpected(json.error());
    if (!access_token || !refresh_token || !expires_in) {
        return std::unexpected(failed("Wahoo's answer to the login has no tokens"));
    }
    OAuthTokens fresh {
        .access_token = *access_token,
        .refresh_token = *refresh_token,
        .expires_at = now() + *expires_in,
        .code_verifier = std::move(verifier),
    };
    // Saved before they're used: once the new access token is used, Wahoo
    // revokes the old tokens, and the saved ones have to be the new ones.
    if (auto const saved = save_tokens(tokens_file, fresh); !saved) {
        return std::unexpected(saved.error());
    }
    tokens = std::move(fresh);
    return { };
}

WahooResult<void> Wahoo::refresh_if_expiring()
{
    if (!tokens) {
        return std::unexpected(not_logged_in());
    }
    if (tokens->expires_at - now() > RefreshMargin) {
        return { };
    }
    trace(Wahoo, "Refreshing the access token");
    return request_tokens(
        {
            { "grant_type", "refresh_token" },
            { "client_id", client_id },
            { "refresh_token", tokens->refresh_token },
            { "code_verifier", tokens->code_verifier.value_or("") },
        },
        tokens->code_verifier.value_or(""));
}

WahooResult<HttpResponse> Wahoo::get(std::string const &path)
{
    if (auto const refreshed = refresh_if_expiring(); !refreshed) {
        return std::unexpected(refreshed.error());
    }
    auto response = fetch(HttpRequest {
        .url = std::format("{}{}", WahooBaseUrl, path),
        .headers = { "Accept: application/json", std::format("Authorization: Bearer {}", tokens->access_token) },
    });
    if (!response) {
        return std::unexpected(unreachable(response.error()));
    }
    trace(Wahoo, "`{}`: HTTP {}; {}", path, response->status, rate_limit_headers(*response));
    if (response->status == 401) {
        return std::unexpected(not_logged_in());
    }
    if (response->status != 200) {
        return std::unexpected(http_error(*response, std::format("for `{}`", path), now()));
    }
    return std::move(*response);
}

// A whole history's list runs into Wahoo's 5-minute limit once or twice:
// a limit that resets that soon is waited for, and the page asked for
// again. A longer one stops the list.
WahooResult<HttpResponse> Wahoo::get_waiting(std::string const &path)
{
    auto const soon = [this](WahooResult<HttpResponse> const &response) {
        return !response && response.error().status == 429 && response.error().resets_at > 0
            && response.error().resets_at - now() <= WahooMaxLimitWait;
    };
    auto ret = get(path);
    for (auto waited = 0; wait_for_limit && waited < WahooLimitWaits && soon(ret); ++waited) {
        warning(Wahoo, "Wahoo's limit is reached: waiting until {}", clock_time(ret.error().resets_at));
        wait_for_limit(ret.error().resets_at);
        ret = get(path);
    }
    return ret;
}

WahooResult<std::vector<WahooWorkout>> Wahoo::list_workouts(i64 since)
{
    std::vector<WahooWorkout> workouts;
    // Newest first: past the first workout before `since`, they're all
    // before it.
    for (i64 page = 1;; ++page) {
        auto const response = get_waiting(std::format("{}?page={}&per_page={}", WorkoutsPath, page, PageSize));
        if (!response) {
            return std::unexpected(response.error());
        }
        auto const json = JSONValue::deserialize(response->body);
        if (!json) {
            return std::unexpected(failed(std::format("Wahoo's list of workouts isn't JSON: {}", json.error().to_string())));
        }
        auto const listed = parse_workouts(*json);
        if (!listed) {
            return std::unexpected(failed(std::format("Unexpected list of workouts from Wahoo: {}", listed.error().to_string())));
        }
        for (auto const &workout : *listed) {
            if (workout.start_time < since) {
                return workouts;
            }
            workouts.push_back(workout);
        }
        auto const total = json->try_get<i64>("total");
        if (listed->empty() || (total && page * PageSize >= *total)) {
            return workouts;
        }
    }
}

WahooResult<std::string> Wahoo::download_fit(std::string const &url)
{
    // The URL is on Wahoo's CDN, and needs no token.
    auto response = fetch(HttpRequest { .url = url });
    if (!response) {
        return std::unexpected(unreachable(response.error()));
    }
    if (response->status != 200) {
        return std::unexpected(http_error(*response, "for a FIT file", now()));
    }
    return std::move(response->body);
}

WahooResult<size_t> download_new_workouts(Wahoo &wahoo, Storage const &storage, i64 since, WahooProgress const &progress, Pacing const &pacing)
{
    auto const listed = wahoo.list_workouts(since);
    if (!listed) {
        return std::unexpected(listed.error());
    }
    std::vector<WahooWorkout> wanted;
    for (auto const &workout : *listed) {
        // Downloaded before, but not organized yet: the inbox will.
        std::error_code ec { };
        auto const      in_inbox = fs::exists(storage.inbox_dir / std::format("wahoo-{}.fit", workout.id), ec);
        if (!workout.file_url.empty() && !in_inbox && !storage.has_activity(workout.start_time)) {
            wanted.push_back(workout);
        }
    }
    size_t downloaded { 0 };
    for (size_t ix = 0; ix < wanted.size(); ++ix) {
        auto const &workout = wanted[ix];
        if (pause_before(pacing, ix)) {
            pause(pacing);
        }
        if (progress) {
            progress(ix, wanted.size());
        }
        auto const fit = fetch_paced(pacing, [&wahoo, &workout] { return wahoo.download_fit(workout.file_url); });
        if (!fit && unavailable(fit.error())) {
            warning(Wahoo, "Skipping workout {}: {}", workout.id, fit.error().to_string());
            continue;
        }
        if (!fit) {
            return std::unexpected(fit.error());
        }
        // The sidecar first: the inbox takes a FIT file as soon as it's
        // there, and would leave the sidecar behind if it came second.
        auto const stem = storage.inbox_dir / std::format("wahoo-{}", workout.id);
        if (auto const written = write_whole_file(stem.string() + ".json", workout.json.serialize()); !written) {
            return std::unexpected(written.error());
        }
        if (auto const written = write_whole_file(stem.string() + ".fit", *fit); !written) {
            return std::unexpected(written.error());
        }
        trace(Wahoo, "Downloaded workout {} to `{}.fit`", workout.id, stem.string());
        ++downloaded;
    }
    return downloaded;
}

fs::path wahoo_complete_file(Storage const &storage)
{
    return storage.data_dir / "wahoo-complete.txt";
}

std::optional<i64> read_wahoo_complete(fs::path const &file)
{
    std::error_code ec { };
    if (!fs::exists(file, ec)) {
        return { };
    }
    auto const contents = read_file_by_name(file.string());
    if (!contents) {
        warning(Wahoo, "Can't read `{}`: {}", file.string(), contents.error().to_string());
        return { };
    }
    auto const complete = DateTime::parse_iso8601(strip(*contents));
    if (!complete) {
        warning(Wahoo, "`{}` doesn't hold a time: {}", file.string(), complete.error().to_string());
        return { };
    }
    return static_cast<i64>(complete->timestamp);
}

WahooResult<void> write_wahoo_complete(fs::path const &file, i64 complete)
{
    auto const time = std::chrono::sys_seconds { std::chrono::seconds { complete } };
    return write_whole_file(file, std::format("{:%FT%TZ}\n", time));
}

bool whole_history(i64 since)
{
    using namespace std::chrono;
    // A day into the year, so that "Everything" counted in local time
    // anywhere is before it.
    auto const second_of_january = sys_days { year { EverythingSinceYear } / January / 2 };
    return since < duration_cast<seconds>(second_of_january.time_since_epoch()).count();
}

WahooPlan plan_wahoo_download(i64 since, std::optional<i64> complete, i64 overlap)
{
    if (whole_history(since)) {
        return WahooPlan { .since = complete ? *complete - overlap : since, .completes = true };
    }
    return WahooPlan { .since = since, .completes = complete && since <= *complete };
}

WahooResult<size_t> download_wahoo_history(Wahoo &wahoo, Storage const &storage, i64 since, i64 overlap, WahooProgress const &progress, Pacing const &pacing)
{
    auto const started = wahoo.now();
    auto const file = wahoo_complete_file(storage);
    auto       complete = read_wahoo_complete(file);
    // A clock set back would leave T in the future, and "Everything" would
    // list too little.
    if (complete && *complete > started) {
        complete = started;
    }
    auto const plan = plan_wahoo_download(since, complete, overlap);
    trace(Wahoo, "Listing back to {}{}", plan.since, plan.completes ? ", which completes the store" : "");
    auto const count = download_new_workouts(wahoo, storage, plan.since, progress, pacing);
    if (count && plan.completes) {
        // Not written, the next "Everything" lists more than it has to.
        if (auto const written = write_wahoo_complete(file, started); !written) {
            warning(Wahoo, "Can't remember how far Wahoo's history is complete: {}", written.error().to_string());
        }
    }
    return count;
}

}
