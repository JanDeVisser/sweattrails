/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests what the activity's graph draws: its smoothed lines, a pixel
 * column at a time, their scales, its labels and its legend.
 */

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <widget/GraphData.h>

#include <TestApp.h>

using namespace ST;

namespace {

using Records = std::vector<record>;

// Ticks over the whole of `records`.
std::vector<GraphTick> whole_time_ticks(Records const &records, float width)
{
    return time_ticks(records, GraphView { .first = 0, .end = records.size() }, width);
}

std::vector<GraphTick> whole_distance_ticks(Records const &records, float width)
{
    return distance_ticks(records, GraphView { .first = 0, .end = records.size() }, width);
}

bool near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.01f;
}

// `count` records, a second apart.
Records seconds(size_t count)
{
    Records ret(count);
    for (auto ix = 0uz; ix < count; ++ix) {
        ret[ix].timestamp.timestamp = 1000 + static_cast<u32>(ix);
    }
    return ret;
}

Records with_power(std::vector<std::optional<u16>> const &power)
{
    auto ret = seconds(power.size());
    for (auto ix = 0uz; ix < power.size(); ++ix) {
        ret[ix].power = power[ix];
    }
    return ret;
}

void test_power_is_averaged_over_five_seconds()
{
    auto const        series = graph_series(with_power({ 100, 200, 300, 400, 500, 600, 700 }), GraphValue::Power);
    GraphSeries const expected { 100.0f, 150.0f, 200.0f, 250.0f, 300.0f, 400.0f, 500.0f };
    CHECK(series == expected);
}

void test_speed_is_averaged_too()
{
    auto records = seconds(3);
    records[0].speed = 2.0f;
    records[1].speed = 4.0f;
    records[2].speed = 6.0f;
    auto const        series = graph_series(records, GraphValue::Speed);
    GraphSeries const expected { 2.0f, 3.0f, 4.0f };
    CHECK(series == expected);
}

void test_heart_rate_and_altitude_are_as_recorded()
{
    auto records = seconds(3);
    records[0].heart_rate = 120;
    records[1].heart_rate = 150;
    records[2].heart_rate = 0; // No heart rate, as real_heart_rate() says
    records[0].altitude = 10.0f;
    records[1].altitude = 30.0f;
    auto const        hr = graph_series(records, GraphValue::HeartRate);
    auto const        alt = graph_series(records, GraphValue::Altitude);
    GraphSeries const expected_hr { 120.0f, 150.0f, std::nullopt };
    GraphSeries const expected_alt { 10.0f, 30.0f, std::nullopt };
    CHECK(hr == expected_hr);
    CHECK(alt == expected_alt);
}

// A pause that recorded nothing for more than RangeMovingGap seconds
// starts the average again.
void test_average_restarts_after_a_pause()
{
    auto records = with_power({ 100, 200, 300 });
    records[2].timestamp.timestamp = records[1].timestamp.timestamp + RangeMovingGap + 1;
    auto const        series = graph_series(records, GraphValue::Power);
    GraphSeries const expected { 100.0f, 150.0f, 300.0f };
    CHECK(series == expected);
}

// A gap of RangeMovingGap seconds or less isn't a pause, but the average
// is over seconds, so the records before it have left the window.
void test_average_is_over_seconds_not_records()
{
    auto records = with_power({ 100, 200, 300 });
    records[2].timestamp.timestamp = records[1].timestamp.timestamp + GraphSmoothing;
    auto const        series = graph_series(records, GraphValue::Power);
    GraphSeries const expected { 100.0f, 150.0f, 300.0f };
    CHECK(series == expected);
    records[2].timestamp.timestamp = records[1].timestamp.timestamp + GraphSmoothing - 1;
    auto const        within = graph_series(records, GraphValue::Power);
    GraphSeries const expected_within { 100.0f, 150.0f, 250.0f };
    CHECK(within == expected_within);
}

// A dropout is a gap in the line, and the values before it aren't
// averaged into those after it.
void test_dropout_breaks_the_average()
{
    auto const        series = graph_series(with_power({ 100, 200, std::nullopt, 400, 600 }), GraphValue::Power);
    GraphSeries const expected { 100.0f, 150.0f, std::nullopt, 400.0f, 500.0f };
    CHECK(series == expected);
}

