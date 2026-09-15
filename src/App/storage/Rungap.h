/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/rungap.zig.
 *
 * There is no HTTP client in src/ (unlike zig's std.http.Client), so the
 * request in get_activity_data() shells out to `curl` via ST::Process, same
 * as ../Map.cpp and ../connect/Strava.cpp.
 */

#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <time.h>
#include <variant>

#include <Error.h>
#include <Expected.h>
#include <JSON.h>

// #include "../Date.h"
#include <storage/Types.h>

namespace ST {

struct Storage;

constexpr std::string_view RUNGAP_API_URL = "https://vps-86bf504e.vps.ovh.ca";

struct RungapError {
    enum class Code {
        RequestError,
        InvalidFITFile,
    };

    using ErrorVariant = std::variant<LibCError, JSONError, StorageError, Code>;

    ErrorVariant error;

    RungapError(RungapError const &) = default;
    RungapError(RungapError &&) = default;
    RungapError &operator=(RungapError const &) = default;
    RungapError &operator=(RungapError &&) = default;

    template<typename E>
        requires std::constructible_from<ErrorVariant, E>
    RungapError(E const &e)
        : error(e)
    {
    }

    [[nodiscard]] std::string to_string() const;
};

template<typename T>
using RungapResult = std::expected<T, RungapError>;

struct Rungap {
    struct Config {
        std::string access_token { };
        i64         newest_synced { 0 };
        i64         oldest_synced { 0 };

        static Decoded<Config>  decode(JSONValue const &json);
        [[nodiscard]] JSONValue encode() const;
    };

    Config config { };

    // zig's `init(app: Sweattrails) !?Rungap` only ever touches
    // `app.storage.data_dir`, so that is all this takes. Returns an empty
    // optional if there is no `rungap.config` file yet (zig:
    // `error.FileNotFound => return null`). Unlike Strava, Rungap does not
    // keep the data directory around; every method that needs it is handed
    // the Storage explicitly, matching the zig source.
    static RungapResult<std::optional<Rungap>> init(fs::path const &data_dir);

    RungapResult<std::optional<u32>> get_activity(Storage &storage, std::string_view const &query);
    Error<RungapError>               get_next_activity(Storage &storage);

private:
    Error<RungapError> write_config(Storage &storage);
};

}
