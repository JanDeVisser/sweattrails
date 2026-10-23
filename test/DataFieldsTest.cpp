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

record with_altitude(std::optional<float32> altitude)
{
    return record { .altitude = altitude };
}

float32 climb_of(std::vector<std::optional<float32>> const &altitudes)
{
    std::vector<record> records;
    for (auto const &altitude : altitudes) {
        records.push_back(with_altitude(altitude));
    }
    Segment segment;
    segment.analyze(records, true);
    return segment.computed_climb;
}

// A rise counts once it's 3 m above the lowest point since the last rise
// that counted. Descents don't take anything off, and a record without
// altitude is skipped.
void test_analyze_climb()
{
    // 100 to 105 counts. 105 to 103 is down, 103 to 110 counts, and 90 to
    // 92.5 is less than 3 m.
    CHECK_EQ(climb_of({ 100.0f, 105.0f, 103.0f, { }, 110.0f, 90.0f, 92.5f }), 12.0f);
}

void test_analyze_climb_ignores_jitter()
{
    CHECK_EQ(climb_of({ 100.0f, 101.5f, 99.0f, 101.0f, 100.0f, 101.9f, 99.5f }), 0.0f);
}

// A climb in 1 m steps counts every 3 m: 100 to 110 gives 9 m, short of the
// top by at most 3 m.
void test_analyze_steady_climb()
{
    std::vector<std::optional<float32>> altitudes;
    for (auto altitude = 100; altitude <= 110; ++altitude) {
        altitudes.emplace_back(static_cast<float32>(altitude));
    }
    CHECK_EQ(climb_of(altitudes), 9.0f);
}

void test_climb_prefers_the_device()
{
    Segment segment;
    segment.computed_climb = 120.0f;
    CHECK_EQ(segment.climb(), 120.0f);
    segment.total_ascent = 95;
    CHECK_EQ(segment.climb(), 95.0f);
}

