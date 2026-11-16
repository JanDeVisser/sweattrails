/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdlib>
#include <ctime>

#include <Date.h>
#include <Environment.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

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
    set_time_zone("America/Toronto");
    // 2026-10-29T12:05:00Z, 08:05 in Toronto in summer time.
    CHECK_EQ(clock_time(1793275500), "08:05");
    // 2026-11-11T14:00:00Z, 09:00 in winter time.
    CHECK_EQ(clock_time(1794405600), "09:00");
}

// utc_tm() and utc_time() are each other's inverse, whatever the zone.
void test_utc_tm_and_utc_time()
{
    set_time_zone("America/Toronto");
    // 2024-02-29T23:59:58Z, a Thursday, the 60th day of a leap year.
    auto const leap_day = utc_tm(1709251198);
    CHECK_EQ(leap_day.tm_year, 124);
    CHECK_EQ(leap_day.tm_mon, 1);
    CHECK_EQ(leap_day.tm_mday, 29);
    CHECK_EQ(leap_day.tm_hour, 23);
    CHECK_EQ(leap_day.tm_wday, 4);
    CHECK_EQ(leap_day.tm_yday, 59);
    CHECK_EQ(utc_time(leap_day), 1709251198);
    // Fields that overflow carry over.
    auto next = leap_day;
    next.tm_sec += 5;
    CHECK_EQ(utc_time(next), 1709251203);
    CHECK_EQ(utc_tm(utc_time(next)).tm_mon, 2);
    // Local time, in summer and in winter time.
    CHECK_EQ(local_tm(1793275500).tm_hour, 8);
    CHECK_EQ(local_tm(1794405600).tm_hour, 9);
}

void test_utc_offset()
{
    set_time_zone("America/Toronto");
    // 2026-10-29T12:05:00Z, in summer time, and 2026-11-11T14:00:00Z.
    CHECK_EQ(utc_offset(1793275500), -4 * 3600);
    CHECK_EQ(utc_offset(1794405600), -5 * 3600);
    set_time_zone("UTC");
    CHECK_EQ(utc_offset(1793275500), 0);
}

// The calendar is UTC's, not the local one's.
void test_parse_iso8601_calendar()
{
    set_time_zone("America/Toronto");
    auto const date = DateTime::parse_iso8601("2024-03-01T02:30:15Z").value();
    CHECK_EQ(date.timestamp, 1709260215u);
    CHECK_EQ(static_cast<int>(date.day_of_month), 1);
    CHECK_EQ(static_cast<int>(date.day_of_week), 5);
    CHECK_EQ(static_cast<int>(date.day_of_year), 60);
    CHECK_EQ(static_cast<int>(date.hour), 2);
}

}

int main()
{
    test_utc_tm_and_utc_time();
    test_utc_offset();
    test_parse_iso8601_calendar();
    test_parse_iso8601();
    test_parse_iso8601_drops_fractions();
    test_clock_time();
    return ST::Test::test_result();
}
