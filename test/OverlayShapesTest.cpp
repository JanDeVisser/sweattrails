/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The geometry of gauges, graphs and panels, and the values a graph shows.
 */

#include <format>
#include <string>
#include <vector>

#include <overlay/Shapes.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

constexpr u32 Start = 1'750'000'000;

std::string show(Rectangle r)
{
    return std::format("{} {} {} {}", r.x, r.y, r.width, r.height);
}

// The columns as text: a value each, or `-` for a break.
std::string show(std::vector<std::optional<double>> const &columns)
{
    std::string ret;
    for (auto const &c : columns) {
        ret += ret.empty() ? "" : " ";
        ret += c ? std::format("{:.0f}", *c) : std::string { "-" };
    }
    return ret;
}

// A ride of a record a second at 10 m/s, 200 W and 150 bpm, for 300 s; a
// pause of 100 s without records; and another 200 s. The power meter drops
// out from 100 s to 110 s.
struct Ride {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    explicit Ride(bool with_distance = true)
    {
        activity.sport = sport::Cycling;
        activity.segment.start_time.timestamp = Start;
        auto const add = [this, with_distance](u32 t, float distance) {
            auto const dropout = t >= 100 && t < 110;
            activity.records.push_back(record {
                .timestamp = DateTime { .timestamp = Start + t },
                .heart_rate = 150,
                .distance = with_distance ? std::optional { distance } : std::nullopt,
                .speed = 10.0f,
                .power = dropout ? std::nullopt : std::optional<u16> { 200 },
            });
        };
        for (auto t = 0u; t <= 300; ++t) {
            add(t, static_cast<float>(t) * 10.0f);
        }
        for (auto t = 400u; t <= 600; ++t) {
            add(t, 3000.0f + static_cast<float>(t - 400) * 10.0f);
        }
    }
};

GraphElement graph(OverlayField field, std::optional<double> window, GraphAxis axis = GraphAxis::Time)
{
    return GraphElement { .field = field, .axis = axis, .window = window };
}

FieldUnit unit_of(OverlayField field)
{
    return default_unit(field, Units::Metric, sport::Cycling);
}

void test_content_box()
{
    CHECK_EQ(show(content_box({ 10, 20, 100, 50 }, 0)), "10 20 100 50");
    CHECK_EQ(show(content_box({ 10, 20, 100, 50 }, 5)), "15 25 90 40");
    // Padding more than half the box leaves nothing, not less.
    CHECK_EQ(show(content_box({ 10, 20, 100, 50 }, 40)), "50 45 20 0");
}

// A box without a size is its content with the padding around it; one
// with a size keeps it, padding or not. Both scale.
void test_box_size()
{
    auto const  show_size = [](Vector2 v) { return std::format("{} {}", v.x, v.y); };
    Panel const padded { .padding = 10 };
    CHECK_EQ(show_size(box_size(Placement { }, { 100, 40 }, padded, 1)), "120 60");
    CHECK_EQ(show_size(box_size(Placement { }, { 100, 40 }, padded, 2)), "140 80");
    CHECK_EQ(show_size(box_size(Placement { }, { 100, 40 }, Panel { }, 2)), "100 40");
    Placement const sized { .size = Vector2 { 300, 50 } };
    CHECK_EQ(show_size(box_size(sized, { 100, 40 }, padded, 1)), "300 50");
    CHECK_EQ(show_size(box_size(sized, { 100, 40 }, padded, 2)), "600 100");
}

void test_shown_bounds()
{
    Ride                t;
    OverlaySource const source { t.activity };
    auto const          show_bounds = [&source](OverlayField field, FieldUnit unit, std::optional<double> min, std::optional<double> max) {
        auto const b = shown_bounds(source, field, unit, min, max);
        return b ? std::format("{:.0f} {:.0f}", b->min, b->max) : std::string { "none" };
    };
    CHECK_EQ(show_bounds(OverlayField::power, unit_of(OverlayField::power), 0, 400), "0 400");
    // "auto" is the activity's range, in the unit shown.
    CHECK_EQ(show_bounds(OverlayField::distance, unit_of(OverlayField::distance), { }, { }), "0 5");
    CHECK_EQ(show_bounds(OverlayField::speed, unit_of(OverlayField::speed), 0, { }), "0 36");
    // A field that never changes: 1 apart, rather than dividing by 0.
    CHECK_EQ(show_bounds(OverlayField::heart_rate, unit_of(OverlayField::heart_rate), { }, { }), "150 151");
    CHECK_EQ(show_bounds(OverlayField::heart_rate, unit_of(OverlayField::heart_rate), 160, { }), "160 161");
    // "auto" without the field; numbers don't need it.
    CHECK_EQ(show_bounds(OverlayField::temperature, unit_of(OverlayField::temperature), { }, 40), "none");
    CHECK_EQ(show_bounds(OverlayField::temperature, unit_of(OverlayField::temperature), 0, 40), "0 40");
}

