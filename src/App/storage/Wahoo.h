/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Downloads workouts from Wahoo's Cloud API: see specs/08-wahoo-download.md.
 * The user registers an app with Wahoo, and logs in to it from
 * SweatTrails with OAuth 2 and PKCE. The login's tokens are kept in a file
 * in SweatTrails' config folder.
 */

#pragma once

#include <ctime>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <JSON.h>
#include <Logging.h>

#include <Http.h>
#include <OAuth.h>

namespace ST {

namespace fs = std::filesystem;

struct Storage;

constexpr std::string_view WahooBaseUrl = "https://api.wahooligan.com";

// Wahoo only takes https redirect URIs: the login's server answers with
// the user's mkcert certificate.
constexpr int              WahooCallbackPort = 8090;
constexpr std::string_view WahooCallbackPath = "/callback";
constexpr std::string_view WahooRedirectUri = "https://localhost:8090/callback";

// user_read, because without it the API answers 403, and offline_data for
// a refresh token.
constexpr std::string_view WahooScopes = "workouts_read user_read offline_data";

// The number of days a download looks back when the settings don't say.
constexpr int WahooDefaultDays = 30;

struct WahooError {
    enum class Kind {
        NotLoggedIn, // No tokens, or tokens Wahoo no longer accepts
        Failed,      // Anything else: no connection, an HTTP error, bad data
    };

    Kind        kind { Kind::Failed };
    std::string message { };
    long        status { 0 }; // The HTTP status, for an HTTP error

    [[nodiscard]] std::string to_string() const { return message; }
};

template<typename T>
using WahooResult = std::expected<T, WahooError>;

// `wahoo-tokens.json` in SweatTrails' config folder.
fs::path wahoo_tokens_file(fs::path const &config_dir);

// A workout in Wahoo's list.
struct WahooWorkout {
    u64         id { 0 };
    i64         start_time { 0 }; // Unix time
    std::string file_url { };     // Empty for a workout without a FIT file
    JSONValue   json { };         // The list's entry, as it came
};

// The workouts in a page of Wahoo's list. A workout without an ID or start
// time is skipped, with a warning.
Decoded<std::vector<WahooWorkout>> parse_workouts(JSONValue const &page);

struct Wahoo {
    HttpFetch                  fetch;
    std::string                client_id { };
    fs::path                   tokens_file { };
    std::optional<OAuthTokens> tokens { };
    std::function<i64()>       now { [] { return static_cast<i64>(std::time(nullptr)); } };

    // Where the browser goes to log in. `state` comes back with the code.
    [[nodiscard]] std::string authorize_url(std::string_view challenge, std::string_view state) const;

    // Trades the code the login brought back for tokens, and saves them.
    WahooResult<void> exchange_code(std::string_view code, std::string_view verifier);

    // The workouts that started at or after `since`, newest first.
    WahooResult<std::vector<WahooWorkout>> list_workouts(i64 since);

    // The FIT file at a workout's `file_url`.
    WahooResult<std::string> download_fit(std::string const &url);

private:
    WahooResult<void>         refresh_if_expiring();
    WahooResult<void>         request_tokens(std::vector<std::pair<std::string, std::string>> const &form, std::string verifier);
    WahooResult<HttpResponse> get(std::string const &path);
};

// Called before each workout is downloaded, with its index and the number
// of workouts to download.
using WahooProgress = std::function<void(size_t index, size_t count)>;

// Downloads the workouts since `since` that the store doesn't have into its
// inbox: `wahoo-<id>.json`, the workout as Wahoo sent it, and
// `wahoo-<id>.fit`. Returns how many were downloaded. A workout without a
// FIT file is skipped, and so is one whose FIT file Wahoo won't hand over
// (403 or 404), with a warning; any other error stops the download.
// Workouts downloaded before an error stay in the inbox.
WahooResult<size_t> download_new_workouts(Wahoo &wahoo, Storage const &storage, i64 since, WahooProgress const &progress = { });

}
