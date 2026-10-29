/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <charconv>
#include <format>
#include <system_error>

#include <Date.h>
#include <IO.h>
#include <StringUtil.h>

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

// When to try again, `reset` seconds from now.
std::string when(std::optional<i64> reset)
{
    if (!reset) {
        return "later";
    }
    if (*reset <= 60) {
        return "in a minute";
    }
    return std::format("in {} minutes", (*reset + 59) / 60);
}

WahooError http_error(HttpResponse const &response, std::string_view what)
{
    if (response.status == 429) {
        auto const reset = rate_limit_reset(response);
        return WahooError {
            .kind = WahooError::Kind::Failed,
            .message = std::format("Wahoo's limit on requests is reached. Try again {}.", when(reset)),
            .status = response.status,
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
    return failed(std::format("Could not reach Wahoo: {}", error.to_string()));
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

std::expected<std::string, std::string> login_code(std::expected<QueryParameters, RedirectError> const &redirect)
{
    if (!redirect) {
        return std::unexpected(redirect.error().kind == RedirectError::Kind::TimedOut
                ? std::string { "The Wahoo login timed out. Try again from SweatTrails when you're ready to log in." }
                : redirect.error().to_string());
    }
    // access_denied when the user doesn't allow SweatTrails access.
    if (redirect->contains("error")) {
        return std::unexpected(std::format("The Wahoo login didn't go through: {}", redirect->at("error")));
    }
    if (!redirect->contains("code") || redirect->at("code").empty()) {
        return std::unexpected(std::string { "The Wahoo login came back without a code. Try again." });
    }
    return redirect->at("code");
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
    if (response->status == 400 || response->status == 401) {
        return std::unexpected(token_error(response->body));
    }
    if (response->status != 200) {
        return std::unexpected(http_error(*response, "when asked for tokens"));
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
    if (response->status == 401) {
        return std::unexpected(not_logged_in());
    }
    if (response->status != 200) {
        return std::unexpected(http_error(*response, std::format("for `{}`", path)));
    }
    return std::move(*response);
}

WahooResult<std::vector<WahooWorkout>> Wahoo::list_workouts(i64 since)
{
    std::vector<WahooWorkout> workouts;
    // Newest first: past the first workout before `since`, they're all
    // before it.
    for (i64 page = 1;; ++page) {
        auto const response = get(std::format("{}?page={}&per_page={}", WorkoutsPath, page, PageSize));
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
        return std::unexpected(http_error(*response, "for a FIT file"));
    }
    return std::move(response->body);
}

WahooResult<size_t> download_new_workouts(Wahoo &wahoo, Storage const &storage, i64 since, WahooProgress const &progress)
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
        if (progress) {
            progress(ix, wanted.size());
        }
        auto const fit = wahoo.download_fit(workout.file_url);
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

}
