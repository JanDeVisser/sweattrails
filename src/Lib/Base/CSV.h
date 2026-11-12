/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Reads comma-separated values, as RFC 4180 has them, such as the list of
 * activities in Strava's account export.
 */

#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Error.h>

namespace ST {

using CSVRow = std::vector<std::string>;

struct CSV {
    CSVRow              headings { };
    std::vector<CSVRow> rows { };

    // The column with this heading, the first if there are several.
    [[nodiscard]] std::optional<size_t> column(std::string_view heading) const;

    // The first record is the headings. A field may be quoted, with "" for
    // a quote and line breaks in it. Records end in LF or CRLF, and blank
    // lines are skipped. A UTF-8 byte order mark at the start is skipped.
    // A quote that isn't closed, or text after a closing quote, is an
    // error, as is a text without headings. A quote inside a field that
    // isn't quoted is kept as it is.
    static std::expected<CSV, LibCError> parse(std::string_view text);
};

}
