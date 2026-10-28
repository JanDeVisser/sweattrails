/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cctype>
#include <format>
#include <memory>
#include <mutex>

#include <curl/curl.h>

#include <StringUtil.h>

#include <Http.h>

namespace ST {

namespace {

size_t append_body(char *data, size_t size, size_t count, void *body)
{
    static_cast<std::string *>(body)->append(data, size * count);
    return size * count;
}

// libcurl calls this for each header line, the status line included.
size_t append_header(char *data, size_t size, size_t count, void *response)
{
    auto const line = std::string_view { data, size * count };
    auto      *headers = &static_cast<HttpResponse *>(response)->headers;
    // After a redirect, only the last response's headers count.
    if (line.starts_with("HTTP/")) {
        headers->clear();
        return size * count;
    }
    if (auto const colon = line.find(':'); colon != std::string_view::npos) {
        headers->emplace_back(
            std::string { strip(line.substr(0, colon)) },
            std::string { strip(line.substr(colon + 1)) });
    }
    return size * count;
}

}

std::optional<std::string> HttpResponse::header(std::string_view name) const
{
    for (auto const &[header_name, value] : headers) {
        if (string_compare_ic(header_name, name)) {
            return value;
        }
    }
    return { };
}

std::string url_encode(std::string_view text)
{
    std::string encoded;
    for (auto const c : text) {
        auto const u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '.' || c == '_' || c == '~') {
            encoded += c;
        } else {
            encoded += std::format("%{:02X}", u);
        }
    }
    return encoded;
}

std::string form_encode(std::vector<std::pair<std::string, std::string>> const &fields)
{
    std::string encoded;
    for (auto const &[name, value] : fields) {
        if (!encoded.empty()) {
            encoded += '&';
        }
        encoded += std::format("{}={}", url_encode(name), url_encode(value));
    }
    return encoded;
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
        // Even an empty body: without one, libcurl would wait for one on
        // stdin.
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header_list.get());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, UserAgent);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, append_header);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &response);

    if (auto const result = curl_easy_perform(curl.get()); result != CURLE_OK) {
        return std::unexpected(LibCError { std::format("{} {}: {}", request.method, request.url, curl_easy_strerror(result)) });
    }
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

}
