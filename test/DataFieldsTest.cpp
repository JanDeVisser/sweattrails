/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests what the activity's data panel shows. The ranges that
 * Segment::analyze() builds give each maximum the record where it happened,
 * which the panel marks when the maximum is clicked.
 */

#include <optional>
#include <vector>

#include <Range.h>
#include <storage/Activity.h>

#include <TestApp.h>

using namespace ST;

namespace {

void test_range_max_at_is_the_index_passed()
{
    Range<i16> range;
    range.extend(5, 10);
    range.extend(9, 11);
    range.extend(3, 13);
    range.extend(7, 17);
    CHECK_EQ(range.range->max_at, 11uz);
    CHECK_EQ(range.range->min_at, 13uz);
}

void test_range_ties_go_to_the_first_record()
{
    Range<i16> range;
    range.extend(5, 0);
    range.extend(9, 1);
    range.extend(9, 2);
    range.extend(3, 3);
    range.extend(3, 4);
    CHECK_EQ(range.range->max_at, 1uz);
    CHECK_EQ(range.range->min_at, 3uz);
}

// The smoothed value is for the record that completes the window.
void test_smoothed_range_max_at()
{
    SmoothedRange<u16, 3> range;
    for (auto [value, at] : std::vector<std::pair<u16, size_t>> { { 10, 0 }, { 10, 1 }, { 40, 2 }, { 40, 3 }, { 10, 4 } }) {
        range.extend(value, at);
    }
    // Windows end at 2 (average 20), 3 (30) and 4 (30).
    CHECK_EQ(range.range->max, 30);
    CHECK_EQ(range.range->max_at, 3uz);
    CHECK_EQ(range.range->min, 20);
    CHECK_EQ(range.range->min_at, 2uz);
}

record with_hr(std::optional<u8> heart_rate)
{
    return record { .heart_rate = heart_rate };
}

// Records without HR, as in a sensor dropout, don't shift max_at off the
// record index.
void test_analyze_max_at_is_a_record_index()
{
    std::vector<record> records {
        with_hr(100),
        with_hr({ }),
        with_hr(110),
        with_hr(160),
        with_hr({ }),
        with_hr(170),
        with_hr(120),
    };
    Segment segment;
    segment.analyze(records, true);
    // HR samples are records 0, 2, 3, 5 and 6. The windows end at 3 (average
    // 123), 5 (146) and 6 (150).
    CHECK(segment.computed_hr_range.range.has_value());
    CHECK_EQ(segment.computed_hr_range.max(), 150);
    CHECK_EQ(segment.computed_hr_range.range->max_at, 6uz);
}

}

int main()
{
    test_range_max_at_is_the_index_passed();
    test_range_ties_go_to_the_first_record();
    test_smoothed_range_max_at();
    test_analyze_max_at_is_a_record_index();
    return ST::Test::test_result();
}
