/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdlib>
#include <ctime>

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

// In local time, as the user's clock says it.
void test_clock_time()
{
    setenv("TZ", "America/Toronto", 1);
    tzset();
    // 2026-10-29T12:05:00Z, 08:05 in Toronto in summer time.
    CHECK_EQ(clock_time(1793275500), "08:05");
    // 2026-11-11T14:00:00Z, 09:00 in winter time.
    CHECK_EQ(clock_time(1794405600), "09:00");
}

}

int main()
{
    test_parse_iso8601();
    test_parse_iso8601_drops_fractions();
    test_clock_time();
    return ST::Test::test_result();
}