void test_columns_average_their_records()
{
    GraphSeries const series { 1.0f, 3.0f, 5.0f, 7.0f, 9.0f, 11.0f };
    auto const        columns = graph_columns(series, 3);
    GraphSeries const expected { 2.0f, 6.0f, 10.0f };
    CHECK(columns == expected);
}

void test_columns_leave_out_dropouts()
{
    GraphSeries const series { 1.0f, std::nullopt, std::nullopt, std::nullopt, 9.0f, 11.0f };
    auto const        columns = graph_columns(series, 3);
    GraphSeries const expected { 1.0f, std::nullopt, 10.0f };
    CHECK(columns == expected);
}

void test_fewer_records_than_columns()
{
    GraphSeries const series { 1.0f, 2.0f };
    auto const        columns = graph_columns(series, 5);
    GraphSeries const expected { 1.0f, 1.0f, 1.0f, 2.0f, 2.0f };
    CHECK(columns == expected);
    CHECK(graph_columns({ }, 5).empty());
    CHECK(graph_columns(series, 0).empty());
}

void test_scales()
{
    std::vector<GraphSeries> const series { { 120.0f, std::nullopt, 180.0f }, { 100.0f } };
    CHECK(graph_scale(GraphValue::HeartRate, series) == (GraphScale { .bottom = 100.0f, .top = 180.0f }));
    CHECK(graph_scale(GraphValue::Power, series) == (GraphScale { .bottom = 0.0f, .top = 180.0f }));
    std::vector<GraphSeries> const zero { { 0.0f, 0.0f } };
    CHECK(!graph_scale(GraphValue::Power, zero));
    CHECK(!graph_scale(GraphValue::Altitude, std::vector<GraphSeries> { { std::nullopt } }));
}

void test_y()
{
    GraphScale const scale { .bottom = 100.0f, .top = 200.0f };
    CHECK_EQ(graph_y(scale, 100.0f, 50.0f), 50.0f);
    CHECK_EQ(graph_y(scale, 200.0f, 50.0f), 0.0f);
    CHECK_EQ(graph_y(scale, 150.0f, 50.0f), 25.0f);
    CHECK_EQ(graph_y(GraphScale { .bottom = 7.0f, .top = 7.0f }, 7.0f, 50.0f), 25.0f);
}

// An hour 800 pixels wide has room for 10 labels: every 10 minutes.
void test_time_ticks_of_an_hour()
{
    auto const ticks = whole_time_ticks(seconds(3601), 800.0f);
    CHECK_EQ(ticks.size(), 6uz);
    if (ticks.size() == 6) {
        auto const first = ticks.front();
        auto const last = ticks.back();
        CHECK_EQ(first.label, "0:10");
        CHECK_EQ(last.label, "1:00");
        CHECK(near(first.x, 600.0f * 800.0f / 3601.0f));
    }
}

void test_time_ticks_of_a_long_ride()
{
    auto const ticks = whole_time_ticks(seconds(5 * 3600 + 1), 400.0f);
    CHECK_EQ(ticks.size(), 5uz);
    if (!ticks.empty()) {
        auto const first = ticks.front();
        CHECK_EQ(first.label, "1:00");
    }
}

// A pause that recorded nothing bunches the ticks: the record after it
// gets the last step it passed.
void test_time_ticks_around_a_pause()
{
    auto records = seconds(1200);
    for (auto ix = 500uz; ix < records.size(); ++ix) {
        records[ix].timestamp.timestamp += 1500;
    }
    // From 0 to 2699 s, 400 pixels: steps of 10 minutes. The pause is
    // from 499 to 2000 s, past 0:10 and 0:20 up to 0:30.
    auto const               ticks = whole_time_ticks(records, 400.0f);
    std::vector<std::string> labels;
    for (auto const &tick : ticks) {
        labels.push_back(tick.label);
    }
    std::vector<std::string> const expected { "0:30", "0:40" };
    CHECK(labels == expected);
    if (ticks.size() == 2) {
        CHECK(near(ticks[0].x, 500.0f * 400.0f / 1200.0f));
    }
}

