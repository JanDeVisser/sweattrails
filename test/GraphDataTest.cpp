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
    auto const ticks = time_ticks(seconds(3601), 800.0f);
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
    auto const ticks = time_ticks(seconds(5 * 3600 + 1), 400.0f);
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
    auto const               ticks = time_ticks(records, 400.0f);
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
    auto const               ticks = distance_ticks(records, 400.0f);
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
    CHECK(distance_ticks(seconds(100), 400.0f).empty());
    CHECK(time_ticks({ }, 400.0f).empty());
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
    return Test::test_result();
}
