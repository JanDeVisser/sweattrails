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
#include <string>
#include <vector>

#include <Date.h>

#include <Range.h>
#include <storage/Activity.h>
#include <storage/Storage.h>
#include <widget/DataFields.h>

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

// An activity with the given records, analyzed as when it's loaded.
struct TestActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    TestActivity(sport the_sport, std::vector<record> records)
    {
        activity.sport = the_sport;
        activity.segment.moving = Duration { 3000 };
        activity.segment.elapsed = Duration { 3100 };
        activity.records = std::move(records);
        activity.segment.analyze(activity.records, true);
    }

    [[nodiscard]] std::optional<DataField> field(std::string const &label) const
    {
        for (auto const &line : data_fields(activity)) {
            for (auto const &field : { line.left, line.right }) {
                if (field && field->label == label) {
                    return field;
                }
            }
        }
        return { };
    }

    [[nodiscard]] std::string value(std::string const &label) const
    {
        auto const f = field(label);
        return f ? f->value : std::string { "<none>" };
    }

    // The record a click on the field marks, or NoMark.
    static constexpr size_t NoMark = static_cast<size_t>(-1);

    [[nodiscard]] size_t mark_at(std::string const &label) const
    {
        auto const f = field(label);
        return (f && f->mark_at) ? *f->mark_at : NoMark;
    }
};

// `count` records, where record `peak_at` has `peak` and the others `value`.
std::vector<record> records_with(size_t count, auto set, size_t peak_at, auto value, auto peak)
{
    std::vector<record> records(count);
    for (size_t ix = 0; ix < count; ++ix) {
        set(records[ix], (ix == peak_at) ? peak : value);
    }
    return records;
}

void set_speed(record &rec, float32 speed)
{
    rec.speed = speed;
}

void set_hr(record &rec, u8 heart_rate)
{
    rec.heart_rate = heart_rate;
}

void set_power(record &rec, u16 power)
{
    rec.power = power;
}

void test_ride_shows_speed()
{
    TestActivity t { sport::Cycling, records_with(10, set_speed, 7, 10.0f, 15.0f) };
    t.activity.segment.distance = 30000;
    CHECK_EQ(t.value("Distance"), "30.0 km");
    CHECK_EQ(t.value("Avg Speed"), "36.0 km/h");
    CHECK_EQ(t.value("Max Speed"), "54.0 km/h");
    CHECK_EQ(t.mark_at("Max Speed"), 7uz);
    CHECK(!t.field("Avg Pace"));
    CHECK(!t.field("Max Pace"));
}

void test_run_shows_pace()
{
    TestActivity t { sport::Running, records_with(10, set_speed, 4, 3.0f, 5.0f) };
    t.activity.segment.distance = 10000;
    CHECK_EQ(t.value("Distance"), "10.00 km");
    CHECK_EQ(t.value("Avg Pace"), "5:00 min/km");
    // 5 m/s is 200 s/km.
    CHECK_EQ(t.value("Max Pace"), "3:20 min/km");
    CHECK_EQ(t.mark_at("Max Pace"), 4uz);
    CHECK(!t.field("Avg Speed"));
}

void test_heart_rate()
{
    // Smoothed over 3 records, the peak of 180 at record 5 makes the
    // windows ending at 5, 6 and 7 average (120 + 120 + 180) / 3 = 140.
    TestActivity t { sport::Running, records_with(20, set_hr, 5, u8 { 120 }, u8 { 180 }) };
    CHECK_EQ(t.value("Max HR"), "140 bpm");
    CHECK_EQ(t.mark_at("Max HR"), 5uz);
    CHECK_EQ(t.value("Avg HR"), "123 bpm");
    CHECK_EQ(t.mark_at("Avg HR"), TestActivity::NoMark);
}

void test_power()
{
    TestActivity t { sport::Cycling, records_with(40, set_power, 20, u16 { 200 }, u16 { 200 }) };
    CHECK_EQ(t.value("Avg Power"), "200 W");
    CHECK_EQ(t.value("Max Power"), "200 W");
    CHECK_EQ(t.value("NP"), "200 W");
    // 200 W over the hard-coded FTP of 275 W.
    CHECK_EQ(t.value("IF"), "0.73");
    CHECK_EQ(t.value("VI"), "1.00");
    CHECK_EQ(t.mark_at("NP"), TestActivity::NoMark);
}

void test_no_np_with_few_power_samples()
{
    TestActivity t { sport::Cycling, records_with(30, set_power, 0, u16 { 200 }, u16 { 200 }) };
    CHECK(t.field("Avg Power").has_value());
    CHECK(!t.field("NP"));
    CHECK(!t.field("IF"));
    CHECK(!t.field("VI"));
}

void test_unrealistic_maxima_are_not_shown()
{
    TestActivity hr { sport::Running, records_with(40, set_hr, 0, u8 { 0 }, u8 { 9 }) };
    CHECK(!hr.field("Avg HR"));
    CHECK(!hr.field("Max HR"));

    TestActivity power { sport::Cycling, records_with(40, set_power, 0, u16 { 0 }, u16 { 9 }) };
    for (auto const *label : { "Avg Power", "Max Power", "NP", "IF", "VI" }) {
        CHECK(!power.field(label));
    }
}

void test_only_times_without_data()
{
    TestActivity t { sport::Running, std::vector<record>(10) };
    auto const   lines = data_fields(t.activity);
    CHECK_EQ(lines.size(), 1uz);
    CHECK_EQ(t.value("Moving Time"), "50:00");
    CHECK_EQ(t.value("Elapsed Time"), "51:40.000");
    CHECK(!t.field("Distance"));
    CHECK(!t.field("Min elevation"));
}

// It used to show only the seconds when the minutes were 0, also with
// hours: 1:00:30 showed as 30.000.
void test_moving_time_of_an_hour()
{
    TestActivity t { sport::Running, { } };
    t.activity.segment.moving = Duration { 3630 };
    CHECK_EQ(t.value("Moving Time"), "1:00:30");
    t.activity.segment.moving = Duration { 42 };
    CHECK_EQ(t.value("Moving Time"), "42.000");
}

void test_only_maxima_can_be_clicked()
{
    auto records = records_with(40, set_power, 30, u16 { 200 }, u16 { 300 });
    for (size_t ix = 0; ix < records.size(); ++ix) {
        records[ix].heart_rate = 150;
        records[ix].speed = 10.0f;
        records[ix].altitude = 100.0f;
    }
    TestActivity t { sport::Cycling, records };
    t.activity.segment.distance = 20000;
    for (auto const &line : data_fields(t.activity)) {
        for (auto const &field : { line.left, line.right }) {
            if (!field) {
                continue;
            }
            auto const is_max = field->label == "Max HR" || field->label == "Max Power" || field->label == "Max Speed";
            if (field->mark_at.has_value() != is_max) {
                std::println(stderr, "{} has the wrong mark_at", field->label);
            }
            CHECK_EQ(field->mark_at.has_value(), is_max);
        }
    }
}

}

int main()
{
    test_range_max_at_is_the_index_passed();
    test_range_ties_go_to_the_first_record();
    test_smoothed_range_max_at();
    test_analyze_max_at_is_a_record_index();
    test_ride_shows_speed();
    test_run_shows_pace();
    test_heart_rate();
    test_power();
    test_no_np_with_few_power_samples();
    test_unrealistic_maxima_are_not_shown();
    test_only_times_without_data();
    test_moving_time_of_an_hour();
    test_only_maxima_can_be_clicked();
    return ST::Test::test_result();
}