void test_gauge_fraction()
{
    Bounds const bounds { .min = 0, .max = 400 };
    CHECK_EQ(gauge_fraction(100.0, bounds).value_or(-1), 0.25f);
    CHECK_EQ(gauge_fraction(-5.0, bounds).value_or(-1), 0.0f);
    CHECK_EQ(gauge_fraction(500.0, bounds).value_or(-1), 1.0f);
    CHECK(!gauge_fraction(std::nullopt, bounds));
    CHECK_EQ(gauge_fraction(30.0, Bounds { .min = 20, .max = 60 }).value_or(-1), 0.25f);
}

void test_bar_rects()
{
    auto const bar = bar_rects({ 0, 0, 200, 40 }, GaugeShape::Bar, 10.0f, 0.5f);
    CHECK_EQ(show(bar.track), "0 15 200 10");
    CHECK_EQ(show(bar.fill), "0 15 100 10");
    // Without a thickness, the box's height; one thicker than the box fits it.
    CHECK_EQ(show(bar_rects({ 0, 0, 200, 40 }, GaugeShape::Bar, { }, 1).track), "0 0 200 40");
    CHECK_EQ(show(bar_rects({ 0, 0, 200, 40 }, GaugeShape::Bar, 60.0f, 1).track), "0 0 200 40");

    auto const vbar = bar_rects({ 10, 0, 40, 200 }, GaugeShape::VBar, 10.0f, 0.25f);
    CHECK_EQ(show(vbar.track), "25 0 10 200");
    CHECK_EQ(show(vbar.fill), "25 150 10 50");
    CHECK_EQ(show(bar_rects({ 10, 0, 40, 200 }, GaugeShape::VBar, { }, 0).fill), "10 200 40 0");
}

void test_arc()
{
    CHECK_EQ(arc_angles(0).start, 135.0f);
    CHECK_EQ(arc_angles(0).end, 135.0f);
    CHECK_EQ(arc_angles(0.5f).end, 270.0f);
    CHECK_EQ(arc_angles(1).end, 405.0f);
    CHECK_EQ(arc_angles(2).end, 405.0f);

    // As big as fits, in the middle.
    auto const ring = arc_ring({ 0, 0, 200, 100 }, { });
    CHECK_EQ(ring.center.x, 100.0f);
    CHECK_EQ(ring.center.y, 50.0f);
    CHECK_EQ(ring.outer, 50.0f);
    CHECK_EQ(ring.inner, 40.0f);
    CHECK_EQ(arc_ring({ 0, 0, 200, 100 }, 14.0f).inner, 36.0f);
    CHECK_EQ(arc_ring({ 0, 0, 200, 100 }, 80.0f).inner, 0.0f);
}

// At the start of a rolling window, the line grows in from the right.
void test_rolling_window_at_the_start()
{
    Ride                t;
    OverlaySource const source { t.activity };
    auto const          power = graph(OverlayField::power, 10);
    CHECK_EQ(show(graph_columns(source, power, unit_of(OverlayField::power), 5, 10)), "- - - - - 200 200 200 200 200");
    CHECK_EQ(show(graph_columns(source, power, unit_of(OverlayField::power), 0, 4)), "- - - -");
    // More columns than records: the value at each column's middle.
    CHECK_EQ(show(graph_columns(source, power, unit_of(OverlayField::power), 2, 8)), "- - - - - - 200 200");
}