void test_analyze_no_climb_without_altitude()
{
    std::vector<record> records(5);
    Segment             segment;
    segment.analyze(records, true);
    CHECK_EQ(segment.computed_climb, 0.0f);
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

void set_cadence(record &rec, u8 cadence)
{
    rec.cadence = cadence;
}

// One sample per record, from record 0.
std::vector<CadenceSample> cadence_samples(std::vector<u8> const &cadences)
{
    std::vector<CadenceSample> samples;
    for (size_t ix = 0; ix < cadences.size(); ++ix) {
        samples.push_back(CadenceSample { .at = ix, .cadence = cadences[ix] });
    }
    return samples;
}

void test_cadence_average_leaves_out_zeros()
{
    auto const stats = cadence_stats(cadence_samples({ 80, 0, 0, 90 }));
    CHECK(stats.has_value());
    if (stats) {
        CHECK_EQ(stats->average, 85.0f);
    }
}

// Record 10 is a stop. The slowing down before it (40 at record 6) and the
// getting going after it (30 at record 13) are within 5 records, and don't
// count. The 60 at record 22 does: the windows ending at 22, 23 and 24
// average 80.
void test_cadence_min_skips_stops()
{
    std::vector<u8> cadences(30, 90);
    cadences[6] = 40;
    cadences[10] = 0;
    cadences[13] = 30;
    cadences[22] = 60;
    auto const stats = cadence_stats(cadence_samples(cadences));
    CHECK(stats.has_value() && stats->min.has_value());
    if (stats && stats->min) {
        CHECK_EQ(static_cast<int>(*stats->min), 80);
        CHECK_EQ(stats->min_at, 22uz);
    }
}

// Every window is within 5 records of the zero at record 3.
void test_no_cadence_min_near_stops_only()
{
    auto const stats = cadence_stats(cadence_samples({ 90, 85, 80, 0, 80, 85, 90 }));
    CHECK(stats.has_value());
    if (stats) {
        CHECK(!stats->min.has_value());
        CHECK_EQ(stats->average, 85.0f);
    }
}

void test_no_cadence_stats_without_cadence()
{
    CHECK(!cadence_stats(cadence_samples({ 0, 0, 0, 0 })).has_value());
    CHECK(!cadence_stats({ }).has_value());
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

void test_e_bike_ride_shows_speed()
{
    TestActivity t { sport::E_biking, records_with(10, set_speed, 7, 10.0f, 15.0f) };
    t.activity.segment.distance = 30000;
    CHECK_EQ(t.value("Avg Speed"), "36.0 km/h");
    CHECK_EQ(t.value("Max Speed"), "54.0 km/h");
    CHECK(!t.field("Avg Pace"));
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

// 40 records at 85 rpm, with 100 at record 20: smoothed, the windows ending
// at 20, 21 and 22 average 90. The average is (39 * 85 + 100) / 40 = 85.4,
// and the lowest window is the first, ending at record 2.
std::vector<record> cadence_records()
{
    return records_with(40, set_cadence, 20, u8 { 85 }, u8 { 100 });
}

void test_ride_cadence_in_rpm()
{
    TestActivity t { sport::Cycling, cadence_records() };
    CHECK_EQ(t.value("Avg Cadence"), "85 rpm");
    CHECK_EQ(t.value("Max Cadence"), "90 rpm");
    CHECK_EQ(t.mark_at("Max Cadence"), 20uz);
    CHECK_EQ(t.value("Min Cadence"), "85 rpm");
    CHECK_EQ(t.mark_at("Min Cadence"), 2uz);
    CHECK_EQ(t.mark_at("Avg Cadence"), TestActivity::NoMark);
}

// FIT has running cadence in strides per minute: the panel doubles it.
void test_run_cadence_in_spm()
{
    TestActivity t { sport::Running, cadence_records() };
    CHECK_EQ(t.value("Avg Cadence"), "171 spm");
    CHECK_EQ(t.value("Max Cadence"), "180 spm");
    CHECK_EQ(t.value("Min Cadence"), "170 spm");
}

void test_unrealistic_cadence_is_not_shown()
{
    TestActivity t { sport::Cycling, records_with(40, set_cadence, 0, u8 { 0 }, u8 { 9 }) };
    CHECK(!t.field("Avg Cadence"));
    CHECK(!t.field("Max Cadence"));
    CHECK(!t.field("Min Cadence"));
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
    CHECK(!t.field("Climbed"));
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

void test_which_fields_can_be_clicked()
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
            auto const clickable = field->label == "Max HR" || field->label == "Max Power" || field->label == "Max Speed"
                || field->label == "Min elevation" || field->label == "Max elevation";
            if (field->mark_at.has_value() != clickable) {
                std::println(stderr, "{} has the wrong mark_at", field->label);
            }
            CHECK_EQ(field->mark_at.has_value(), clickable);
        }
    }
}

// As test_analyze_max_at_is_a_record_index(), for power.
void test_analyze_power_max_at_is_a_record_index()
{
    std::vector<record> records(8);
    for (auto [ix, power] : std::vector<std::pair<size_t, u16>> { { 0, 100 }, { 1, 100 }, { 3, 100 }, { 4, 400 }, { 6, 100 }, { 7, 100 } }) {
        records[ix].power = power;
    }
    Segment segment;
    segment.analyze(records, true);
    // Power samples are records 0, 1, 3, 4, 6 and 7. The windows end at 3
    // (100), 4 (200), 6 (200) and 7 (100).
    CHECK_EQ(segment.computed_power_range.max(), 200);
    CHECK_EQ(segment.computed_power_range.range->max_at, 4uz);
    CHECK_EQ(records[4].power.value_or(0), 200);
}

// A maximum of exactly 10 is realistic.
void test_a_maximum_of_10_is_shown()
{
    TestActivity hr { sport::Running, records_with(40, set_hr, 0, u8 { 10 }, u8 { 10 }) };
    CHECK_EQ(hr.value("Max HR"), "10 bpm");

    TestActivity power { sport::Cycling, records_with(40, set_power, 0, u16 { 10 }, u16 { 10 }) };
    CHECK_EQ(power.value("Max Power"), "10 W");
    CHECK_EQ(power.value("NP"), "10 W");
}

// One 3000 W spike in 4000 records of 0 W: the average rounds down to 0 W,
// but there is an NP. VI would divide by the average.
void test_no_vi_without_average_power()
{
    TestActivity t { sport::Cycling, records_with(4000, set_power, 2000, u16 { 0 }, u16 { 3000 }) };
    CHECK_EQ(t.value("Avg Power"), "0 W");
    CHECK_EQ(t.value("Max Power"), "1000 W");
    CHECK_EQ(t.mark_at("Max Power"), 2000uz);
    CHECK_EQ(t.value("NP"), "29 W");
    CHECK_EQ(t.value("IF"), "0.11");
    CHECK(!t.field("VI"));
}

// With a distance but no moving time and no speed samples, there's neither
// an average nor a maximum, and so no line for them.
void test_no_pace_line_without_time_or_speed()
{
    TestActivity t { sport::Running, std::vector<record>(10) };
    t.activity.segment.distance = 5000;
    t.activity.segment.moving = Duration { };
    auto const lines = data_fields(t.activity);
    CHECK_EQ(lines.size(), 2uz);
    CHECK_EQ(t.value("Distance"), "5.00 km");
    CHECK(!t.field("Avg Pace"));
    CHECK(!t.field("Max Pace"));
}

// A record without speed, as when GPS drops out, doesn't shift the mark.
void test_max_speed_with_gaps()
{
    std::vector<record> records(6);
    records[0].speed = 3.0f;
    records[2].speed = 4.0f;
    records[5].speed = 3.5f;
    TestActivity t { sport::Running, records };
    t.activity.segment.distance = 10000;
    CHECK_EQ(t.value("Max Pace"), "4:10 min/km");
    CHECK_EQ(t.mark_at("Max Pace"), 2uz);
}

void test_elevation()
{
    std::vector<record> records(5);
    for (auto [ix, altitude] : std::vector<std::pair<size_t, float32>> { { 0, 120.4f }, { 1, 80.9f }, { 3, 250.0f }, { 4, 100.0f } }) {
        records[ix].altitude = altitude;
    }
    TestActivity t { sport::Running, records };
    CHECK_EQ(t.value("Min elevation"), "80 m");
    CHECK_EQ(t.value("Max elevation"), "250 m");
    CHECK_EQ(t.mark_at("Min elevation"), 1uz);
    CHECK_EQ(t.mark_at("Max elevation"), 3uz);
    // Only 80.9 m to 250 m is a climb.
    CHECK_EQ(t.value("Climbed"), "169 m");
    CHECK_EQ(t.mark_at("Climbed"), TestActivity::NoMark);
}

// A pace of a day or more per km, as for a gym session where the watch
// recorded a few meters, lost the days: 5 m in 50:00 is 10000 min/km.
void test_pace_of_more_than_a_day()
{
    TestActivity t { sport::Training, std::vector<record>(10) };
    t.activity.segment.distance = 5;
    CHECK_EQ(t.value("Avg Pace"), "10000:00 min/km");
}

}

