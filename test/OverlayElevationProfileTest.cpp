/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay's elevation profile: its extent, columns, scale, line,
 * marker, lap lines and label.
 */

#include <format>
#include <string>
#include <vector>

#include <overlay/ElevationProfile.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

constexpr u32 Start = 1'750'000'000;

// A record every 5 m, a second apart: flat to 100 m, a ramp of 20% to 8 m
// at 140 m, and flat again to 300 m. Three laps, from 0, 100 and 200 m.
struct Climb {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    explicit Climb(bool with_laps = true)
    {
        activity.segment.start_time.timestamp = Start;
        for (auto d = 0u; d <= 300; d += 5) {
            auto const altitude = (d <= 100) ? 0.0 : (d <= 140) ? (d - 100) * 0.2
                                                                : 8.0;
            activity.records.push_back(record {
                .timestamp = DateTime { .timestamp = Start + d / 5 },
                .altitude = static_cast<f32>(altitude),
                .distance = static_cast<f32>(d),
            });
        }
        if (with_laps) {
            activity.laps.push_back(Lap { .first_record = 0, .end_record = 20 });
            activity.laps.push_back(Lap { .first_record = 20, .end_record = 40 });
            activity.laps.push_back(Lap { .first_record = 40, .end_record = 61 });
        }
    }
};

FieldUnit meters()
{
    return default_unit(OverlayField::altitude, Units::Metric, sport::Cycling);
}

std::string show(std::optional<DistanceRange> range)
{
    return range ? std::format("{:.0f}-{:.0f}", range->from, range->to) : std::string { "none" };
}

std::string show(std::vector<double> const &columns)
{
    std::string ret;
    for (auto const c : columns) {
        ret += std::format("{}{:.1f}", ret.empty() ? "" : " ", c);
    }
    return ret;
}

std::string show(Bounds b)
{
    return std::format("{:.1f} {:.1f}", b.min, b.max);
}

std::string show(std::vector<Vector2> const &points)
{
    std::string ret;
    for (auto const &p : points) {
        ret += std::format("{}({} {})", ret.empty() ? "" : " ", p.x, p.y);
    }
    return ret;
}

std::string show(Rectangle r)
{
    return std::format("{} {} {} {}", r.x, r.y, r.width, r.height);
}

FieldValues in_lap(OverlaySource const &source, double lap)
{
    auto ret = values_at_record(source, 0);
    ret.values[static_cast<size_t>(OverlayField::lap)] = lap;
    return ret;
}

void test_extent()
{
    Climb               c;
    OverlaySource const source { c.activity };
    ProfileElement      all { };
    ProfileElement      lap { .extent = ProfileExtent::Lap };
    CHECK_EQ(show(profile_extent(source, all, in_lap(source, 2))), "0-300");
    CHECK_EQ(show(profile_extent(source, lap, in_lap(source, 1))), "0-95");
    CHECK_EQ(show(profile_extent(source, lap, in_lap(source, 2))), "100-195");
    CHECK_EQ(show(profile_extent(source, lap, in_lap(source, 3))), "200-300");

    // An activity without laps is one lap.
    Climb               no_laps { false };
    OverlaySource const one_lap { no_laps.activity };
    CHECK_EQ(show(profile_extent(one_lap, lap, in_lap(one_lap, 1))), "0-300");

    // Without altitudes, there's no profile.
    for (auto &r : c.activity.records) {
        r.altitude.reset();
    }
    OverlaySource const flat { c.activity };
    CHECK_EQ(show(profile_extent(flat, all, in_lap(flat, 1))), "none");
}

void test_columns()
{
    Climb               c;
    OverlaySource const source { c.activity };
    DistanceRange const all { .from = 0, .to = 300 };
    // Flat, 40 m of the ramp at 4 m and 60 m at 8 m, and flat at 8 m.
    CHECK_EQ(show(profile_columns(source, all, 0, meters(), 3)), "0.0 6.4 8.0");
    // Smoothed over 20 m, the middle column is 90 to 210 m.
    CHECK_EQ(show(profile_columns(source, all, 20, meters(), 3)), "0.1 6.0 8.0");
    // In the unit shown.
    auto const feet = default_unit(OverlayField::altitude, Units::Imperial, sport::Cycling);
    CHECK_EQ(show(profile_columns(source, all, 0, feet, 3)), "0.0 21.0 26.2");
}

// GPS altitude that jumps 10 m up and down every 5 m: smoothing over 20 m
// takes the jaggies out of columns of half a meter.
void test_smoothing_a_sawtooth()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = Start;
    for (auto d = 0u; d <= 300; d += 5) {
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + d / 5 },
            .altitude = (d % 10 == 0) ? 0.0f : 10.0f,
            .distance = static_cast<f32>(d),
        });
    }
    OverlaySource const source { activity };
    DistanceRange const middle { .from = 100, .to = 200 };
    auto const          spread = [](std::vector<double> const &columns) {
        return *std::ranges::max_element(columns) - *std::ranges::min_element(columns);
    };
    CHECK(spread(profile_columns(source, middle, 0, meters(), 200)) > 8);
    CHECK(spread(profile_columns(source, middle, 20, meters(), 200)) < 1);
}

