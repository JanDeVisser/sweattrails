/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdlib>
#include <ctime>
#include <format>
#include <system_error>

#include <IO.h>
#include <StringUtil.h>
#include <Zip.h>

#include <storage/Garmin.h>
#include <storage/Storage.h>

namespace ST {

namespace {

constexpr std::string_view TokenPath = "/personalapitoken-service/personal-api/personal-api-key/token";
constexpr std::string_view ActivitiesPath = "/activitylist-service/activities/search/activities";
constexpr std::string_view DownloadPath = "/download-service/files/activity/";

// Activities asked for per request. Garmin's client uses 20 by default,
// and the server may send fewer than asked for.
constexpr size_t PageSize = 100;

GarminError failed(std::string message)
{
    return GarminError { .kind = GarminError::Kind::Failed, .message = std::move(message) };
}

GarminError rejected()
{
    return GarminError {
        .kind = GarminError::Kind::KeyRejected,
        .message = "Garmin Connect rejected the API key: it may have expired. "
                   "Create a new one in Garmin Connect's settings, under Personal API.",
    };
}

bool is_rejection(long status)
{
    return status == 401 || status == 403;
}

// Writes `contents` to `path` by way of `<path>.part`, so the inbox never
// sees half a file.
GarminResult<void> write_whole_file(fs::path const &path, std::string_view contents)
{
    auto const part = fs::path { path.string() + ".part" };
    if (auto const written = write_file_by_name(part.string(), contents); !written) {
        return std::unexpected(failed(std::format("Could not write `{}`: {}", part.string(), written.error().to_string())));
    }
    std::error_code ec { };
    fs::rename(part, path, ec);
    if (ec) {
        fs::remove(part, ec);
        return std::unexpected(failed(std::format("Could not write `{}`: {}", path.string(), LibCError { ec }.to_string())));
    }
    return { };
}

}

fs::path garmin_key_file()
{
    auto const *home = std::getenv("HOME");
    return fs::path { (home != nullptr) ? home : "" } / ".config" / "garmin" / "connect-personal-api-key";
}

std::optional<std::string> read_api_key(std::optional<std::string_view> variable, fs::path const &file)
{
    // An empty variable is as good as none.
    if (variable && !strip(*variable).empty()) {
        return std::string { strip(*variable) };
    }
    std::error_code ec { };
    if (!fs::exists(file, ec)) {
        return { };
    }
    auto const permissions = fs::status(file, ec).permissions();
    if (!ec && (permissions & (fs::perms::group_read | fs::perms::others_read)) != fs::perms::none) {
        warning(Garmin, "`{}` can be read by other users: `chmod 600` it", file.string());
    }
    auto const contents = read_file_by_name(file.string());
    if (!contents) {
        warning(Garmin, "Could not read `{}`: {}", file.string(), contents.error().to_string());
        return { };
    }
    auto const key = std::string { strip(*contents) };
    return key.empty() ? std::optional<std::string> { } : key;
}

Decoded<std::vector<GarminActivity>> parse_activity_list(JSONValue const &list)
{
    if (!list.is_array()) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, "the activity list isn't an array" });
    }
    std::vector<GarminActivity> activities;
    for (auto const &entry : list) {
        auto const id = TRY_EVAL(entry.try_get<u64>("activityId"));
        // Milliseconds since 1970.
        auto const begin = TRY_EVAL(entry.try_get<i64>("beginTimestamp"));
        auto const manual = entry.try_get<bool>("manualActivity").value_or(false);
        activities.push_back(GarminActivity {
            .id = id,
            .start_time = begin / 1000,
            .has_file = !manual,
            .summary = entry,
        });
    }
    return activities;
}

