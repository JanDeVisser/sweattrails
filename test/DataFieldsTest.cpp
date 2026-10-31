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

// At least 3 m counts: exactly 3 m does, just under doesn't. A descent
// moves the point the next rise is measured from down.
void test_analyze_climb_threshold()
{
    CHECK_EQ(climb_of({ 100.0f, 103.0f }), 3.0f);
    CHECK_EQ(climb_of({ 100.0f, 102.9f }), 0.0f);
    CHECK_EQ(climb_of({ 100.0f, 90.0f, 93.0f }), 3.0f);
    CHECK_EQ(climb_of({ 100.0f, 102.0f, 101.0f, 104.0f }), 4.0f);
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

// A lap of records [first, end), with the distance and the moving time
// that the FIT file's lap message has.
struct TestLap {
    size_t  first;
    size_t  end;
    float32 distance;
    double  moving;
};

// An activity with the given records and laps, analyzed as when it's
// loaded: the laps first, since the activity's analysis smooths the
// records.
struct TestActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    // An FTP of 275 W, and an LTHR of 170 bpm running and 160 riding.
    static ThresholdList thresholds()
    {
        return ThresholdList { Thresholds {
            .from = std::chrono::year { 2012 } / 1 / 1,
            .ftp = 275,
            .lthr_running = 170,
            .lthr_cycling = 160,
        } };
    }

    TestActivity(sport the_sport, std::vector<record> records, std::vector<TestLap> const &laps = { })
    {
        storage.thresholds = thresholds();
        activity.sport = the_sport;
        activity.segment.moving = Duration { 3000 };
        activity.segment.elapsed = Duration { 3100 };
        activity.records = std::move(records);
        for (auto const &[first, end, distance, moving] : laps) {
            Lap lap { .first_record = first, .end_record = end };
            lap.segment.distance = distance;
            lap.segment.moving = Duration { moving };
            lap.segment.elapsed = Duration { moving };
            lap.segment.analyze(std::span { activity.records }.subspan(first, end - first), false);
            activity.laps.push_back(lap);
        }
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

// One sample per record, from record `from`, without power.
std::vector<CadenceSample> cadence_samples(std::vector<u8> const &cadences, size_t from = 0)
{
    std::vector<CadenceSample> samples;
    for (size_t ix = 0; ix < cadences.size(); ++ix) {
        samples.push_back(CadenceSample { .at = from + ix, .cadence = cadences[ix] });
    }
    return samples;
}

// `count` samples of `cadence` at `power`, after the last of `samples`.
void add_samples(std::vector<CadenceSample> &samples, size_t count, u8 cadence, std::optional<u16> power = { })
{
    auto const from = samples.empty() ? 0 : samples.back().at + 1;
    for (size_t ix = 0; ix < count; ++ix) {
        samples.push_back(CadenceSample { .at = from + ix, .cadence = cadence, .power = power });
    }
}

void check_min(std::optional<CadenceStats> const &stats, float32 min, size_t min_at)
{
    CHECK(stats.has_value() && stats->min.has_value());
    if (stats && stats->min) {
        CHECK_EQ(*stats->min, min);
        CHECK_EQ(stats->min_at, min_at);
    }
}

// Below 20 isn't pedaling: the 0 and the 19 are left out, the 20 isn't.
void test_cadence_average_while_pedaling()
{
    auto const stats = cadence_stats(cadence_samples({ 80, 0, 19, 90, 20, 110 }));
    CHECK(stats.has_value());
    if (stats) {
        CHECK_EQ(stats->average, 75.0f);
    }
}

// Below 20 W isn't pedaling, whatever the cadence; a sample without power
// only needs the cadence.
void test_cadence_average_leaves_out_low_power()
{
    std::vector<CadenceSample> samples;
    add_samples(samples, 1, 90, 200);
    add_samples(samples, 1, 90, 19);
    add_samples(samples, 1, 60, 0);
    add_samples(samples, 1, 50, 20);
    add_samples(samples, 1, 70);
    auto const stats = cadence_stats(samples);
    CHECK(stats.has_value());
    if (stats) {
        CHECK_EQ(stats->average, 70.0f);
    }
}

// 60 records of 90 with 10 of 60 from record 30: the windows of 30 that
// hold all ten average (20 * 90 + 10 * 60) / 30 = 80, the first ending at
// record 39.
void test_cadence_min_over_30_records()
{
    std::vector<CadenceSample> samples;
    add_samples(samples, 30, 90);
    add_samples(samples, 10, 60);
    add_samples(samples, 20, 90);
    check_min(cadence_stats(samples), 80.0f, 39);
}

// As on the May 11 ride: coasting reads as cadence decaying over several
// seconds, then lagging behind the power when pedaling again.
void test_cadence_min_leaves_out_coasting()
{
    std::vector<CadenceSample> samples;
    add_samples(samples, 30, 90, 200);
    for (u8 const cadence : { 60, 40, 25, 15, 8, 4, 2, 1, 1 }) {
        add_samples(samples, 1, cadence, 0);
    }
    for (u8 const cadence : { 6, 9, 13 }) {
        add_samples(samples, 1, cadence, 100);
    }
    add_samples(samples, 30, 90, 200);
    check_min(cadence_stats(samples), 90.0f, 29);
}

// Also from the May 11 ride: the sensor holds its last reading, 40 rpm,
// for many seconds while coasting, at a few watts.
void test_cadence_min_leaves_out_a_held_reading()
{
    std::vector<CadenceSample> samples;
    add_samples(samples, 30, 90, 200);
    add_samples(samples, 40, 40, 3);
    add_samples(samples, 30, 90, 200);
    auto const stats = cadence_stats(samples);
    check_min(stats, 90.0f, 29);
    if (stats) {
        CHECK_EQ(stats->average, 90.0f);
    }
}

// Without power, the cadence floor is all there is: 30 records of 40 do
// count.
void test_cadence_min_without_power()
{
    std::vector<CadenceSample> samples;
    add_samples(samples, 30, 90);
    add_samples(samples, 30, 40);
    check_min(cadence_stats(samples), 40.0f, 59);
}

// A stop, or a record without cadence, breaks the run: neither side has 30
// records, and there's no minimum.
void test_no_cadence_min_across_a_break()
{
    std::vector<CadenceSample> stop;
    add_samples(stop, 20, 90);
    add_samples(stop, 1, 0);
    add_samples(stop, 20, 90);
    auto const stopped = cadence_stats(stop);
    CHECK(stopped.has_value() && !stopped->min.has_value());

    auto dropout = cadence_samples(std::vector<u8>(20, 90));
    auto after = cadence_samples(std::vector<u8>(20, 90), 21);
    dropout.insert(dropout.end(), after.begin(), after.end());
    auto const dropped = cadence_stats(dropout);
    CHECK(dropped.has_value() && !dropped->min.has_value());
}

// A run starts again after a break: the 30 records of 90 after a stop
// count, the 20 of 60 before it don't.
void test_cadence_min_after_a_break()
{
    std::vector<CadenceSample> samples;
    add_samples(samples, 20, 60);
    add_samples(samples, 1, 0);
    add_samples(samples, 30, 90);
    check_min(cadence_stats(samples), 90.0f, 50);
}

// 29 records make no window of 30, but they have an average.
void test_no_cadence_min_with_fewer_than_30_records()
{
    auto const stats = cadence_stats(cadence_samples(std::vector<u8>(29, 90)));
    CHECK(stats.has_value());
    if (stats) {
        CHECK_EQ(stats->average, 90.0f);
        CHECK(!stats->min.has_value());
    }
    auto const thirty = cadence_stats(cadence_samples(std::vector<u8>(30, 90)));
    check_min(thirty, 90.0f, 29);
}

void test_no_cadence_stats_without_pedaling()
{
    CHECK(!cadence_stats(cadence_samples({ 0, 0, 19, 0 })).has_value());
    CHECK(!cadence_stats({ }).has_value());
    std::vector<CadenceSample> coasting;
    add_samples(coasting, 10, 90, 0);
    CHECK(!cadence_stats(coasting).has_value());
}

void test_ride_shows_speed()
{
    TestActivity t { sport::Cycling, records_with(10, set_speed, 7, 10.0f, 15.0f) };
    t.activity.segment.distance = 30000;
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

// 60 records at 85 rpm, with 121 at record 50: smoothed, the windows ending
// at 50, 51 and 52 average 97. The average is (59 * 85 + 121) / 60 = 85.6,
// and the lowest 30 records are the first, ending at record 29.
std::vector<record> cadence_records()
{
    return records_with(60, set_cadence, 50, u8 { 85 }, u8 { 121 });
}

void test_ride_cadence_in_rpm()
{
    TestActivity t { sport::Cycling, cadence_records() };
    CHECK_EQ(t.value("Avg Cadence"), "86 rpm");
    CHECK_EQ(t.value("Max Cadence"), "97 rpm");
    CHECK_EQ(t.mark_at("Max Cadence"), 50uz);
    CHECK_EQ(t.value("Min Cadence"), "85 rpm");
    CHECK_EQ(t.mark_at("Min Cadence"), 29uz);
    CHECK_EQ(t.mark_at("Avg Cadence"), TestActivity::NoMark);
}

// FIT has running cadence in strides per minute: the panel doubles it.
void test_run_cadence_in_spm()
{
    TestActivity t { sport::Running, cadence_records() };
    CHECK_EQ(t.value("Avg Cadence"), "171 spm");
    CHECK_EQ(t.value("Max Cadence"), "194 spm");
    CHECK_EQ(t.value("Min Cadence"), "170 spm");
}

// Walks and hikes are doubled, as runs are.
void test_walk_and_hike_cadence_in_spm()
{
    for (auto const the_sport : { sport::Walking, sport::Hiking }) {
        TestActivity t { the_sport, cadence_records() };
        CHECK_EQ(t.value("Avg Cadence"), "171 spm");
        CHECK_EQ(t.value("Max Cadence"), "194 spm");
    }
}

// Other sports' cadence is strokes per minute, shown as recorded.
void test_rowing_cadence_as_recorded()
{
    TestActivity t { sport::Rowing, cadence_records() };
    CHECK_EQ(t.value("Avg Cadence"), "86/min");
    CHECK_EQ(t.value("Max Cadence"), "97/min");
    CHECK_EQ(t.value("Min Cadence"), "85/min");
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
    // 200 W over the FTP of 275 W.
    CHECK_EQ(t.value("IF"), "0.73");
    CHECK_EQ(t.value("VI"), "1.00");
    CHECK_EQ(t.mark_at("NP"), TestActivity::NoMark);
}

// 200 W over the moving time of 3000 s, at an IF of 200 / 275: 44. Over
// the elapsed time of 3100 s it would be 45.
void test_tss_over_moving_time()
{
    TestActivity t { sport::Cycling, records_with(40, set_power, 0, u16 { 200 }, u16 { 200 }) };
    CHECK_EQ(activity_summary(t.activity), "50:00 · TSS 44");
}

// The FTP in force on the activity's day: 250 W from 2025, which makes
// 200 W over 3000 s 53.
void test_tss_with_the_ftp_of_its_day()
{
    TestActivity t { sport::Cycling, records_with(40, set_power, 0, u16 { 200 }, u16 { 200 }) };
    t.storage.thresholds.push_back(Thresholds { .from = std::chrono::year { 2025 } / 1 / 1, .ftp = 250 });
    t.activity.segment.start_time = DateTime::from_timestamp(1750000000); // June 2025
    CHECK_EQ(activity_summary(t.activity), "50:00 · TSS 53");
    CHECK_EQ(t.value("IF"), "0.80");
}

void test_no_tss_without_thresholds()
{
    TestActivity t { sport::Cycling, records_with(40, set_power, 0, u16 { 200 }, u16 { 200 }) };
    t.storage.thresholds.clear();
    CHECK_EQ(activity_summary(t.activity), "50:00");
    CHECK(!t.field("IF"));
}

// A record a second for 3000 s, at running's LTHR of 170 bpm: 3000 s at
// threshold is 83, as for power.
std::vector<record> heart_rate_at(u8 heart_rate, size_t count, u32 every = 1)
{
    std::vector<record> records(count);
    for (auto ix = 0uz; ix < count; ++ix) {
        records[ix].timestamp = DateTime::from_timestamp(static_cast<i32>(1750000000 + ix * every));
        records[ix].heart_rate = heart_rate;
    }
    return records;
}

void test_hr_tss()
{
    TestActivity run { sport::Running, heart_rate_at(170, 3001) };
    CHECK_EQ(activity_summary(run.activity), "50:00 · hrTSS 83");
    // Cycling's LTHR for a ride: 170 over 160.
    TestActivity ride { sport::Cycling, heart_rate_at(160, 3001) };
    CHECK_EQ(activity_summary(ride.activity), "50:00 · hrTSS 83");
    // Running's for a walk.
    TestActivity walk { sport::Walking, heart_rate_at(170, 3001) };
    CHECK_EQ(activity_summary(walk.activity), "50:00 · hrTSS 83");
}

void test_hr_load_leaves_out_pauses()
{
    TestActivity steady { sport::Running, heart_rate_at(100, 11, 10) };
    CHECK(steady.activity.segment.hr_load.has_value());
    if (steady.activity.segment.hr_load) {
        CHECK_EQ(*steady.activity.segment.hr_load, 100.0 * 100 * 100);
    }
    // 11 s apart is a pause, which isn't training.
    TestActivity paused { sport::Running, heart_rate_at(100, 11, 11) };
    CHECK(!paused.activity.segment.hr_load.has_value());
}

void test_power_before_heart_rate()
{
    auto records = heart_rate_at(170, 3001);
    for (auto &rec : records) {
        rec.power = 200;
    }
    TestActivity t { sport::Cycling, records };
    CHECK(activity_summary(t.activity).ends_with(" · TSS 44"));
}

// They're in the summary under the title instead.
void test_panel_has_no_distance_or_moving_time()
{
    TestActivity t { sport::Running, records_with(10, set_speed, 4, 3.0f, 5.0f) };
    t.activity.segment.distance = 10000;
    auto const lines = data_fields(t.activity);
    CHECK(!lines.empty());
    if (!lines.empty()) {
        CHECK(lines[0].left.has_value() && lines[0].left->label == "Elapsed Time");
        CHECK(!lines[0].right.has_value());
    }
    CHECK(!t.field("Distance"));
    CHECK(!t.field("Moving Time"));
}

void test_summary()
{
    TestActivity t { sport::Cycling, records_with(40, set_power, 0, u16 { 200 }, u16 { 200 }) };
    t.activity.segment.distance = 30000;
    CHECK_EQ(activity_summary(t.activity), "30.0 km · 50:00 · TSS 44");
}

void test_summary_without_distance_or_tss()
{
    TestActivity no_distance { sport::Cycling, records_with(40, set_power, 0, u16 { 200 }, u16 { 200 }) };
    CHECK_EQ(activity_summary(no_distance.activity), "50:00 · TSS 44");

    TestActivity no_power { sport::Running, std::vector<record>(10) };
    no_power.activity.segment.distance = 10000;
    CHECK_EQ(activity_summary(no_power.activity), "10.00 km · 50:00");

    // Too few power samples for an NP, so no TSS.
    TestActivity no_np { sport::Cycling, records_with(30, set_power, 0, u16 { 200 }, u16 { 200 }) };
    CHECK_EQ(activity_summary(no_np.activity), "50:00");
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
    CHECK_EQ(t.value("Elapsed Time"), "51:40.000");
    CHECK(!t.field("Min elevation"));
    CHECK(!t.field("Climbed"));
}

// It used to show only the seconds when the minutes were 0, also with
// hours: 1:00:30 showed as 30.000.
void test_moving_time_of_an_hour()
{
    TestActivity t { sport::Running, { } };
    t.activity.segment.moving = Duration { 3630 };
    CHECK_EQ(activity_summary(t.activity), "1:00:30");
    t.activity.segment.moving = Duration { 42 };
    CHECK_EQ(activity_summary(t.activity), "42.000");
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
    CHECK_EQ(lines.size(), 1uz);
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

std::optional<DataField> lap_field(TestActivity const &t, size_t lap, std::string const &label)
{
    auto const &l = t.activity.laps[lap];
    for (auto const &line : data_fields(t.activity, l.segment, l.first_record)) {
        for (auto const &field : { line.left, line.right }) {
            if (field && field->label == label) {
                return field;
            }
        }
    }
    return { };
}

// A lap's maximum is relative to the lap; the field marks the record in
// the activity.
void test_lap_fields()
{
    TestActivity t { sport::Running, records_with(20, set_hr, 15, u8 { 120 }, u8 { 180 }), { { 0, 10, 2000, 600 }, { 10, 20, 2000, 500 } } };
    auto const   max_hr = lap_field(t, 1, "Max HR");
    CHECK(max_hr.has_value());
    if (max_hr) {
        CHECK_EQ(max_hr->value, "140 bpm");
        CHECK_EQ(max_hr->mark_at.value_or(TestActivity::NoMark), 15uz);
    }
    // The first lap is all 120.
    auto const first_max = lap_field(t, 0, "Max HR");
    CHECK(first_max.has_value() && first_max->value == "120 bpm");
    auto const pace = lap_field(t, 1, "Avg Pace");
    CHECK(pace.has_value() && pace->value == "4:10 min/km");
}

void test_lap_summary()
{
    TestActivity t { sport::Running, std::vector<record>(20), { { 0, 10, 1000, 300 }, { 10, 20, 1500, 450 } } };
    CHECK_EQ(lap_summary(t.activity, 1), "Lap 2 of 2 · 1.50 km · 07:30");
}

std::vector<std::string> headers(LapTable const &table)
{
    std::vector<std::string> ret;
    for (auto const &column : table.columns) {
        ret.push_back(column.header);
    }
    return ret;
}

void test_lap_table_columns_for_a_run()
{
    TestActivity t { sport::Running, records_with(20, set_hr, 0, u8 { 130 }, u8 { 130 }), { { 0, 10, 2000, 600 }, { 10, 20, 2000, 500 } } };
    t.activity.segment.distance = 4000;
    auto const table = lap_table(t.activity);
    CHECK(headers(table) == (std::vector<std::string> { "Lap", "Time", "Distance", "Pace", "Avg HR" }));
    CHECK_EQ(table.rows.size(), 2uz);
    if (table.rows.size() == 2) {
        CHECK(table.rows[1] == (TableRow { "2", "08:20", "2.00 km", "4:10 min/km", "130 bpm" }));
    }
}

void test_lap_table_columns_for_a_ride()
{
    auto records = records_with(60, set_power, 0, u16 { 200 }, u16 { 200 });
    for (auto ix = 0uz; ix < records.size(); ++ix) {
        records[ix].cadence = 85;
        records[ix].altitude = 100.0f + static_cast<float32>(ix);
    }
    TestActivity t { sport::Cycling, records, { { 0, 30, 10000, 1200 }, { 30, 60, 10000, 1200 } } };
    t.activity.segment.distance = 20000;
    auto const table = lap_table(t.activity);
    CHECK(headers(table) == (std::vector<std::string> { "Lap", "Time", "Distance", "Speed", "Avg Power", "Cadence", "Climb" }));
    if (table.rows.size() == 2) {
        CHECK(table.rows[0] == (TableRow { "1", "20:00", "10.00 km", "30.0 km/h", "200 W", "85 rpm", "27 m" }));
    }
}

// The other maxima and minima of a lap mark records in the activity too.
void test_lap_marks()
{
    auto records = records_with(30, set_power, 25, u16 { 200 }, u16 { 400 });
    for (auto ix = 0uz; ix < records.size(); ++ix) {
        records[ix].altitude = 100.0f + static_cast<float32>(ix);
    }
    TestActivity t { sport::Cycling, records, { { 0, 20, 5000, 600 }, { 20, 30, 2500, 300 } } };
    auto const   max_power = lap_field(t, 1, "Max Power");
    CHECK(max_power.has_value() && max_power->mark_at == 25uz);
    auto const min_elevation = lap_field(t, 1, "Min elevation");
    CHECK(min_elevation.has_value() && min_elevation->mark_at == 20uz);
    auto const max_elevation = lap_field(t, 1, "Max elevation");
    CHECK(max_elevation.has_value() && max_elevation->mark_at == 29uz);
}

// A lap without distance, such as a rest lap on a treadmill, in an
// activity with distance: empty Distance and Pace cells.
void test_lap_table_lap_without_distance()
{
    TestActivity t { sport::Running, std::vector<record>(20), { { 0, 10, 2000, 600 }, { 10, 20, 0, 60 } } };
    t.activity.segment.distance = 2000;
    auto const table = lap_table(t.activity);
    CHECK(headers(table) == (std::vector<std::string> { "Lap", "Time", "Distance", "Pace" }));
    if (table.rows.size() == 2) {
        CHECK(table.rows[0] == (TableRow { "1", "10:00", "2.00 km", "5:00 min/km" }));
        CHECK(table.rows[1] == (TableRow { "2", "01:00", "", "" }));
    }
}

// A column for data the activity has, with an empty cell for a lap without
// it, such as a lap where the heart rate strap dropped out.
void test_lap_table_empty_cells()
{
    auto records = records_with(20, set_hr, 0, u8 { 130 }, u8 { 130 });
    for (auto ix = 10uz; ix < 20; ++ix) {
        records[ix].heart_rate.reset();
    }
    TestActivity t { sport::Running, records, { { 0, 10, 0, 300 }, { 10, 20, 0, 300 } } };
    auto const   table = lap_table(t.activity);
    // No distance in the activity: no Distance or Pace.
    CHECK(headers(table) == (std::vector<std::string> { "Lap", "Time", "Avg HR" }));
    if (table.rows.size() == 2) {
        CHECK_EQ(table.rows[0][2], "130 bpm");
        CHECK_EQ(table.rows[1][2], "");
    }
}
}

// 120 records a second apart, 5 m apart, from 12 km into the ride, with
// the highest heart rate at record 70.
struct RangeActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    explicit RangeActivity(bool with_distance = true)
    {
        activity.sport = sport::Cycling;
        for (u32 ix = 0; ix < 120; ++ix) {
            record rec { .timestamp = DateTime { .timestamp = 1'000'000 + ix }, .heart_rate = static_cast<u8>((ix == 70) ? 180 : 140) };
            if (with_distance) {
                rec.distance = 12'000.0f + 5.0f * static_cast<float32>(ix);
                rec.speed = 5.0f;
            }
            activity.records.push_back(rec);
        }
        activity.recorded = activity.records;
    }
};

// A range says where it is by distance, or by time without one.
void test_range_summary()
{
    RangeActivity t;
    auto const    range = range_segment(t.activity, 20, 100);
    CHECK(range.has_value());
    if (!range) {
        return;
    }
    CHECK_EQ(range_summary(t.activity, 20, 100, *range), "12.1–12.5 km · 400 m · 01:20");
    // At a stop, in a ride with distance: still where it is in km.
    for (auto ix = 30uz; ix <= 50; ++ix) {
        t.activity.records[ix].distance = 12'160.0f;
        t.activity.recorded[ix].distance = 12'160.0f;
    }
    auto const stop = range_segment(t.activity, 35, 50);
    CHECK(stop.has_value());
    if (stop) {
        CHECK(range_summary(t.activity, 35, 50, *stop).starts_with("12.2–12.2 km · "));
    }
    RangeActivity indoors { false };
    auto const    still = range_segment(indoors.activity, 20, 100);
    CHECK(still.has_value());
    if (still) {
        CHECK_EQ(range_summary(indoors.activity, 20, 100, *still), "0:00:20–0:01:40 · 01:20");
    }
}

// A range's fields mark records of the activity, not of the range.
void test_range_fields_mark_the_activitys_records()
{
    RangeActivity t;
    auto const    range = range_segment(t.activity, 50, 100);
    CHECK(range.has_value());
    if (!range) {
        return;
    }
    std::optional<size_t> max_hr;
    for (auto const &line : data_fields(t.activity, *range, 50)) {
        for (auto const &field : { line.left, line.right }) {
            if (field && field->label == "Max HR") {
                max_hr = field->mark_at;
            }
        }
    }
    CHECK_EQ(max_hr.value_or(0), 70uz);
}

// By time, hours from the start are shown as such, past the first hour;
// a range out of bounds has just the summary.
void test_range_summary_by_time_past_an_hour()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.sport = sport::Cycling;
    for (u32 ix = 0; ix < 5000; ++ix) {
        activity.records.push_back(record { .timestamp = DateTime { .timestamp = 1'000'000 + ix } });
    }
    activity.recorded = activity.records;
    auto const range = range_segment(activity, 2530, 3933);
    CHECK(range.has_value());
    if (!range) {
        return;
    }
    CHECK_EQ(range_summary(activity, 2530, 3933, *range), "0:42:10–1:05:33 · 23:23");
    CHECK_EQ(range_summary(activity, 2530, 5000, *range), "23:23");
}

// A lap's TSS and hrTSS, with the thresholds of the activity's day and
// for its sport. The first lap's 1801 records are 1800 s at 170 bpm,
// hrTSS 50; the second's 1200, 1199 s.
void test_lap_tss()
{
    TestActivity run { sport::Running, heart_rate_at(170, 3001), { { 0, 1801, 0, 1800 }, { 1801, 3001, 0, 1200 } } };
    CHECK_EQ(lap_summary(run.activity, 0), "Lap 1 of 2 · 30:00 · hrTSS 50");
    CHECK_EQ(lap_summary(run.activity, 1), "Lap 2 of 2 · 20:00 · hrTSS 33");
    // 200 W for 1800 s, at an FTP of 275 W from 2012 and 250 W from 2025.
    TestActivity ride { sport::Cycling, records_with(80, set_power, 0, u16 { 200 }, u16 { 200 }), { { 0, 40, 0, 1800 }, { 40, 80, 0, 1800 } } };
    CHECK_EQ(lap_summary(ride.activity, 0), "Lap 1 of 2 · 30:00 · TSS 26");
    CHECK_EQ(lap_field(ride, 0, "IF").value_or(DataField { "IF", "<none>" }).value, "0.73");
    ride.storage.thresholds.push_back(Thresholds { .from = std::chrono::year { 2025 } / 1 / 1, .ftp = 250 });
    ride.activity.segment.start_time = DateTime::from_timestamp(1750000000); // June 2025
    CHECK_EQ(lap_summary(ride.activity, 0), "Lap 1 of 2 · 30:00 · TSS 32");
    CHECK_EQ(lap_field(ride, 0, "IF").value_or(DataField { "IF", "<none>" }).value, "0.80");
}

// A range's hrTSS, with cycling's LTHR of 160 bpm: 79 s at 140 and one at
// 180 is 1,580,800 bpm² s, 1.7.
void test_range_hr_tss()
{
    RangeActivity t;
    t.storage.thresholds = TestActivity::thresholds();
    auto const range = range_segment(t.activity, 20, 100);
    CHECK(range.has_value());
    if (!range) {
        return;
    }
    CHECK(range->hr_load == 79.0 * 140 * 140 + 180.0 * 180);
    CHECK_EQ(range_summary(t.activity, 20, 100, *range), "12.1–12.5 km · 400 m · 01:20 · hrTSS 1");
}

// The heart rate as recorded, not as smoothed: 100 and 200 bpm by turns
// for 100 s is 50 × 100² + 50 × 200², where smoothed it'd be near 100 ×
// 150².
void test_hr_load_is_of_the_recorded_heart_rate()
{
    auto records = heart_rate_at(100, 101);
    for (auto ix = 1uz; ix < records.size(); ix += 2) {
        records[ix].heart_rate = 200;
    }
    TestActivity t { sport::Running, records, { { 0, 101, 0, 100 } } };
    CHECK(t.activity.segment.hr_load == 50.0 * 100 * 100 + 50.0 * 200 * 200);
    CHECK(t.activity.laps[0].segment.hr_load == t.activity.segment.hr_load);
}

int main()
{
    test_range_max_at_is_the_index_passed();
    test_range_summary();
    test_range_fields_mark_the_activitys_records();
    test_range_summary_by_time_past_an_hour();
    test_lap_tss();
    test_range_hr_tss();
    test_hr_load_is_of_the_recorded_heart_rate();
    test_range_ties_go_to_the_first_record();
    test_smoothed_range_max_at();
    test_analyze_max_at_is_a_record_index();
    test_analyze_climb();
    test_analyze_climb_ignores_jitter();
    test_analyze_steady_climb();
    test_analyze_climb_threshold();
    test_climb_prefers_the_device();
    test_cadence_average_while_pedaling();
    test_cadence_average_leaves_out_low_power();
    test_cadence_min_over_30_records();
    test_cadence_min_leaves_out_coasting();
    test_cadence_min_leaves_out_a_held_reading();
    test_cadence_min_without_power();
    test_no_cadence_min_across_a_break();
    test_cadence_min_after_a_break();
    test_no_cadence_min_with_fewer_than_30_records();
    test_no_cadence_stats_without_pedaling();
    test_ride_cadence_in_rpm();
    test_run_cadence_in_spm();
    test_walk_and_hike_cadence_in_spm();
    test_rowing_cadence_as_recorded();
    test_unrealistic_cadence_is_not_shown();
    test_analyze_no_climb_without_altitude();
    test_ride_shows_speed();
    test_e_bike_ride_shows_speed();
    test_run_shows_pace();
    test_heart_rate();
    test_power();
    test_no_np_with_few_power_samples();
    test_tss_over_moving_time();
    test_tss_with_the_ftp_of_its_day();
    test_no_tss_without_thresholds();
    test_hr_tss();
    test_hr_load_leaves_out_pauses();
    test_power_before_heart_rate();
    test_panel_has_no_distance_or_moving_time();
    test_summary();
    test_summary_without_distance_or_tss();
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
    test_lap_fields();
    test_lap_summary();
    test_lap_table_columns_for_a_run();
    test_lap_table_columns_for_a_ride();
    test_lap_table_empty_cells();
    test_lap_marks();
    test_lap_table_lap_without_distance();
    return ST::Test::test_result();
}
