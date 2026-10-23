/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>
#include <optional>
#include <string>
#include <vector>

#include <Date.h>

#include <storage/Activity.h>
#include <widget/DataFields.h>

namespace ST {

namespace {

std::string format_moving_time(Duration const &moving)
{
    if (moving.hours == 0 && moving.minutes == 0) {
        return std::format("{:02}.{:03}", moving.seconds, static_cast<int>(moving.fraction * 1000));
    }
    if (moving.hours == 0) {
        return std::format("{:02}:{:02}", moving.minutes, moving.seconds);
    }
    return std::format("{}:{:02}:{:02}", moving.hours, moving.minutes, moving.seconds);
}

std::string format_elapsed_time(Duration const &elapsed)
{
    if (elapsed.hours == 0) {
        return std::format("{:02}:{:02}.{:03}", elapsed.minutes, elapsed.seconds, static_cast<int>(elapsed.fraction * 1000));
    }
    return std::format("{}:{:02}:{:02}", elapsed.hours, elapsed.minutes, elapsed.seconds);
}

std::string format_distance(float32 meters)
{
    auto const km = meters / 1000.0f;
    if (km < 1.0f) {
        return std::format("{} m", static_cast<int>(meters));
    }
    if (km < 20.0f) {
        return std::format("{:.2f} km", km);
    }
    if (km < 100.0f) {
        return std::format("{:.1f} km", km);
    }
    return std::format("{:.0f} km", km);
}

std::string format_pace(float32 seconds_per_km)
{
    // All of it in minutes, however slow: a watch that logged next to no
    // distance gives a pace of hours, or days, per km.
    Duration const pace { seconds_per_km };
    return std::format("{}:{:02} min/km", pace.elapsed / 60, pace.elapsed % 60);
}

std::string format_speed(float32 meters_per_second)
{
    return std::format("{:.1f} km/h", meters_per_second * 3.6f);
}

// FIT has e-biking as a sport of its own, besides Cycling's e-bike
// sub-sports.
bool is_ride(Activity const &activity)
{
    return activity.sport == sport::Cycling || activity.sport == sport::E_biking;
}

void add_speed_lines(std::vector<DataLine> &lines, Activity const &activity)
{
    auto const &segment = activity.segment;
    auto const  cycling = is_ride(activity);

    DataLine line { };
    if (segment.moving.elapsed > 0) {
        auto const seconds = static_cast<float32>(segment.moving.elapsed);
        line.left = cycling
            ? DataField { "Avg Speed", format_speed(segment.distance / seconds) }
            : DataField { "Avg Pace", format_pace(seconds / (segment.distance / 1000.0f)) };
    }
    auto const &speed = segment.computed_speed_range.range;
    if (speed && speed->max > 0) {
        line.right = cycling
            ? DataField { "Max Speed", format_speed(speed->max), speed->max_at }
            : DataField { "Max Pace", format_pace(1000.0f / speed->max), speed->max_at };
    }
    if (line.left || line.right) {
        lines.push_back(line);
    }
}

void add_hr_lines(std::vector<DataLine> &lines, Segment const &segment)
{
    auto const &hr = segment.computed_hr_range.range;
    if (!hr || hr->max < MinRealisticMax) {
        return;
    }
    lines.push_back(DataLine {
        .left = DataField { "Avg HR", std::format("{} bpm", static_cast<int>(hr->average)) },
        .right = DataField { "Max HR", std::format("{} bpm", static_cast<int>(hr->max)), hr->max_at },
    });
}

bool has_power(Segment const &segment)
{
    auto const &power = segment.computed_power_range.range;
    return power && power->max >= MinRealisticMax;
}

// There's only an NP after more than 30 power samples. IF, VI and TSS are
// worked out from it.
bool has_np(Segment const &segment)
{
    auto const &np = segment.computed_normalized_range.range;
    return has_power(segment) && np && np->normalized > 0;
}

void add_power_lines(std::vector<DataLine> &lines, Segment const &segment)
{
    if (!has_power(segment)) {
        return;
    }
    auto const &power = segment.computed_power_range.range;
    lines.push_back(DataLine {
        .left = DataField { "Avg Power", std::format("{} W", power->average) },
        .right = DataField { "Max Power", std::format("{} W", power->max), power->max_at },
    });

    if (!has_np(segment)) {
        return;
    }
    auto const &np = segment.computed_normalized_range.range;
    DataLine    np_line { .left = DataField { "NP", std::format("{} W", np->normalized) } };
    if (segment.intensity_factor) {
        np_line.right = DataField { "IF", std::format("{:.2f}", *segment.intensity_factor) };
    }
    lines.push_back(np_line);
    if (segment.variability_index && power->average > 0) {
        lines.push_back(DataLine { .left = DataField { "VI", std::format("{:.2f}", *segment.variability_index) } });
    }
}

void add_cadence_lines(std::vector<DataLine> &lines, Activity const &activity)
{
    auto const &range = activity.segment.computed_cadence_range.range;
    auto const &stats = activity.segment.cadence;
    if (!range || range->max < MinRealisticMax || !stats) {
        return;
    }
    // Rides in rpm. FIT has running cadence in strides per minute, one
    // foot's steps, which is shown doubled, as steps per minute.
    auto const ride = is_ride(activity);
    auto const format_cadence = [ride](float32 cadence) {
        return ride ? std::format("{:.0f} rpm", cadence) : std::format("{:.0f} spm", 2 * cadence);
    };
    lines.push_back(DataLine {
        .left = DataField { "Avg Cadence", format_cadence(stats->average) },
        .right = DataField { "Max Cadence", format_cadence(range->max), range->max_at },
    });
    if (stats->min) {
        lines.push_back(DataLine { .left = DataField { "Min Cadence", format_cadence(*stats->min), stats->min_at } });
    }
}

void add_elevation_lines(std::vector<DataLine> &lines, Segment const &segment)
{
    auto const &elevation = segment.computed_elevation_range.range;
    if (!elevation) {
        return;
    }
    lines.push_back(DataLine {
        .left = DataField { "Min elevation", std::format("{} m", elevation->min), elevation->min_at },
        .right = DataField { "Max elevation", std::format("{} m", elevation->max), elevation->max_at },
    });
    lines.push_back(DataLine { .left = DataField { "Climbed", std::format("{:.0f} m", segment.climb()) } });
}

}

std::string activity_summary(Activity const &activity)
{
    auto const              &segment = activity.segment;
    std::vector<std::string> parts;
    if (segment.distance > 0) {
        parts.push_back(format_distance(segment.distance));
    }
    parts.push_back(format_moving_time(segment.moving));
    if (has_np(segment) && segment.tss) {
        parts.push_back(std::format("TSS {:.0f}", *segment.tss));
    }
    std::string summary;
    for (auto const &part : parts) {
        summary += summary.empty() ? part : " · " + part;
    }
    return summary;
}

std::vector<DataLine> data_fields(Activity const &activity)
{
    auto const           &segment = activity.segment;
    std::vector<DataLine> lines;
    // The distance and the moving time are in activity_summary().
    lines.push_back(DataLine { .left = DataField { "Elapsed Time", format_elapsed_time(segment.elapsed) } });
    if (segment.distance > 0) {
        add_speed_lines(lines, activity);
    }
    add_hr_lines(lines, segment);
    add_power_lines(lines, segment);
    add_cadence_lines(lines, activity);
    add_elevation_lines(lines, segment);
    return lines;
}

}
