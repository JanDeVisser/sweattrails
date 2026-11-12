/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <CSV.h>
#include <Expected.h>

namespace ST {

std::optional<size_t> CSV::column(std::string_view heading) const
{
    auto const found = std::ranges::find(headings, heading);
    if (found == headings.end()) {
        return { };
    }
    return static_cast<size_t>(found - headings.begin());
}

namespace {

// Reads records from `text`, keeping count of the lines for errors.
struct Reader {
    std::string_view text;
    size_t           pos { 0 };
    size_t           line { 1 };

    [[nodiscard]] bool at_end() const
    {
        return pos >= text.size();
    }

    [[nodiscard]] bool at_line_end() const
    {
        return text.substr(pos).starts_with('\n') || text.substr(pos).starts_with("\r\n");
    }

    void skip_line_end()
    {
        pos += (text[pos] == '\r') ? 2 : 1;
        ++line;
    }

    std::string plain_field()
    {
        auto const start = pos;
        while (!at_end() && text[pos] != ',' && !at_line_end()) {
            ++pos;
        }
        return std::string { text.substr(start, pos - start) };
    }

    std::expected<std::string, LibCError> quoted_field()
    {
        auto const opened_on = line;
        ++pos;
        std::string field;
        while (true) {
            auto const quote = text.find('"', pos);
            if (quote == std::string_view::npos) {
                return std::unexpected(LibCError { "The quote that opens a field on line {} isn't closed", opened_on });
            }
            auto const chunk = text.substr(pos, quote - pos);
            line += std::ranges::count(chunk, '\n');
            field += chunk;
            pos = quote + 1;
            if (at_end() || text[pos] != '"') {
                return field;
            }
            field += '"';
            ++pos;
        }
    }

    // A record and the line end after it, if there is one.
    std::expected<CSVRow, LibCError> record()
    {
        CSVRow ret;
        while (true) {
            bool const quoted = text[pos] == '"';
            ret.push_back(quoted ? TRY_EVAL(quoted_field()) : plain_field());
            if (at_end()) {
                return ret;
            }
            if (at_line_end()) {
                skip_line_end();
                return ret;
            }
            if (text[pos] != ',') {
                return std::unexpected(LibCError { "There is text after a field's closing quote on line {}", line });
            }
            ++pos;
            // A comma at the very end leaves an empty last field.
            if (at_end()) {
                ret.emplace_back();
                return ret;
            }
        }
    }
};

}

std::expected<CSV, LibCError> CSV::parse(std::string_view text)
{
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    Reader reader { .text = text };
    CSV    ret;
    bool   have_headings = false;
    while (!reader.at_end()) {
        if (reader.at_line_end()) {
            reader.skip_line_end();
            continue;
        }
        auto row = TRY_EVAL(reader.record());
        if (!have_headings) {
            ret.headings = std::move(row);
            have_headings = true;
        } else {
            ret.rows.push_back(std::move(row));
        }
    }
    if (!have_headings) {
        return std::unexpected(LibCError { std::string { "There are no headings: the text is empty" } });
    }
    return ret;
}

}
