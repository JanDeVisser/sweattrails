/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A Strava with canned responses, for the tests: nothing here talks to
 * Strava.
 */

#pragma once

#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <Http.h>
#include <OAuth.h>
#include <storage/Strava.h>

#include <TempDir.h>

namespace ST::Test {

// 2026-10-29T12:05:00Z.
constexpr i64 Now = 1793275500;

// Strava, as far as the tests need it: answers by the request's path,
// without its query, in the order the answers were given, and remembers
// the requests. Out of answers, the list of activities is empty, and
// anything else isn't found.
struct FakeStrava {
    std::map<std::string, std::vector<HttpResponse>> responses { };
    std::vector<HttpRequest>                         requests { };

    void answer(std::string const &path, HttpResponse response)
    {
        responses[path].push_back(std::move(response));
    }

    HttpFetch fetch()
    {
        return [this](HttpRequest const &request) -> std::expected<HttpResponse, LibCError> {
            requests.push_back(request);
            auto path = request.url.substr(StravaBaseUrl.size());
            path = path.substr(0, path.find('?'));
            auto &queue = responses[path];
            if (queue.empty()) {
                if (path == "/api/v3/athlete/activities") {
                    return HttpResponse { .status = 200, .body = "[]" };
                }
                return HttpResponse { .status = 404, .body = R"({"message":"Record Not Found"})" };
            }
            auto response = queue.front();
            queue.erase(queue.begin());
            return response;
        };
    }
};

inline OAuthTokens fresh_tokens()
{
    return OAuthTokens { .access_token = "access", .refresh_token = "refresh", .expires_at = Now + 6 * 3600 };
}

inline Strava logged_in(FakeStrava &fake, TempDir const &dir, std::optional<OAuthTokens> tokens = fresh_tokens())
{
    return Strava {
        .fetch = fake.fetch(),
        .client_id = "12345",
        .client_secret = "the-secret",
        .tokens_file = strava_tokens_file(dir.path),
        .tokens = std::move(tokens),
        .now = [] { return Now; },
    };
}

inline std::string token_response(std::string_view access, std::string_view refresh)
{
    return std::format(R"({{"token_type":"Bearer","expires_at":{},"expires_in":21600,"refresh_token":"{}","access_token":"{}","athlete":{{"id":1}}}})",
        Now + 21600, refresh, access);
}

inline std::string strava_activity(u64 id, std::string_view start_date, std::string_view name)
{
    return std::format(R"({{"id":{},"name":"{}","start_date":"{}","elapsed_time":3600,"sport_type":"Ride"}})", id, name, start_date);
}

inline std::string list_of(std::vector<std::string> const &activities)
{
    std::string list = "[";
    for (auto const &a : activities) {
        list += (list.size() > 1 ? "," : "") + a;
    }
    return list + "]";
}

}
