/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Error.h>

namespace ST {

// OSM's tile policy blocks library defaults, and asks for a way to reach
// the app's makers.
constexpr char const UserAgent[] = "SweatTrails/1.0 (+https://github.com/JanDeVisser/sweattrails)";

struct HttpRequest {
    std::string              method { "GET" };
    std::string              url { };
    std::vector<std::string> headers { }; // As "Name: value"
    std::string              body { };    // For a POST
};

// Any status: a 404 or 500 is a response, not an error.
struct HttpResponse {
    long                                             status { 0 };
    std::string                                      body { };
    std::vector<std::pair<std::string, std::string>> headers { };

    // The value of the header `name`: HTTP header names ignore case.
    [[nodiscard]] std::optional<std::string> header(std::string_view name) const;
};

// Code that talks to a web service takes one of these, so that tests can
// give it canned responses.
using HttpFetch = std::function<std::expected<HttpResponse, LibCError>(HttpRequest const &)>;

// Sends the request with libcurl, and returns the response with its body
// in memory. An error is a request that got no response at all.
std::expected<HttpResponse, LibCError> http_fetch(HttpRequest const &request);

// `text` with everything but letters, digits and `-._~` percent-encoded,
// for a query string or a form.
std::string url_encode(std::string_view text);

// `name=value&...`, URL-encoded: a query string, or the body of a form.
std::string form_encode(std::vector<std::pair<std::string, std::string>> const &fields);

}
