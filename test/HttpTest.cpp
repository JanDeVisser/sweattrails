/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the HTTP helpers that don't send anything.
 */

#include <Http.h>

#include <TestApp.h>

using namespace ST;

namespace {

void test_url_encode()
{
    CHECK_EQ(url_encode("abc-XYZ_0.9~"), "abc-XYZ_0.9~");
    CHECK_EQ(url_encode("https://localhost:8090/callback"), "https%3A%2F%2Flocalhost%3A8090%2Fcallback");
    CHECK_EQ(url_encode("workouts_read user_read"), "workouts_read%20user_read");
    CHECK_EQ(url_encode("\xC3\xA9"), "%C3%A9");
}

void test_form_encode()
{
    CHECK_EQ(form_encode({ }), "");
    CHECK_EQ(form_encode({ { "grant_type", "authorization_code" }, { "code", "a b&c" } }),
        "grant_type=authorization_code&code=a%20b%26c");
}

void test_multipart_encode()
{
    // A FIT file's header has NULs.
    std::string const fit { "\x0e\x10\0\x01", 4 };
    auto const        body = multipart_encode("XyZ", {
                                                         { .name = "data_type", .value = "fit" },
                                                         { .name = "file", .value = fit, .file_name = "1793275500.fit" },
                                                     });
    CHECK_EQ(body,
        std::string { "--XyZ\r\n"
                      "Content-Disposition: form-data; name=\"data_type\"\r\n\r\n"
                      "fit\r\n"
                      "--XyZ\r\n"
                      "Content-Disposition: form-data; name=\"file\"; filename=\"1793275500.fit\"\r\n"
                      "Content-Type: application/octet-stream\r\n\r\n" }
            + fit + "\r\n--XyZ--\r\n");
    CHECK_EQ(multipart_encode("XyZ", { }), "--XyZ--\r\n");
    CHECK_EQ(multipart_content_type("XyZ"), "Content-Type: multipart/form-data; boundary=XyZ");
}

void test_header_ignores_case()
{
    HttpResponse const response {
        .status = 429,
        .headers = { { "Content-Type", "application/json" }, { "X-RateLimit-Reset", "300" } },
    };
    CHECK_EQ(response.header("x-ratelimit-reset").value_or(""), "300");
    CHECK_EQ(response.header("X-RATELIMIT-RESET").value_or(""), "300");
    CHECK(!response.header("X-RateLimit-Limit").has_value());
}

}

int main()
{
    test_url_encode();
    test_form_encode();
    test_multipart_encode();
    test_header_ignores_case();
    return ST::Test::test_result();
}
