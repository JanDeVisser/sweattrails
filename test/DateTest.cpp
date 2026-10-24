/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Date.h>

#include <TestApp.h>

using namespace ST;

namespace {

void test_parse_iso8601()
{
    CHECK_EQ(DateTime::parse_iso8601("2015-08-12T09:00:00Z").value().timestamp, 1439370000u);
    CHECK_EQ(DateTime::parse_iso8601("20150812T090000Z").value().timestamp, 1439370000u);
    CHECK(!DateTime::parse_iso8601("2015-08-12 09:00:00").has_value());
}

void test_parse_iso8601_drops_fractions()
{
    // As Wahoo writes them.
    CHECK_EQ(DateTime::parse_iso8601("2015-08-12T09:00:00.000Z").value().timestamp, 1439370000u);
    CHECK_EQ(DateTime::parse_iso8601("2015-08-12T09:00:00.9Z").value().timestamp, 1439370000u);
    CHECK(!DateTime::parse_iso8601("2015-08-12T09:00:00.Z").has_value());
    CHECK(!DateTime::parse_iso8601("2015-08-12T09:00:00.1x0Z").has_value());
}

}

int main()
{
    test_parse_iso8601();
    test_parse_iso8601_drops_fractions();
    return ST::Test::test_result();
}
