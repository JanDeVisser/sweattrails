/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Downloads activities from Garmin Connect, with Garmin's Personal API:
 * see specs/07-garmin-download.md. The user creates an API key on the Garmin
 * Connect website; the key buys a bearer token, valid for a day, for the
 * other requests.
 */

#pragma once

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

namespace ST {

namespace fs = std::filesystem;

struct Storage;

constexpr std::string_view GarminBaseUrl = "https://personal.connectapi.garmin.com";

// Where Garmin's own client looks for the key, so the two share it.
constexpr char const *GarminKeyVariable = "GARMIN_PERSONAL_API_KEY";
fs::path              garmin_key_file();

// The number of days a download looks back when the settings don't say.
constexpr int GarminDefaultDays = 30;

struct GarminError {
    enum class Kind {
        NoKey,       // There's no key: the user hasn't set up Garmin Connect.
        KeyRejected, // Expired or revoked: the user has to make a new one.
        Failed,      // Anything else: no connection, an HTTP error, bad data.
    };

    Kind        kind { Kind::Failed };
    std::string message { };
    long        status { 0 }; // The HTTP status, for an HTTP error

    [[nodiscard]] std::string to_string() const { return message; }
};

template<typename T>
using GarminResult = std::expected<T, GarminError>;

// The key from the environment variable's value if it has one, else from
// `file`, without surrounding whitespace. Nothing if neither has one.
std::optional<std::string> read_api_key(std::optional<std::string_view> variable, fs::path const &file);

// An activity in Garmin Connect's activity list.
struct GarminActivity {
    u64       id { 0 };
    i64       start_time { 0 };  // Unix time, as the FIT session's start time
    bool      has_file { true }; // Manual activities have no FIT file
    JSONValue summary { };       // The list's entry, as it came
};

// The activities in the list. An entry without an ID or start time is
// skipped, with a warning.
Decoded<std::vector<GarminActivity>> parse_activity_list(JSONValue const &list);

// The first day a download of the last `days` days asks for, in local time,
// as Garmin Connect's YYYY-MM-DD.
std::string garmin_start_date(int days, i64 now);

struct GarminConnect {
    HttpFetch   fetch;
    std::string token { };

    // Trades the API key for a bearer token.
    GarminResult<void> obtain_token(std::string_view key);
    // The activities since `start_date`, newest first.
    GarminResult<std::vector<GarminActivity>> list_activities(std::string_view start_date);
    // The activity's FIT file, unzipped.
    GarminResult<std::string> download_fit(u64 id);

private:
    GarminResult<HttpResponse> get(std::string const &path, std::string accept);
};

// Called before each activity is downloaded, with its index and the number
// of activities to download.
using GarminProgress = std::function<void(size_t index, size_t count)>;

// Downloads the activities of the last `days` days that the store doesn't
// have into its inbox: `garmin-<id>.json`, the activity's summary, and
// `garmin-<id>.fit`. The inbox organizes them as any other file. Returns
// how many were downloaded. An activity Garmin Connect won't hand over (403
// or 404) is skipped with a warning; any other error stops the download.
// Activities downloaded before an error stay in the inbox.
GarminResult<size_t> download_new_activities(GarminConnect &garmin, Storage const &storage, std::string_view start_date, GarminProgress const &progress = { });

}
