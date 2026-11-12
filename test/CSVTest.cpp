/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <string_view>
#include <vector>

#include <CSV.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

// The rows of `text`, after its headings, or none if it doesn't parse.
std::vector<CSVRow> rows_of(std::string_view text)
{
    auto const csv = CSV::parse(text);
    CHECK(csv.has_value());
    return csv ? csv->rows : std::vector<CSVRow> { };
}

void check_row(std::vector<CSVRow> const &rows, size_t ix, CSVRow const &expected)
{
    CHECK(ix < rows.size());
    if (ix >= rows.size()) {
        return;
    }
    CHECK_EQ(rows[ix].size(), expected.size());
    for (size_t field = 0; field < std::min(rows[ix].size(), expected.size()); ++field) {
        CHECK_EQ(rows[ix][field], expected[field]);
    }
}

void test_plain_fields()
{
    auto const csv = CSV::parse("ID,Name\n1,Ride\n2,Run\n");
    CHECK(csv.has_value());
    if (!csv) {
        return;
    }
    CHECK_EQ(csv->headings.size(), 2uz);
    CHECK_EQ(csv->headings[1], "Name");
    CHECK_EQ(csv->rows.size(), 2uz);
    check_row(csv->rows, 0, { "1", "Ride" });
    check_row(csv->rows, 1, { "2", "Run" });
}

void test_quoted_fields()
{
    auto const rows = rows_of("ID,Description\n"
                              "1,\"Windy, wet\"\n"
                              "2,\"The \"\"Big\"\" Loop\"\n"
                              "3,\"Line one\nline two\"\n"
                              "4,\"Line one\r\nline two\"\n");
    CHECK_EQ(rows.size(), 4uz);
    check_row(rows, 0, { "1", "Windy, wet" });
    check_row(rows, 1, { "2", "The \"Big\" Loop" });
    check_row(rows, 2, { "3", "Line one\nline two" });
    check_row(rows, 3, { "4", "Line one\r\nline two" });
}

void test_line_ends()
{
    auto const rows = rows_of("ID,Name\r\n1,Ride\r\n2,Run");
    CHECK_EQ(rows.size(), 2uz);
    check_row(rows, 0, { "1", "Ride" });
    check_row(rows, 1, { "2", "Run" });
}

void test_blank_lines_are_skipped()
{
    auto const rows = rows_of("ID,Name\n\n1,Ride\n\r\n\n");
    CHECK_EQ(rows.size(), 1uz);
    check_row(rows, 0, { "1", "Ride" });
}

void test_empty_fields()
{
    auto const rows = rows_of("A,B,C\n,,\n\"\",x,\"\"\n1,2,");
    CHECK_EQ(rows.size(), 3uz);
    check_row(rows, 0, { "", "", "" });
    check_row(rows, 1, { "", "x", "" });
    check_row(rows, 2, { "1", "2", "" });
}

void test_quote_inside_a_plain_field()
{
    auto const rows = rows_of("A,B\n5'11\",x\n");
    check_row(rows, 0, { "5'11\"", "x" });
}

void test_byte_order_mark()
{
    auto const csv = CSV::parse("\xEF\xBB\xBF"
                                "Activity ID,Name\n1,Ride\n");
    CHECK(csv.has_value());
    if (csv) {
        CHECK_EQ(csv->column("Activity ID").value_or(99), 0uz);
    }
}

void test_column()
{
    auto const csv = CSV::parse("Activity ID,Distance,Elapsed Time,Distance\n");
    CHECK(csv.has_value());
    if (!csv) {
        return;
    }
    CHECK_EQ(csv->column("Distance").value_or(99), 1uz);
    CHECK_EQ(csv->column("Elapsed Time").value_or(99), 2uz);
    CHECK(!csv->column("Activity Description").has_value());
    CHECK(csv->rows.empty());
}

void test_unclosed_quote()
{
    auto const csv = CSV::parse("ID,Description\n1,ok\n2,\"never\nclosed\n");
    CHECK(!csv.has_value());
    if (!csv) {
        CHECK_EQ(csv.error().description, "The quote that opens a field on line 3 isn't closed");
    }
}

void test_text_after_closing_quote()
{
    auto const csv = CSV::parse("ID,Description\n1,\"two\nlines\"\n2,\"quoted\" not\n");
    CHECK(!csv.has_value());
    if (!csv) {
        CHECK_EQ(csv.error().description, "There is text after a field's closing quote on line 4");
    }
}

void test_quoted_field_at_the_end()
{
    // No line end after the last record, which ends in a quoted field, or
    // in a quoted field followed by a comma.
    auto rows = rows_of("a,b\n1,\"x\r\ny\"");
    CHECK_EQ(rows.size(), 1uz);
    check_row(rows, 0, { "1", "x\r\ny" });
    rows = rows_of("a,b\n\"\",\"\"\"\"\n\"q\",");
    CHECK_EQ(rows.size(), 2uz);
    check_row(rows, 0, { "", "\"" });
    check_row(rows, 1, { "q", "" });
}

void test_empty_text()
{
    CHECK(!CSV::parse("").has_value());
    CHECK(!CSV::parse("\n\r\n").has_value());
}

}

int main()
{
    test_plain_fields();
    test_quoted_fields();
    test_line_ends();
    test_blank_lines_are_skipped();
    test_empty_fields();
    test_quote_inside_a_plain_field();
    test_byte_order_mark();
    test_column();
    test_unclosed_quote();
    test_text_after_closing_quote();
    test_quoted_field_at_the_end();
    test_empty_text();
    return test_result();
}
