/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <functional>
#include <string>
#include <vector>

#include <Error.h>

namespace ST {

struct HttpRequest {
    std::string              method { "GET" };
    std::string              url { };
    std::vector<std::string> headers { }; // As "Name: value"
};

// Any status: a 404 or 500 is a response, not an error.
struct HttpResponse {
    long        status { 0 };
    std::string body { };
};

// Code that talks to a web service takes one of these, so that tests can
// give it canned responses.
using HttpFetch = std::function<std::expected<HttpResponse, LibCError>(HttpRequest const &)>;

// Sends the request with libcurl, and returns the response with its body
// in memory. An error is a request that got no response at all.
std::expected<HttpResponse, LibCError> http_fetch(HttpRequest const &request);

}
