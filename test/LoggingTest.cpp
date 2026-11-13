/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests which categories TRACE turns on. The logger reads it once, the
 * first time it's used: so there's one case, set before that.
 */

#include <cstdlib>

#include <Logging.h>

#include <TestApp.h>

using namespace ST;

namespace {

// TRACE=";Garmin,Wahoo:ST;" turns on all three, and nothing else: each
// separator works, and empty names in between are skipped.
void test_categories()
{
    setenv("TRACE", ";Garmin,Wahoo:ST;", 1);
    auto const &logger = Logger::get_logger();
    CHECK(logger.enabled("Garmin"));
    CHECK(logger.enabled("Wahoo"));
    CHECK(logger.enabled("ST"));
    CHECK(!logger.enabled("Strava"));
    CHECK(!logger.enabled(""));
    CHECK(!logger.enabled(",Wahoo"));
}

}

int main()
{
    test_categories();
    return ST::Test::test_result();
}