// In a pause, power is 0 and heart rate breaks the line.
void test_pause_on_a_time_axis()
{
    Ride                t;
    OverlaySource const source { t.activity };
    CHECK_EQ(show(graph_columns(source, graph(OverlayField::power, 100), unit_of(OverlayField::power), 350, 10)),
        "200 200 200 200 200 200 0 0 0 0");
    CHECK_EQ(show(graph_columns(source, graph(OverlayField::heart_rate, 100), unit_of(OverlayField::heart_rate), 350, 10)),
        "150 150 150 150 150 150 - - - -");
}

// A power meter that drops out breaks the line, while heart rate goes on.
void test_dropout()
{
    Ride                t;
    OverlaySource const source { t.activity };
    CHECK_EQ(show(graph_columns(source, graph(OverlayField::power, 20), unit_of(OverlayField::power), 115, 4)), "200 - - 200");
    CHECK_EQ(show(graph_columns(source, graph(OverlayField::heart_rate, 20), unit_of(OverlayField::heart_rate), 115, 4)),
        "150 150 150 150");
}

// "all" over a long activity: each column is its stretch's average, rather
// than one jagged record.
void test_all_averages()
{
    Storage  storage;
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = Start;
    for (auto t = 0u; t < 3600; ++t) {
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + t },
            .power = static_cast<u16>(t % 2 == 0 ? 100 : 300),
        });
    }
    OverlaySource const source { activity };
    auto const          columns = graph_columns(source, graph(OverlayField::power, { }), unit_of(OverlayField::power), 10, 36);
    CHECK_EQ(columns.size(), 36uz);
    CHECK_EQ(show(columns), show(std::vector<std::optional<double>>(36, 200.0)));
    // The whole activity, wherever the moment is.
    CHECK_EQ(show(graph_columns(source, graph(OverlayField::power, { }), unit_of(OverlayField::power), 3000, 36)), show(columns));
}

// On a distance axis a pause takes no width: no 0 W in the line.
void test_distance_axis_through_a_pause()
{
    Ride                t;
    OverlaySource const source { t.activity };
    auto const          power = graph(OverlayField::power, 200, GraphAxis::Distance);
    CHECK_EQ(show(graph_columns(source, power, unit_of(OverlayField::power), 3100, 10)), show(std::vector<std::optional<double>>(10, 200.0)));
    // Speed in the unit shown.
    auto const speed = graph(OverlayField::speed, 100, GraphAxis::Distance);
    CHECK_EQ(show(graph_columns(source, speed, unit_of(OverlayField::speed), 50, 4)), "- - 36 36");
    // Between records, the value at the column's middle.
    CHECK_EQ(show(graph_columns(source, speed, unit_of(OverlayField::speed), 3100, 40)), show(std::vector<std::optional<double>>(40, 36.0)));
}

// Without distances, a distance axis has nothing; nor has a field the
// activity hasn't got.
void test_nothing_to_draw()
{
    Ride                t { false };
    OverlaySource const source { t.activity };
    CHECK_EQ(show(graph_columns(source, graph(OverlayField::power, 100, GraphAxis::Distance), unit_of(OverlayField::power), 50, 3)), "- - -");
    CHECK_EQ(show(graph_columns(source, graph(OverlayField::power, 3), unit_of(OverlayField::power), 50, 3)), "200 200 200");
    CHECK_EQ(show(graph_columns(source, graph(OverlayField::temperature, 3), unit_of(OverlayField::temperature), 50, 3)), "- - -");
    CHECK(graph_columns(source, graph(OverlayField::power, 3), unit_of(OverlayField::power), 50, 0).empty());
}

void test_graph_now()
{
    Ride                t;
    OverlaySource const source { t.activity };
    auto const          values = values_at_time(source, 50);
    CHECK_EQ(graph_now(graph(OverlayField::power, 10), values).value_or(-1), 50.0);
    CHECK_EQ(graph_now(graph(OverlayField::power, 10, GraphAxis::Distance), values).value_or(-1), 500.0);

    Ride                without { false };
    OverlaySource const no_distance { without.activity };
    CHECK(!graph_now(graph(OverlayField::power, 10, GraphAxis::Distance), values_at_time(no_distance, 50)));
}

