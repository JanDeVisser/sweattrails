/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <Date.h>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <storage/Thresholds.h>
#include <storage/Volume.h>
#include <widget/DataFields.h>

namespace ST {

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

namespace {

std::string format_elapsed_time(Duration const &elapsed)
{
    if (elapsed.hours == 0) {
        return std::format("{:02}:{:02}.{:03}", elapsed.minutes, elapsed.seconds, static_cast<int>(elapsed.fraction * 1000));
    }
    return std::format("{}:{:02}:{:02}", elapsed.hours, elapsed.minutes, elapsed.seconds);
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

bool is_on_foot(Activity const &activity)
{
    return activity.sport == sport::Running || activity.sport == sport::Walking || activity.sport == sport::Hiking;
}

// The lines below take the segment whose fields they add, the activity's
// or a lap's, and the index of its first record: the ranges' min_at and
// max_at are relative to it, mark_at is an index into the activity's
// records.

void add_speed_lines(std::vector<DataLine> &lines, Activity const &activity, Segment const &segment, size_t first)
{
    auto const cycling = is_ride(activity);

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
            ? DataField { "Max Speed", format_speed(speed->max), first + speed->max_at }
            : DataField { "Max Pace", format_pace(1000.0f / speed->max), first + speed->max_at };
    }
    if (line.left || line.right) {
        lines.push_back(line);
    }
}

bool has_hr(Segment const &segment)
{
    auto const &hr = segment.computed_hr_range.range;
    return hr && hr->max >= MinRealisticMax;
}

void add_hr_lines(std::vector<DataLine> &lines, Segment const &segment, size_t first)
{
    if (!has_hr(segment)) {
        return;
    }
    auto const &hr = segment.computed_hr_range.range;
    lines.push_back(DataLine {
        .left = DataField { "Avg HR", std::format("{} bpm", static_cast<int>(hr->average)) },
        .right = DataField { "Max HR", std::format("{} bpm", static_cast<int>(hr->max)), first + hr->max_at },
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

// The thresholds in force on the activity's day, if the user set any.
std::optional<Thresholds> activity_thresholds(Activity const &activity)
{
    if (activity.storage == nullptr) {
        return { };
    }
    return thresholds_on(activity.storage->thresholds, day_of(activity.segment.start_time));
}

std::optional<float64> segment_np(Segment const &segment)
{
    if (!has_np(segment)) {
        return { };
    }
    return segment.computed_normalized_range.range->normalized;
}

void add_power_lines(std::vector<DataLine> &lines, Activity const &activity, Segment const &segment, size_t first)
{
    if (!has_power(segment)) {
        return;
    }
    auto const &power = segment.computed_power_range.range;
    lines.push_back(DataLine {
        .left = DataField { "Avg Power", std::format("{} W", power->average) },
        .right = DataField { "Max Power", std::format("{} W", power->max), first + power->max_at },
    });

    if (!has_np(segment)) {
        return;
    }
    auto const &np = segment.computed_normalized_range.range;
    DataLine    np_line { .left = DataField { "NP", std::format("{} W", np->normalized) } };
    if (auto const intensity = intensity_factor(segment_np(segment), activity_thresholds(activity)); intensity) {
        np_line.right = DataField { "IF", std::format("{:.2f}", *intensity) };
    }
    lines.push_back(np_line);
    if (segment.variability_index && power->average > 0) {
        lines.push_back(DataLine { .left = DataField { "VI", std::format("{:.2f}", *segment.variability_index) } });
    }
}

// Rides in rpm. FIT has running and walking cadence in strides per
// minute, one foot's steps, which is shown doubled, as steps per minute.
// Other sports, such as swimming or rowing, are in strokes per minute.
std::string format_cadence(Activity const &activity, float32 cadence)
{
    if (is_ride(activity)) {
        return std::format("{:.0f} rpm", cadence);
    }
    if (is_on_foot(activity)) {
        return std::format("{:.0f} spm", 2 * cadence);
    }
    return std::format("{:.0f}/min", cadence);
}

bool has_cadence(Segment const &segment)
{
    auto const &range = segment.computed_cadence_range.range;
    return range && range->max >= MinRealisticMax && segment.cadence;
}

void add_cadence_lines(std::vector<DataLine> &lines, Activity const &activity, Segment const &segment, size_t first)
{
    if (!has_cadence(segment)) {
        return;
    }
    auto const &range = segment.computed_cadence_range.range;
    auto const &stats = segment.cadence;
    lines.push_back(DataLine {
        .left = DataField { "Avg Cadence", format_cadence(activity, stats->average) },
        .right = DataField { "Max Cadence", format_cadence(activity, range->max), first + range->max_at },
    });
    if (stats->min) {
        lines.push_back(DataLine { .left = DataField { "Min Cadence", format_cadence(activity, *stats->min), first + stats->min_at } });
    }
}

void add_elevation_lines(std::vector<DataLine> &lines, Segment const &segment, size_t first)
{
    auto const &elevation = segment.computed_elevation_range.range;
    if (!elevation) {
        return;
    }
    lines.push_back(DataLine {
        .left = DataField { "Min elevation", std::format("{} m", elevation->min), first + elevation->min_at },
        .right = DataField { "Max elevation", std::format("{} m", elevation->max), first + elevation->max_at },
    });
    lines.push_back(DataLine { .left = DataField { "Climbed", std::format("{:.0f} m", segment.climb()) } });
}

// The summary of any segment, the activity's or a lap's.
std::string segment_summary(Activity const &activity, Segment const &segment)
{
    std::vector<std::string> parts;
    if (segment.distance > 0) {
        parts.push_back(format_distance(segment.distance));
    }
    parts.push_back(format_moving_time(segment.moving));
    // Over the moving time: the stops weren't training.
    if (auto const stress = training_stress(segment_np(segment), segment.moving.elapsed, segment.hr_load, activity.sport, activity.sub_sport, activity_thresholds(activity)); stress) {
        parts.push_back(std::format("{} {:.0f}", stress->heart_rate ? "hrTSS" : "TSS", std::trunc(stress->value)));
    }
    std::string summary;
    for (auto const &part : parts) {
        summary += summary.empty() ? part : " · " + part;
    }
    return summary;
}

}

std::string activity_summary(Activity const &activity)
{
    return segment_summary(activity, activity.segment);
}

std::string lap_summary(Activity const &activity, size_t lap)
{
    return std::format("Lap {} of {} · {}", lap + 1, activity.laps.size(), segment_summary(activity, activity.laps[lap].segment));
}

std::string range_summary(Activity const &activity, size_t first, size_t last, Segment const &segment)
{
    auto const &records = activity.records;
    if (first > last || last >= records.size()) {
        return segment_summary(activity, segment);
    }
    auto const &from = records[first];
    auto const &to = records[last];
    // By whether the records have distance, not whether the range went
    // anywhere: a range at a stop says where it is in km, as the others do.
    if (from.distance && to.distance) {
        return std::format("{:.1f}–{:.1f} km · {}", *from.distance / 1000.0f, *to.distance / 1000.0f, segment_summary(activity, segment));
    }
    auto const since_start = [&records](record const &rec) {
        Duration const since { rec.timestamp.timestamp - records.front().timestamp.timestamp };
        return std::format("{}:{:02}:{:02}", since.elapsed / Duration::SECONDS_PER_HOUR, since.minutes, since.seconds);
    };
    return std::format("{}–{} · {}", since_start(from), since_start(to), segment_summary(activity, segment));
}

std::vector<DataLine> data_fields(Activity const &activity, Segment const &segment, size_t first_record)
{
    std::vector<DataLine> lines;
    // The distance and the moving time are in the summary.
    lines.push_back(DataLine { .left = DataField { "Elapsed Time", format_elapsed_time(segment.elapsed) } });
    if (segment.distance > 0) {
        add_speed_lines(lines, activity, segment, first_record);
    }
    add_hr_lines(lines, segment, first_record);
    add_power_lines(lines, activity, segment, first_record);
    add_cadence_lines(lines, activity, segment, first_record);
    add_elevation_lines(lines, segment, first_record);
    return lines;
}

std::vector<DataLine> data_fields(Activity const &activity)
{
    return data_fields(activity, activity.segment, 0);
}

// A column for each kind of data the activity has. The activity's segment
// decides, so that a column doesn't come and go with the laps on screen.
LapTable lap_table(Activity const &activity)
{
    using Cell = std::function<std::string(Segment const &)>;
    struct Column {
        TableColumn column;
        Cell        cell;
    };

    auto const         &whole = activity.segment;
    std::vector<Column> columns;
    columns.push_back({ { .header = "Time", .right_aligned = true }, [](Segment const &lap) {
                           return format_moving_time(lap.moving);
                       } });
    if (whole.distance > 0) {
        columns.push_back({ { .header = "Distance", .right_aligned = true }, [](Segment const &lap) {
                               return lap.distance > 0 ? format_distance(lap.distance) : std::string { };
                           } });
        auto const cycling = is_ride(activity);
        columns.push_back({ { .header = cycling ? "Speed" : "Pace", .right_aligned = true }, [cycling](Segment const &lap) {
                               if (lap.distance <= 0 || lap.moving.elapsed <= 0) {
                                   return std::string { };
                               }
                               auto const seconds = static_cast<float32>(lap.moving.elapsed);
                               return cycling ? format_speed(lap.distance / seconds) : format_pace(seconds / (lap.distance / 1000.0f));
                           } });
    }
    if (has_hr(whole)) {
        columns.push_back({ { .header = "Avg HR", .right_aligned = true }, [](Segment const &lap) {
                               return has_hr(lap) ? std::format("{} bpm", static_cast<int>(lap.computed_hr_range.range->average)) : std::string { };
                           } });
    }
    if (has_power(whole)) {
        columns.push_back({ { .header = "Avg Power", .right_aligned = true }, [](Segment const &lap) {
                               return has_power(lap) ? std::format("{} W", lap.computed_power_range.range->average) : std::string { };
                           } });
    }
    if (has_cadence(whole)) {
        columns.push_back({ { .header = "Cadence", .right_aligned = true }, [&activity](Segment const &lap) {
                               return has_cadence(lap) ? format_cadence(activity, lap.cadence->average) : std::string { };
                           } });
    }
    if (whole.computed_elevation_range.range) {
        columns.push_back({ { .header = "Climb", .right_aligned = true }, [](Segment const &lap) {
                               return lap.computed_elevation_range.range ? std::format("{:.0f} m", lap.climb()) : std::string { };
                           } });
    }

    LapTable table;
    table.columns.push_back({ .header = "Lap", .right_aligned = true });
    for (auto const &column : columns) {
        table.columns.push_back(column.column);
    }
    for (auto ix = 0uz; ix < activity.laps.size(); ++ix) {
        TableRow row { std::format("{}", ix + 1) };
        for (auto const &column : columns) {
            row.push_back(column.cell(activity.laps[ix].segment));
        }
        table.rows.push_back(std::move(row));
    }
    return table;
}

}
