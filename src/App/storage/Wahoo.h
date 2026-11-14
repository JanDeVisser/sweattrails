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
#include <storage/Pacing.h>

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

// A limit that resets in at most this many seconds, Wahoo's shortest
// window, is waited for. A page of the list waits at most WahooLimitWaits
// times.
constexpr i64 WahooMaxLimitWait = 5 * 60;
constexpr int WahooLimitWaits = 2;

struct WahooError {
    enum class Kind {
        NotLoggedIn, // No tokens, or tokens Wahoo no longer accepts
        Failed,      // Anything else: no connection, an HTTP error, bad data
    };

    Kind        kind { Kind::Failed };
    std::string message { };
    long        status { 0 };    // The HTTP status, for an HTTP error
    i64         resets_at { 0 }; // For a 429: when Wahoo's limit resets, a Unix time, if it said

    [[nodiscard]] std::string to_string() const { return message; }
};

template<typename T>
using WahooResult = std::expected<T, WahooError>;

// The X-RateLimit-* headers of an answer, as "Name: value" separated by
// commas, for the log: Wahoo doesn't document its limits, and these say
// what they are, and how much of them is used.
std::string rate_limit_headers(HttpResponse const &response);

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
    // Waits until a Unix time, for the list's limit to reset: see
    // WahooMaxLimitWait. Without it, the list stops at the limit.
    std::function<void(i64 until)> wait_for_limit { };

    // Where the browser goes to log in. `state` comes back with the code.
    [[nodiscard]] std::string authorize_url(std::string_view challenge, std::string_view state) const;

    // Trades the code the login brought back for tokens, and saves them.
    WahooResult<void> exchange_code(std::string_view code, std::string_view verifier);

    // The workouts that started at or after `since`, newest first. A page
    // that runs into a limit that resets soon waits for it.
    WahooResult<std::vector<WahooWorkout>> list_workouts(i64 since);

    // The FIT file at a workout's `file_url`.
    WahooResult<std::string> download_fit(std::string const &url);

private:
    WahooResult<void>         refresh_if_expiring();
    WahooResult<void>         request_tokens(std::vector<std::pair<std::string, std::string>> const &form, std::string verifier);
    WahooResult<HttpResponse> get(std::string const &path);
    WahooResult<HttpResponse> get_waiting(std::string const &path);
};

// Called before each workout is downloaded, with its index and the number
// of workouts to download.
using WahooProgress = std::function<void(size_t index, size_t count)>;

// Downloads the workouts since `since` that the store doesn't have into its
// inbox: `wahoo-<id>.json`, the workout as Wahoo sent it, and
// `wahoo-<id>.fit`. Returns how many were downloaded. A workout without a
// FIT file is skipped, and so is one whose FIT file Wahoo won't hand over
// (403 or 404), with a warning; any other error stops the download.
// Workouts downloaded before an error stay in the inbox. The FIT files are
// downloaded as `pacing` says: see specs/43-download-history.md.
WahooResult<size_t> download_new_workouts(Wahoo &wahoo, Storage const &storage, i64 since, WahooProgress const &progress = { }, Pacing const &pacing = { });

// `wahoo-complete.txt` in the data folder: the time up to which the store
// has everything Wahoo had, as ISO 8601 in UTC. See
// specs/46-wahoo-everything.md.
fs::path wahoo_complete_file(Storage const &storage);

// The time in `file`, or nothing if there's no file, or it can't be read.
std::optional<i64> read_wahoo_complete(fs::path const &file);

WahooResult<void> write_wahoo_complete(fs::path const &file, i64 complete);

// Whether a download that looks back to `since`, a Unix time, asks for
// Wahoo's whole history, as "Everything" does: since before
// EverythingSinceYear, give or take a time zone.
bool whole_history(i64 since);

// What a download from Wahoo lists, by how far the store is complete.
struct WahooPlan {
    i64  since { 0 };         // Lists back to here
    bool completes { false }; // Done without an error, the store is complete up to its start
};

// For a download that looks back to `since`, with the store complete up
// to `complete`. "Everything" lists back to `complete` less `overlap`
// seconds, which catches a workout synced to Wahoo after it, or the whole
// history without it. Any other download completes the store only if it
// looks back as far as `complete`, so that there's no gap.
WahooPlan plan_wahoo_download(i64 since, std::optional<i64> complete, i64 overlap);

// download_new_workouts() for a download that looks back to `since`, as
// plan_wahoo_download() has it with wahoo_complete_file(), and, if it
// completes, the file set to the time it started.
WahooResult<size_t> download_wahoo_history(Wahoo &wahoo, Storage const &storage, i64 since, i64 overlap, WahooProgress const &progress = { }, Pacing const &pacing = { });

}