void test_distance_ticks()
{
    auto records = seconds(1001);
    for (auto ix = 0uz; ix < records.size(); ++ix) {
        records[ix].distance = static_cast<float>(ix) * 10.0f;
    }
    // 10 km, 400 pixels: 5 labels, every 2 km.
    auto const               ticks = whole_distance_ticks(records, 400.0f);
    std::vector<std::string> labels;
    for (auto const &tick : ticks) {
        labels.push_back(tick.label);
    }
    std::vector<std::string> const expected { "2 km", "4 km", "6 km", "8 km", "10 km" };
    CHECK(labels == expected);
    if (!ticks.empty()) {
        CHECK(near(ticks.front().x, 200.0f * 400.0f / 1001.0f));
    }
}

void test_no_distance_ticks_without_distances()
{
    CHECK(whole_distance_ticks(seconds(100), 400.0f).empty());
    CHECK(whole_time_ticks({ }, 400.0f).empty());
}

void test_view_x_and_record()
{
    GraphView const whole { .first = 0, .end = 100 };
    GraphView const zoomed { .first = 40, .end = 60 };
    CHECK(near(view_x(whole, 50, 400), 200));
    CHECK(near(view_x(zoomed, 50, 400), 200));
    CHECK(near(view_x(zoomed, 30, 400), -200)); // Left of the view
    CHECK_EQ(view_record(zoomed, 0, 400), 40uz);
    CHECK_EQ(view_record(zoomed, 399, 400), 59uz);
    CHECK_EQ(view_record(zoomed, -50, 400), 40uz); // Dragged past the ends
    CHECK_EQ(view_record(zoomed, 900, 400), 59uz);
    CHECK_EQ(view_record(GraphView { }, 10, 400), 0uz);
}

// Zoomed to 1:12:00–1:18:00 of a 2-hour ride, 400 pixels: labels from
// the activity's start, every 2 minutes.
void test_time_ticks_zoomed()
{
    auto const               records = seconds(7200);
    GraphView const          view { .first = 4320, .end = 4681 };
    auto const               ticks = time_ticks(records, view, 400.0f);
    std::vector<std::string> labels;
    for (auto const &tick : ticks) {
        labels.push_back(tick.label);
    }
    std::vector<std::string> const expected { "1:14", "1:16", "1:18" };
    CHECK(labels == expected);
    if (!ticks.empty()) {
        CHECK(near(ticks.front().x, 120.0f * 400.0f / 361.0f));
    }
}

// A few minutes zoomed: steps under a minute, with seconds.
void test_time_ticks_with_seconds()
{
    auto const               records = seconds(7200);
    GraphView const          view { .first = 4320, .end = 4441 };
    auto const               ticks = time_ticks(records, view, 400.0f);
    std::vector<std::string> labels;
    for (auto const &tick : ticks) {
        labels.push_back(tick.label);
    }
    std::vector<std::string> const expected { "1:12:30", "1:13:00", "1:13:30", "1:14:00" };
    CHECK(labels == expected);
}

void test_distance_ticks_zoomed()
{
    auto records = seconds(1001);
    for (auto ix = 0uz; ix < records.size(); ++ix) {
        records[ix].distance = static_cast<float>(ix) * 10.0f;
    }
    // 4.5 to 7.5 km, 400 pixels: every km, from the start.
    auto const               ticks = distance_ticks(records, GraphView { .first = 450, .end = 751 }, 400.0f);
    std::vector<std::string> labels;
    for (auto const &tick : ticks) {
        labels.push_back(tick.label);
    }
    std::vector<std::string> const expected { "5 km", "6 km", "7 km" };
    CHECK(labels == expected);
}

struct TestActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    explicit TestActivity(sport the_sport)
    {
        activity.sport = the_sport;
    }
};

