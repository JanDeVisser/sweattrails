/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava.zig.
 *
 * Assumptions on top of the ones documented in ../storage/Types.h:
 *   - `pub const Activity = strava/activity.zig.DetailedActivity` and
 *     `pub const SportType = strava/enums.zig.SportType` are re-exports for
 *     callers outside this file (storage/activity.zig uses `strava.Activity`
 *     to decode `*.strava` sidecar files). Those two files have not been
 *     translated; `ST::strava::Activity` is assumed to exist elsewhere with
 *     optional `name`/`description` members and a
 *     `static Decoded<Activity> decode(JSONValue const &)`, matching the
 *     assumption already documented in storage/Activity.h. Nothing in this
 *     file needs the type to be complete, so it is only forward-declared
 *     here.
 *   - `ST::Socket::listen(ip_address, port)` (src/IO.h) is not implemented
 *     yet (`fatal("listen(ip_addr, port) not yet implemented")` in IO.cpp),
 *     so the OAuth redirect callback server is implemented with raw POSIX
 *     sockets directly in Strava.cpp instead of going through ST::Socket.
 *   - There is no HTTP client in src/ (unlike zig's std.http.Client), so all
 *     HTTP calls shell out to `curl` via ST::Process, the same approach used
 *     in ../Map.cpp for tile fetching.
 */

#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <Error.h>
#include <Expected.h>
#include <JSON.h>

#include <Date.h>
#include <storage/strava/Activity.h>

namespace ST {

namespace fs = std::filesystem;

struct StravaError {
    enum class Code {
        InvalidConfigFile,
        NoAuthCodeReceived,
        RequestError,
        InvalidAuthenticationTokenResponse,
    };

    using ErrorVariant = std::variant<LibCError, JSONError, Code>;

    ErrorVariant error;

    StravaError(StravaError const &) = default;
    StravaError(StravaError &&) = default;
    StravaError &operator=(StravaError const &) = default;
    StravaError &operator=(StravaError &&) = default;

    template<typename E>
        requires std::constructible_from<ErrorVariant, E>
    StravaError(E const &e)
        : error(e)
    {
    }

    [[nodiscard]] std::string to_string() const;
};

template<typename T>
using StravaResult = std::expected<T, StravaError>;

struct Strava {
    static constexpr std::string_view CONFIG_PATH = "/.config/sweattrails/strava_config"; // unused, kept for parity with the zig source.
    static constexpr std::string_view STRAVA_AUTH_URL = "https://www.strava.com/oauth/authorize";
    static constexpr std::string_view STRAVA_TOKEN_URL = "https://www.strava.com/oauth/token";
    static constexpr std::string_view STRAVA_API_URL = "https://www.strava.com/api/v3";
    static constexpr int              CALLBACK_PORT = 8089;
    static constexpr std::string_view REDIRECT_URI = "http://localhost:8089/callback";

    struct Config {
        std::string client_id { };
        std::string client_secret { };
        std::string access_token { };
        std::string refresh_token { };
        u64         expires_at { 0 };
        u64         newest_synced { 0 };
        u64         oldest_synced { 0 };

        static Decoded<Config>  decode(JSONValue const &json);
        [[nodiscard]] JSONValue encode() const;
    };

    fs::path        data_dir { };
    Config          config { };
    std::deque<i64> pending { };

    // zig's `init(app: Sweattrails) !?Strava` only ever touches
    // `app.storage.data_dir`, so that is all this takes. Returns an empty
    // optional if there is no `strava.config` file yet (zig: `error.FileNotFound
    // => return null`).
    static StravaResult<std::optional<Strava>> init(fs::path const &data_dir);

    Error<StravaError>        authenticate();
    StravaResult<std::string> request_response(std::string_view const &url);
    Error<StravaError>        get_next_activity();

    template<typename T>
    StravaResult<T> request(std::string_view const &url)
    {
        auto text = TRY_EVAL(request_response(url));
        auto json = JSONValue::deserialize(text);
        if (!json) {
            return std::unexpected(StravaError { json.error() });
        }
        if constexpr (std::is_same_v<T, JSONValue>) {
            return *json;
        } else {
            auto decoded = decode<T>(*json);
            if (!decoded) {
                return std::unexpected(StravaError { decoded.error() });
            }
            return *decoded;
        }
    }

private:
    Error<StravaError> write_config();
    Error<StravaError> parse_token_response(std::string_view const &resp);
    Error<StravaError> populate_pending_list(std::string_view const &filter);
};

}
