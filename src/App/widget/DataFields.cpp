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
    Duration const pace { seconds_per_km };
    return std::format("{}:{:02} min/km", pace.hours * 60 + pace.minutes, pace.seconds);
}

std::string format_speed(float32 meters_per_second)
{
    return std::format("{:.1f} km/h", meters_per_second * 3.6f);
}

void add_speed_lines(std::vector<DataLine> &lines, Activity const &activity)
{
    auto const &segment = activity.segment;
    auto const  cycling = activity.sport == sport::Cycling;
    lines.push_back(DataLine { .left = DataField { "Distance", format_distance(segment.distance) } });

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

void add_power_lines(std::vector<DataLine> &lines, Segment const &segment)
{
    auto const &power = segment.computed_power_range.range;
    if (!power || power->max < MinRealisticMax) {
        return;
    }
    lines.push_back(DataLine {
        .left = DataField { "Avg Power", std::format("{} W", power->average) },
        .right = DataField { "Max Power", std::format("{} W", power->max), power->max_at },
    });

    // There's only an NP after more than 30 power samples. IF and VI are
    // worked out from it.
    auto const &np = segment.computed_normalized_range.range;
    if (!np || np->normalized == 0) {
        return;
    }
    DataLine np_line { .left = DataField { "NP", std::format("{} W", np->normalized) } };
    if (segment.intensity_factor) {
        np_line.right = DataField { "IF", std::format("{:.2f}", *segment.intensity_factor) };
    }
    lines.push_back(np_line);
    if (segment.variability_index && power->average > 0) {
        lines.push_back(DataLine { .left = DataField { "VI", std::format("{:.2f}", *segment.variability_index) } });
    }
}

void add_elevation_lines(std::vector<DataLine> &lines, Segment const &segment)
{
    auto const &elevation = segment.computed_elevation_range.range;
    if (!elevation) {
        return;
    }
    lines.push_back(DataLine {
        .left = DataField { "Min elevation", std::format("{} m", elevation->min) },
        .right = DataField { "Max elevation", std::format("{} m", elevation->max) },
    });
}

}

std::vector<DataLine> data_fields(Activity const &activity)
{
    auto const           &segment = activity.segment;
    std::vector<DataLine> lines;
    lines.push_back(DataLine {
        .left = DataField { "Moving Time", format_moving_time(segment.moving) },
        .right = DataField { "Elapsed Time", format_elapsed_time(segment.elapsed) },
    });
    if (segment.distance > 0) {
        add_speed_lines(lines, activity);
    }
    add_hr_lines(lines, segment);
    add_power_lines(lines, segment);
    add_elevation_lines(lines, segment);
    return lines;
}

}