int main()
{
    test_range_max_at_is_the_index_passed();
    test_range_ties_go_to_the_first_record();
    test_smoothed_range_max_at();
    test_analyze_max_at_is_a_record_index();
    test_analyze_climb();
    test_analyze_climb_ignores_jitter();
    test_analyze_steady_climb();
    test_climb_prefers_the_device();
    test_cadence_average_leaves_out_zeros();
    test_cadence_min_skips_stops();
    test_no_cadence_min_near_stops_only();
    test_no_cadence_stats_without_cadence();
    test_ride_cadence_in_rpm();
    test_run_cadence_in_spm();
    test_unrealistic_cadence_is_not_shown();
    test_analyze_no_climb_without_altitude();
    test_ride_shows_speed();
    test_e_bike_ride_shows_speed();
    test_run_shows_pace();
    test_heart_rate();
    test_power();
    test_no_np_with_few_power_samples();
    test_unrealistic_maxima_are_not_shown();
    test_only_times_without_data();
    test_moving_time_of_an_hour();
    test_which_fields_can_be_clicked();
    test_analyze_power_max_at_is_a_record_index();
    test_a_maximum_of_10_is_shown();
    test_no_vi_without_average_power();
    test_no_pace_line_without_time_or_speed();
    test_max_speed_with_gaps();
    test_elevation();
    test_pace_of_more_than_a_day();
    return ST::Test::test_result();
}