void test_legend_ranges()
{
    TestActivity ride { sport::Cycling };
    auto const   power = legend_range(ride.activity, GraphValue::Power, { .bottom = 0, .top = 612.4f });
    CHECK_EQ(power.text, "Pwr 0–612 W");
    CHECK(ColorIsEqual(power.color, PowerColor));
    auto const speed = legend_range(ride.activity, GraphValue::Speed, { .bottom = 0, .top = 13.0f });
    CHECK_EQ(speed.text, "Spd 0–47 km/h");
    CHECK_EQ(legend_range(ride.activity, GraphValue::HeartRate, { .bottom = 102, .top = 178 }).text, "HR 102–178 bpm");
    CHECK_EQ(legend_range(ride.activity, GraphValue::Altitude, { .bottom = 12, .top = 12 }).text, "Alt 12 m");
    CHECK_EQ(legend_range(ride.activity, GraphValue::Cadence, { .bottom = 0, .top = 95 }).text, "Cad 0–95 rpm");
    TestActivity run { sport::Running };
    CHECK_EQ(legend_range(run.activity, GraphValue::Cadence, { .bottom = 0, .top = 90 }).text, "Cad 0–180 spm");
}

void test_legend_values()
{
    record rec { };
    rec.altitude = 45.4f;
    rec.speed = 9.0f;
    rec.heart_rate = 145;
    rec.power = 245;
    auto const               labels = legend_values(rec);
    std::vector<std::string> texts;
    for (auto const &label : labels) {
        texts.push_back(label.text);
    }
    std::vector<std::string> const expected { "Alt 45 m", "Spd 32.4 km/h", "HR 145 bpm", "Pwr 245 W" };
    CHECK(texts == expected);

    record sparse { };
    sparse.heart_rate = 0;
    sparse.power = 0;
    auto const only_power = legend_values(sparse);
    CHECK_EQ(only_power.size(), 1uz);
    if (!only_power.empty()) {
        CHECK_EQ(only_power.front().text, "Pwr 0 W");
    }
}

// A watch can set its clock back: the records after it are earlier than
// those before. The average restarts there, as after a pause, and doesn't
// keep the earlier values until the clock has caught up with them.
void test_average_restarts_when_the_clock_goes_back()
{
    auto records = with_power({ 100, 100, 100, 100, 100, 300, 300, 300, 300, 300, 300, 300 });
    for (auto ix = 5uz; ix < records.size(); ++ix) {
        records[ix].timestamp.timestamp = 500 + static_cast<u32>(ix);
    }
    auto const series = graph_series(records, GraphValue::Power);
    CHECK_EQ(series.size(), records.size());
    if (series.size() == records.size()) {
        CHECK(series[5] && near(*series[5], 300.0f));
        CHECK(series.back() && near(*series.back(), 300.0f));
    }
}

// Records with the same timestamp are both in the average.
void test_repeated_timestamps_are_averaged()
{
    auto records = with_power({ 100, 200, 300 });
    records[1].timestamp.timestamp = records[0].timestamp.timestamp;
    auto const        series = graph_series(records, GraphValue::Power);
    GraphSeries const expected { 100.0f, 150.0f, 200.0f };
    CHECK(series == expected);
}

// Cadence isn't smoothed, as heart rate and altitude aren't.
void test_cadence_is_as_recorded()
{
    auto records = seconds(3);
    records[0].cadence = 80;
    records[1].cadence = 90;
    records[2].cadence = 100;
    auto const        series = graph_series(records, GraphValue::Cadence);
    GraphSeries const expected { 80.0f, 90.0f, 100.0f };
    CHECK(series == expected);
}