std::string garmin_start_date(int days, i64 now)
{
    auto const start = static_cast<time_t>(now - static_cast<i64>(days) * 24 * 60 * 60);
    std::tm    tm { };
    localtime_r(&start, &tm);
    return std::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

GarminDownloadRequest make_download_request(JSONValue const &settings, JSONValue const &arguments)
{
    GarminDownloadRequest request;
    if (settings.is_object()) {
        if (auto const garmin = settings.get("garmin"); garmin && garmin->is_object()) {
            request.days = garmin->try_get<int>("days").value_or(request.days);
        }
    }
    if (arguments.is_object()) {
        request.days = arguments.try_get<int>("days").value_or(request.days);
        request.quiet = arguments.try_get<bool>("quiet").value_or(false);
    }
    return request;
}

GarminResult<void> GarminConnect::obtain_token(std::string_view key)
{
    auto const response = fetch(HttpRequest {
        .method = "POST",
        .url = std::format("{}{}", GarminBaseUrl, TokenPath),
        .headers = { "Accept: */*", std::format("Authorization: ApiKey {}", key) },
    });
    if (!response) {
        return std::unexpected(failed(std::format("Could not reach Garmin Connect: {}", response.error().to_string())));
    }
    if (is_rejection(response->status)) {
        return std::unexpected(rejected());
    }
    if (response->status != 200) {
        return std::unexpected(failed(std::format("Garmin Connect answered HTTP {} when asked for a token", response->status)));
    }
    token = std::string { strip(response->body) };
    return { };
}

GarminResult<HttpResponse> GarminConnect::get(std::string const &path, std::string accept)
{
    auto response = fetch(HttpRequest {
        .url = std::format("{}{}", GarminBaseUrl, path),
        .headers = { std::format("Accept: {}", accept), std::format("Authorization: Bearer {}", token) },
    });
    if (!response) {
        return std::unexpected(failed(std::format("Could not reach Garmin Connect: {}", response.error().to_string())));
    }
    if (is_rejection(response->status)) {
        return std::unexpected(rejected());
    }
    if (response->status != 200) {
        return std::unexpected(failed(std::format("Garmin Connect answered HTTP {} for `{}`", response->status, path)));
    }
    return std::move(*response);
}

GarminResult<std::vector<GarminActivity>> GarminConnect::list_activities(std::string_view start_date)
{
    std::vector<GarminActivity> activities;
    // Until a page comes back empty: a short page doesn't mean the last one,
    // if the server sends fewer than asked for.
    for (size_t start = 0;;) {
        auto const path = std::format("{}?start={}&limit={}&startDate={}", ActivitiesPath, start, PageSize, start_date);
        auto const response = get(path, "application/json");
        if (!response) {
            return std::unexpected(response.error());
        }
        auto const json = JSONValue::deserialize(response->body);
        if (!json) {
            return std::unexpected(failed(std::format("Garmin Connect's activity list isn't JSON: {}", json.error().to_string())));
        }
        auto const page = parse_activity_list(*json);
        if (!page) {
            return std::unexpected(failed(std::format("Unexpected activity list from Garmin Connect: {}", page.error().to_string())));
        }
        if (page->empty()) {
            return activities;
        }
        activities.insert(activities.end(), page->begin(), page->end());
        start += page->size();
    }
}

GarminResult<std::string> GarminConnect::download_fit(u64 id)
{
    auto const response = get(std::format("{}{}", DownloadPath, id), "application/zip, application/x-zip-compressed, application/octet-stream");
    if (!response) {
        return std::unexpected(response.error());
    }
    auto const names = zip_entry_names(response->body);
    if (!names) {
        return std::unexpected(failed(std::format("The download of activity {}: {}", id, names.error().to_string())));
    }
    // Garmin calls it `<id>_ACTIVITY.fit`; take whichever FIT file it is.
    for (auto const &name : *names) {
        if (to_lower(fs::path { name }.extension().string()) == ".fit") {
            auto fit = unzip(response->body, name);
            if (!fit) {
                return std::unexpected(failed(std::format("The download of activity {}: {}", id, fit.error().to_string())));
            }
            return std::move(*fit);
        }
    }
    return std::unexpected(failed(std::format("The download of activity {} has no FIT file", id)));
}

GarminResult<size_t> download_new_activities(GarminConnect &garmin, Storage const &storage, std::string_view start_date, GarminProgress const &progress)
{
    auto const listed = garmin.list_activities(start_date);
    if (!listed) {
        return std::unexpected(listed.error());
    }
    std::vector<GarminActivity> wanted;
    for (auto const &activity : *listed) {
        // Downloaded before, but not organized yet: the inbox will.
        std::error_code ec { };
        auto const      in_inbox = fs::exists(storage.inbox_dir / std::format("garmin-{}.fit", activity.id), ec);
        if (activity.has_file && !in_inbox && !storage.has_activity(activity.start_time)) {
            wanted.push_back(activity);
        }
    }
    for (size_t ix = 0; ix < wanted.size(); ++ix) {
        auto const &activity = wanted[ix];
        if (progress) {
            progress(ix, wanted.size());
        }
        auto const fit = garmin.download_fit(activity.id);
        if (!fit) {
            return std::unexpected(fit.error());
        }
        // The sidecar first: the inbox takes a FIT file as soon as it's
        // there, and would leave the sidecar behind if it came second.
        auto const stem = storage.inbox_dir / std::format("garmin-{}", activity.id);
        if (auto const written = write_whole_file(stem.string() + ".json", activity.summary.serialize()); !written) {
            return std::unexpected(written.error());
        }
        if (auto const written = write_whole_file(stem.string() + ".fit", *fit); !written) {
            return std::unexpected(written.error());
        }
        trace(Garmin, "Downloaded activity {} to `{}.fit`", activity.id, stem.string());
    }
    return wanted.size();
}

}