void test_graph_position()
{
    Ride                t;
    OverlaySource const source { t.activity };
    CHECK_EQ(graph_position(source, graph(OverlayField::power, 10), 50).value_or(-1), 1.0f);
    CHECK_EQ(graph_position(source, graph(OverlayField::power, { }), 150).value_or(-1), 0.25f);
    CHECK_EQ(graph_position(source, graph(OverlayField::power, { }), 900).value_or(-1), 1.0f);
    CHECK_EQ(graph_position(source, graph(OverlayField::power, { }, GraphAxis::Distance), 1000).value_or(-1), 0.2f);

    Ride                without { false };
    OverlaySource const no_distance { without.activity };
    CHECK(!graph_position(no_distance, graph(OverlayField::power, 10, GraphAxis::Distance), 50));
}

void test_graph_y()
{
    Bounds const    bounds { .min = 0, .max = 100 };
    Rectangle const content { 0, 10, 50, 100 };
    CHECK_EQ(graph_y(50, bounds, content), 60.0f);
    CHECK_EQ(graph_y(100, bounds, content), 10.0f);
    CHECK_EQ(graph_y(-10, bounds, content), 110.0f);
    CHECK_EQ(graph_y(200, bounds, content), 10.0f);
}

// The last column takes the activity's last record with "all", also when
// the columns' widths don't add up to the activity's length exactly: 3 s
// in 7 columns comes to a hair less than 3.
void test_all_takes_the_last_record()
{
    Storage  storage;
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = Start;
    for (auto t = 0u; t <= 3; ++t) {
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + t },
            .power = static_cast<u16>(t == 3 ? 1000 : 100),
        });
    }
    OverlaySource const source { activity };
    auto const          columns = graph_columns(source, graph(OverlayField::power, { }), unit_of(OverlayField::power), 3, 7);
    CHECK_EQ(columns.size(), 7uz);
    CHECK_EQ(columns.back().value_or(-1), 1000.0);
}

// "all" on a distance axis: the pause takes no width, and the columns
// share out the distance from the first record's to the last's.
void test_all_on_a_distance_axis()
{
    Ride                t;
    OverlaySource const source { t.activity };
    auto const          power = graph(OverlayField::power, { }, GraphAxis::Distance);
    // 5000 m in 10 columns of 500 m: the dropout is 10 of the first
    // column's 50 records.
    CHECK_EQ(show(graph_columns(source, power, unit_of(OverlayField::power), 1000, 10)), show(std::vector<std::optional<double>>(10, 200.0)));
    auto const hr = graph(OverlayField::heart_rate, { }, GraphAxis::Distance);
    CHECK_EQ(show(graph_columns(source, hr, unit_of(OverlayField::heart_rate), 1000, 10)), show(std::vector<std::optional<double>>(10, 150.0)));
}

// A rolling window on a distance axis, at the start: empty to the left of
// the first record's distance.
void test_rolling_distance_window_at_the_start()
{
    Ride                t;
    OverlaySource const source { t.activity };
    auto const          speed = graph(OverlayField::speed, 100, GraphAxis::Distance);
    CHECK_EQ(show(graph_columns(source, speed, unit_of(OverlayField::speed), 20, 10)), "- - - - - - - - 36 36");
}

// Grade is graphed as the other fields are, from each record's grade.
void test_grade_graph()
{
    Storage  storage;
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = Start;
    // 10 m a second, climbing 1 m a second: 10 %.
    for (auto t = 0u; t <= 100; ++t) {
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + t },
            .altitude = static_cast<float>(t),
            .distance = static_cast<float>(t) * 10.0f,
        });
    }
    OverlaySource const source { activity };
    CHECK(source.has(OverlayField::grade));
    auto const grade = graph(OverlayField::grade, 20);
    CHECK_EQ(show(graph_columns(source, grade, unit_of(OverlayField::grade), 50, 4)), "10 10 10 10");
}

}

int main()
{
    test_content_box();
    test_box_size();
    test_shown_bounds();
    test_gauge_fraction();
    test_bar_rects();
    test_arc();
    test_rolling_window_at_the_start();
    test_pause_on_a_time_axis();
    test_dropout();
    test_all_averages();
    test_distance_axis_through_a_pause();
    test_nothing_to_draw();
    test_graph_now();
    test_graph_position();
    test_graph_y();
    test_all_takes_the_last_record();
    test_all_on_a_distance_axis();
    test_rolling_distance_window_at_the_start();
    test_grade_graph();
    return ST::Test::test_result();
}
