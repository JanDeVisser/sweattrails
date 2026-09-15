/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/rungap.zig.
 */

#include <algorithm>
#include <cassert>
#include <format>
#include <system_error>

#include <Defer.h>
#include <IO.h>
#include <Logging.h>
#include <Process.h>
#include <StringUtil.h>

#include "../Date.h"
#include "../Storage.h"
#include "../storage/Activity.h"
#include "Rungap.h"

namespace ST {

namespace {

RungapResult<fs::path> create_dir_path_open(fs::path const &parent, std::string_view const &sub_path)
{
    auto            ret = parent / sub_path;
    std::error_code ec { };
    if (!fs::is_directory(ret)) {
        fs::create_directories(ret, ec);
        if (ec) {
            return std::unexpected(RungapError { LibCError { ec.message() } });
        }
    }
    return ret;
}

// See ../connect/Strava.cpp for why this shells out to curl with `-o`/`-w`
// writing straight to disk rather than going through ReadPipe.
RungapResult<std::optional<std::string>> get_activity_data(Rungap::Config const &config, std::string_view const &query)
{
    auto const url = std::format("{}/activity?{}", RUNGAP_API_URL, query);
    auto const stamp = ST::now();
    auto const body_file = (fs::temp_directory_path() / std::format("sweattrails-rungap-{}-body.json", stamp)).string();
    auto const status_file = (fs::temp_directory_path() / std::format("sweattrails-rungap-{}-status.txt", stamp)).string();
    auto const auth_header = std::format("Authorization: Bearer {}", config.access_token);

    Process<> curl {
        "curl",
        "-s",
        "-S",
        "-o",
        body_file,
        "-w",
        "%{http_code}",
        "-A",
        "sweattrails/1.0",
        "-H",
        auth_header,
        url,
    };
    curl.stdout_file = status_file;
    auto exit_code = curl.execute();
    if (!exit_code) {
        return std::unexpected(RungapError { exit_code.error() });
    }
    if (*exit_code != 0) {
        return std::unexpected(RungapError { LibCError { std::format("curl exited with {} fetching `{}`", *exit_code, url) } });
    }

    auto            status = read_file_by_name(status_file);
    std::error_code ec { };
    auto            cleanup_fn = [&]() {
        fs::remove(status_file, ec);
        fs::remove(body_file, ec);
    };
    Defer const cleanup { cleanup_fn };
    if (!status) {
        return std::unexpected(RungapError { status.error() });
    }
    if (*status == "200") {
        auto body = read_file_by_name(body_file);
        if (!body) {
            return std::unexpected(RungapError { body.error() });
        }
        return std::optional<std::string> { *body };
    }
    if (*status == "404") {
        return std::optional<std::string> { };
    }
    return std::unexpected(RungapError { LibCError { std::format("`{}` returned HTTP status {}", url, *status) } });
}

}

std::string RungapError::to_string() const
{
    return std::visit(
        overloaded {
            [](LibCError const &e) { return e.to_string(); },
            [](JSONError const &e) { return e.to_string(); },
            [](StorageError const &e) { return e.to_string(); },
            [](Code const &c) -> std::string {
                switch (c) {
                case Code::RequestError:
                    return "Rungap request failed";
                case Code::InvalidFITFile:
                    return "Rungap returned an invalid FIT file";
                }
                return "Unknown Rungap error";
            },
        },
        error);
}

Decoded<Rungap::Config> Rungap::Config::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, "Expected a JSON object for Rungap::Config" });
    }
    auto get_string = [&json](std::string_view const &key) -> Decoded<std::string> {
        if (auto v = json.get(key); v) {
            std::string s;
            TRY(v->convert(s));
            return s;
        }
        return std::string { };
    };
    auto get_i64 = [&json](std::string_view const &key) -> Decoded<i64> {
        if (auto v = json.get(key); v) {
            i64 n = 0;
            TRY(v->convert(n));
            return n;
        }
        return i64 { 0 };
    };

    Config ret { };
    ret.access_token = TRY_EVAL(get_string("access_token"));
    ret.newest_synced = TRY_EVAL(get_i64("newest_synced"));
    ret.oldest_synced = TRY_EVAL(get_i64("oldest_synced"));
    return ret;
}

JSONValue Rungap::Config::encode() const
{
    auto obj = JSONValue::object();
    ST::set(obj, "access_token", access_token);
    ST::set(obj, "newest_synced", newest_synced);
    ST::set(obj, "oldest_synced", oldest_synced);
    return obj;
}

RungapResult<std::optional<Rungap>> Rungap::init(fs::path const &data_dir)
{
    auto const config_path = (data_dir / "rungap.config").string();
    if (!fs::exists(config_path)) {
        return std::optional<Rungap> { };
    }
    auto text = read_file_by_name(config_path);
    if (!text) {
        return std::unexpected(RungapError { text.error() });
    }
    auto json = JSONValue::deserialize(*text);
    if (!json) {
        return std::unexpected(RungapError { json.error() });
    }
    auto config = Config::decode(*json);
    if (!config) {
        return std::unexpected(RungapError { config.error() });
    }

    Rungap ret { };
    ret.config = *config;
    return std::optional<Rungap> { std::move(ret) };
}

Error<RungapError> Rungap::write_config(Storage &storage)
{
    auto const text = std::format("{}\n", config.encode().serialize(true));
    auto       res = write_file_by_name((storage.data_dir / "rungap.config").string(), std::string_view { text });
    if (!res) {
        return std::unexpected(RungapError { res.error() });
    }
    return { };
}

RungapResult<std::optional<u32>> Rungap::get_activity(Storage &storage, std::string_view const &query)
{
    auto response = TRY_EVAL(get_activity_data(config, query));
    if (!response) {
        return std::optional<u32> { };
    }

    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    activity.depth = LoadingDepth::Shallow;
    if (TRY_EVAL(activity.load_from_slice(*response))) {
        assert(activity.segment.start_time.timestamp > 0);
        auto const dest_dir_name = std::format(
            "{:04}/{:02}",
            static_cast<u16>(activity.segment.start_time.year),
            static_cast<u8>(activity.segment.start_time.month) + 1);
        auto const dest_dir = TRY_EVAL(create_dir_path_open(storage.activity_dir, dest_dir_name));
        auto const dest = dest_dir / std::format("{}.fit", activity.segment.start_time.timestamp);
        auto       write_res = write_file_by_name(dest.string(), std::string_view { *response });
        if (!write_res) {
            return std::unexpected(RungapError { write_res.error() });
        }
        return std::optional<u32> { activity.segment.start_time.timestamp };
    }
    return std::unexpected(RungapError { RungapError::Code::InvalidFITFile });
}

Error<RungapError> Rungap::get_next_activity(Storage &storage)
{
    auto const after = (config.newest_synced > 0) ? config.newest_synced : now();
    if (auto d = TRY_EVAL(get_activity(storage, std::format("after={}", after)))) {
        config.newest_synced = std::max<i64>(*d, config.newest_synced);
        config.oldest_synced = (config.oldest_synced > 0) ? std::min<i64>(*d, config.newest_synced) : *d;
    } else {
        auto const before = (config.oldest_synced > 0) ? config.oldest_synced : now();
        if (auto d2 = TRY_EVAL(get_activity(storage, std::format("before={}", before)))) {
            config.newest_synced = std::max<i64>(*d2, config.newest_synced);
            config.oldest_synced = (config.oldest_synced > 0) ? std::min<i64>(*d2, config.newest_synced) : *d2;
        }
    }
    TRY(write_config(storage));
    return { };
}

}