void test_bounds()
{
    std::vector<double> const columns { 0, 6.4, 8 };
    CHECK_EQ(show(profile_bounds(columns, { }, { }, 5)), "0.0 8.0");
    // A flat ride sits at the bottom of the least span.
    CHECK_EQ(show(profile_bounds(columns, { }, { }, 100)), "0.0 100.0");
    CHECK_EQ(show(profile_bounds(columns, 2.0, { }, 100)), "2.0 102.0");
    // With the top given, the bottom goes down.
    CHECK_EQ(show(profile_bounds(columns, { }, 50.0, 100)), "-50.0 50.0");
    // Both given: as they are.
    CHECK_EQ(show(profile_bounds(columns, 10.0, 20.0, 100)), "10.0 20.0");
}

void test_points_and_split()
{
    auto const points = profile_points({ 0, 10, 10 }, Bounds { .min = 0, .max = 10 }, { 0, 0, 3, 100 });
    CHECK_EQ(show(points), "(0.5 100) (1.5 0) (2.5 0)");
    auto const split = split_at(points, 1);
    CHECK_EQ(show(split.done), "(0.5 100) (1 50)");
    CHECK_EQ(show(split.remaining), "(1 50) (1.5 0) (2.5 0)");
    // Before the line: all of it remains; past it, all of it is done.
    CHECK(split_at(points, 0).done.empty());
    CHECK_EQ(show(split_at(points, 0).remaining), show(points));
    CHECK_EQ(show(split_at(points, 3).done), show(points));
    CHECK(split_at(points, 3).remaining.empty());
}

void test_marker()
{
    Climb               c;
    OverlaySource const source { c.activity };
    DistanceRange const all { .from = 0, .to = 300 };
    Rectangle const     content { 0, 0, 300, 100 };
    auto const          points = profile_points(profile_columns(source, all, 0, meters(), 300), Bounds { .min = 0, .max = 8 }, content);
    auto const          at = [&](std::optional<double> distance) {
        auto const marker = profile_marker(points, all, distance, content);
        return marker ? std::format("{:.0f} {:.0f}", marker->x, marker->y) : std::string { "none" };
    };
    // Half way up the ramp, at 4 m.
    CHECK_EQ(at(120), "120 50");
    CHECK_EQ(at(300), "300 0");
    CHECK_EQ(at({ }), "none");
    CHECK_EQ(at(400), "none");

    // A minute's pause at 120 m: the marker holds.
    for (auto &r : c.activity.records) {
        if (*r.distance > 120) {
            r.timestamp.timestamp += 60;
        }
    }
    OverlaySource const paused { c.activity };
    CHECK_EQ(at(values_at_time(paused, 24)[OverlayField::distance]), "120 50");
    CHECK_EQ(at(values_at_time(paused, 60)[OverlayField::distance]), "120 50");
}

void test_lap_starts()
{
    Climb               c;
    OverlaySource const source { c.activity };
    CHECK_EQ(show(lap_starts(source, DistanceRange { .from = 0, .to = 300 })), "100.0 200.0");
    CHECK_EQ(show(lap_starts(source, DistanceRange { .from = 150, .to = 300 })), "200.0");
    Climb               no_laps { false };
    OverlaySource const one_lap { no_laps.activity };
    CHECK(lap_starts(one_lap, DistanceRange { .from = 0, .to = 300 }).empty());
}

void test_label_box()
{
    Rectangle const content { 0, 0, 300, 100 };
    Vector2 const   size { 40, 20 };
    // Above and to the right.
    CHECK_EQ(show(label_box({ 100, 60 }, 10, size, content)), "110 30 40 20");
    // To the left, near the right edge.
    CHECK_EQ(show(label_box({ 280, 60 }, 10, size, content)), "230 30 40 20");
    // Below, with no room above.
    CHECK_EQ(show(label_box({ 100, 15 }, 10, size, content)), "110 25 40 20");
    CHECK_EQ(show(label_box({ 280, 15 }, 10, size, content)), "230 25 40 20");
    // Inside the content, even when it doesn't fit.
    CHECK_EQ(show(label_box({ 100, 95 }, 10, Vector2 { 40, 200 }, content)), "110 0 40 200");
    CHECK_EQ(show(label_box({ 10, 60 }, 10, Vector2 { 400, 20 }, content)), "0 30 400 20");
}

}

int main()
{
    test_extent();
    test_columns();
    test_smoothing_a_sawtooth();
    test_bounds();
    test_points_and_split();
    test_marker();
    test_lap_starts();
    test_label_box();
    return ST::Test::test_result();
}
