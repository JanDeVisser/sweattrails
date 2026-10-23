/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>
#include <memory>
#include <mutex>

#include <curl/curl.h>

#include <Http.h>

namespace ST {

namespace {

size_t append_body(char *data, size_t size, size_t count, void *body)
{
    static_cast<std::string *>(body)->append(data, size * count);
    return size * count;
}

}

std::expected<HttpResponse, LibCError> http_fetch(HttpRequest const &request)
{
    static std::once_flag initialized;
    std::call_once(initialized, [] { curl_global_init(CURL_GLOBAL_ALL); });

    // A handle per request: requests are few, and it keeps this safe to call
    // from any thread.
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl { curl_easy_init(), curl_easy_cleanup };
    if (!curl) {
        return std::unexpected(LibCError { std::string { "Could not initialize libcurl" } });
    }
    curl_slist *headers = nullptr;
    for (auto const &header : request.headers) {
        headers = curl_slist_append(headers, header.c_str());
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list { headers, curl_slist_free_all };

    HttpResponse response;
    curl_easy_setopt(curl.get(), CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, request.method.c_str());
    if (request.method == "POST") {
        // An empty body: without one, libcurl would wait for one on stdin.
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, "");
    }
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header_list.get());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "sweattrails/1.0");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body);

    if (auto const result = curl_easy_perform(curl.get()); result != CURLE_OK) {
        return std::unexpected(LibCError { std::format("{} {}: {}", request.method, request.url, curl_easy_strerror(result)) });
    }
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

}
