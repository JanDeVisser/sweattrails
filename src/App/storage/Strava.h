/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Reads the athlete's activities from Strava's API, for their titles and
 * descriptions: see specs/24-strava-titles.md. The user registers an app with
 * Strava, and logs in to it from SweatTrails with OAuth 2. Strava has no
 * PKCE: the token request takes the app's client secret. The login's
 * tokens are kept in a file in SweatTrails' config folder.
 */

#pragma once

#include <ctime>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <JSON.h>
#include <Logging.h>

#include <Http.h>
#include <OAuth.h>

namespace ST {

namespace fs = std::filesystem;

constexpr std::string_view StravaBaseUrl = "https://www.strava.com";

// The same server, port and certificate as Wahoo's login: Strava takes
// localhost as the callback domain.
constexpr std::string_view StravaRedirectUri = "https://localhost:8090/callback";

// activity:read_all reads the activities only the athlete sees, too.
constexpr std::string_view StravaScopes = "activity:read_all";

// The number of days a download looks back when the settings don't say.
constexpr int StravaDefaultDays = 30;

struct StravaError {
    enum class Kind {
        NotLoggedIn, // No tokens, or tokens Strava no longer accepts
        RateLimited, // Strava answered 429: see `resets_at`
        Failed,      // Anything else: no connection, an HTTP error, bad data
    };

    Kind        kind { Kind::Failed };
    std::string message { };
    long        status { 0 };    // The HTTP status, for an HTTP error
    i64         resets_at { 0 }; // Unix time, for RateLimited

    [[nodiscard]] std::string to_string() const { return message; }
};

template<typename T>
using StravaResult = std::expected<T, StravaError>;

// `strava-tokens.json` in SweatTrails' config folder.
fs::path strava_tokens_file(fs::path const &config_dir);

// When the read limit a 429 ran into resets. Strava doesn't say: the
// 15-minute limit resets at :00, :15, :30 and :45, and the daily one at
// midnight UTC, which is when it resets if X-ReadRateLimit-Usage says the
// day's limit is used up.
i64 rate_limit_reset(HttpResponse const &response, i64 now);

// An activity in the athlete's list.
struct StravaActivity {
    u64         id { 0 };
    i64         start_time { 0 }; // Unix time, from start_date
    std::string name { };
};

// The activities in a page of Strava's list. One without an ID or a start
// time is skipped, with a warning.
Decoded<std::vector<StravaActivity>> parse_activities(JSONValue const &page);

struct StravaListing {
    std::vector<StravaActivity> activities { };
    std::optional<StravaError>  stopped { }; // Why the list stopped short, if it did
};

struct Strava {
    HttpFetch                  fetch;
    std::string                client_id { };
    std::string                client_secret { };
    fs::path                   tokens_file { };
    std::optional<OAuthTokens> tokens { };
    std::function<i64()>       now { [] { return static_cast<i64>(std::time(nullptr)); } };

    // Where the browser goes to log in. `state` comes back with the code.
    [[nodiscard]] std::string authorize_url(std::string_view state) const;

    // Trades the code the login brought back for tokens, and saves them.
    StravaResult<void> exchange_code(std::string_view code);

    // The activities that started between `after` and `before`, Unix times.
    // A page that fails after the first leaves the ones listed so far, with
    // the error in `stopped`.
    StravaResult<StravaListing> list_activities(i64 after, i64 before);

    // An activity's description: empty if it has none.
    StravaResult<std::string> description(u64 id);

private:
    StravaResult<void>         refresh_if_expiring();
    StravaResult<void>         request_tokens(std::vector<std::pair<std::string, std::string>> const &form);
    StravaResult<HttpResponse> get(std::string const &path);
};

}