// More records than columns, not a multiple of them: each record is in
// exactly one column, and the last column takes the remainder.
void test_columns_take_every_record_once()
{
    GraphSeries const series { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
    auto const        columns = graph_columns(series, 3);
    GraphSeries const expected { 1.5f, 3.5f, 6.0f };
    CHECK(columns == expected);
    // A single record fills the plot.
    auto const        single = graph_columns(GraphSeries { 4.0f }, 3);
    GraphSeries const expected_single { 4.0f, 4.0f, 4.0f };
    CHECK(single == expected_single);
}

// The spec's example: a pause from 0:08 to 0:35 with labels every 10
// minutes gives the record after it "0:30", not "0:10".
void test_time_ticks_after_the_specs_pause()
{
    Records records;
    for (u32 t = 0; t <= 480; ++t) {
        records.push_back(record { });
        records.back().timestamp.timestamp = 1000 + t;
    }
    for (u32 t = 2100; t <= 3000; ++t) {
        records.push_back(record { });
        records.back().timestamp.timestamp = 1000 + t;
    }
    auto const               ticks = whole_time_ticks(records, 400.0f);
    std::vector<std::string> labels;
    for (auto const &tick : ticks) {
        labels.push_back(tick.label);
    }
    std::vector<std::string> const expected { "0:30", "0:40", "0:50" };
    CHECK(labels == expected);
    if (!ticks.empty()) {
        CHECK(near(ticks.front().x, 481.0f * 400.0f / static_cast<float>(records.size())));
    }
}

// A single record, a graph too narrow for two labels, and distances that
// are all 0.
void test_ticks_at_the_edges()
{
    CHECK(whole_time_ticks(seconds(1), 400.0f).empty());
    CHECK(whole_time_ticks(seconds(100), 0.0f).empty());
    auto const narrow = whole_time_ticks(seconds(3601), 50.0f);
    CHECK_EQ(narrow.size(), 1uz);
    if (!narrow.empty()) {
        CHECK_EQ(narrow.front().label, "1:00");
    }
    auto standing = seconds(100);
    for (auto &rec : standing) {
        rec.distance = 0.0f;
    }
    CHECK(whole_distance_ticks(standing, 400.0f).empty());
}

// While comparing, the legend's range covers both recordings.
void test_legend_range_while_comparing()
{
    TestActivity                   ride { sport::Cycling };
    std::vector<GraphSeries> const power { { 100.0f, 250.0f }, { std::nullopt, 300.0f } };
    auto const                     scale = graph_scale(GraphValue::Power, power);
    CHECK(scale.has_value());
    if (scale) {
        CHECK_EQ(legend_range(ride.activity, GraphValue::Power, *scale).text, "Pwr 0–300 W");
    }
}

void test_mark_position()
{
    auto records = seconds(3);
    records[2].timestamp.timestamp += 2 * 3600 + 42 * 60;
    records[2].distance = 12345.0f;
    auto const               labels = mark_position(records, 2);
    std::vector<std::string> texts;
    for (auto const &label : labels) {
        texts.push_back(label.text);
    }
    std::vector<std::string> const expected { "2:42:02", "12.35 km" };
    CHECK(texts == expected);

    // Without a distance, only the time.
    auto const first = mark_position(records, 0);
    CHECK_EQ(first.size(), 1uz);
    if (!first.empty()) {
        CHECK_EQ(first.front().text, "0:00:00");
    }
    CHECK(mark_position(records, 3).empty());
}

}

int main()
{
    test_power_is_averaged_over_five_seconds();
    test_speed_is_averaged_too();
    test_heart_rate_and_altitude_are_as_recorded();
    test_average_restarts_after_a_pause();
    test_average_is_over_seconds_not_records();
    test_dropout_breaks_the_average();
    test_columns_average_their_records();
    test_columns_leave_out_dropouts();
    test_fewer_records_than_columns();
    test_scales();
    test_y();
    test_time_ticks_of_an_hour();
    test_time_ticks_of_a_long_ride();
    test_time_ticks_around_a_pause();
    test_distance_ticks();
    test_no_distance_ticks_without_distances();
    test_legend_ranges();
    test_legend_values();
    test_average_restarts_when_the_clock_goes_back();
    test_repeated_timestamps_are_averaged();
    test_cadence_is_as_recorded();
    test_columns_take_every_record_once();
    test_time_ticks_after_the_specs_pause();
    test_ticks_at_the_edges();
    test_legend_range_while_comparing();
    test_mark_position();
    test_view_x_and_record();
    test_time_ticks_zoomed();
    test_time_ticks_with_seconds();
    test_distance_ticks_zoomed();
    return Test::test_result();
}
