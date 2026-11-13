/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <Table.h>

#include <storage/Activity.h>
#include <storage/Comparison.h>

namespace ST {

// A heart rate or power maximum below this isn't realistic, such as from a
// sensor that only sent zeros. The data panel then shows no HR, or no power,
// fields.
constexpr int MinRealisticMax = 10;

struct DataField {
    std::string           label { };
    std::string           value { };
    std::optional<size_t> mark_at { }; // The record to mark when it's clicked
};

struct DataLine {
    std::optional<DataField> left { };
    std::optional<DataField> right { };
};

// As the data panel shows them, also in Ctrl+M's list: "42.3 km", or
// "850 m" under a kilometer; "1:32:10", or "41:07" under an hour.
std::string format_distance(float32 meters);
std::string format_moving_time(Duration const &moving);

// A device's maker, as it calls itself rather than as FIT does: "Garmin",
// or FIT's name, such as "favero electronics". Empty for none, or one
// FIT doesn't know.
std::string maker_name(std::optional<FIT::manufacturer> made_by);

// The device that recorded the activity: its maker, with the product name
// if the FIT file has one, e.g. "Wahoo ELEMNT ROAM". Empty if the file
// doesn't say, or names a maker FIT doesn't know.
std::string device_name(Activity const &activity);

// The line under the activity's title: the distance, the active time and
// the TSS, as far as the activity has them, e.g. "42.3 km · 1:32:10 · TSS 87".
// For one of several recordings, which it is, e.g. "· Recording 2 of 2:
// Wahoo", as the lap's and the range's lines say too.
std::string activity_summary(Activity const &activity);

// The line under the title while lap `lap` is selected: which lap it is,
// and its summary, e.g. "Lap 3 of 8 · 1.00 km · 4:52".
std::string lap_summary(Activity const &activity, size_t lap);

// The line under the title while a range dragged in the graph is selected,
// from record `first` to `last`, with `segment` its stats: where it is, by
// distance from the start, and its summary, e.g. "12.4–18.9 km · 6.50 km ·
// 14:21". Without distance, where it is by time from the first record,
// e.g. "0:42:10–1:05:33 · 23:23".
std::string range_summary(Activity const &activity, size_t first, size_t last, Segment const &segment);

// While comparing recordings (specs/33-comparing-recordings.md): one
// number of both, e.g. "245 / 238 W · +7 W (+3%)", the one on display's
// first. One that's missing is "–", with no difference. `scale` is for a
// cadence on foot, which FIT has per leg.
std::string format_compared(Compared const &compared, std::string_view unit, float64 scale = 1);

// What the line under the title adds while comparing, after which
// recording is shown: ", compared with Garmin".
std::string compared_suffix(std::string_view device);

// The graph's colors, which the banner under the cursor uses too. The
// other recording's lines are a lighter shade of them. Light enough to
// read on the dark background.
constexpr Color AltitudeColor = LIGHTGRAY;
constexpr Color SpeedColor = LIME;
constexpr Color PowerColor = SKYBLUE;
constexpr Color HeartRateColor = RED;
constexpr Color CadenceColor = PURPLE;
// The marked record's line across the graph, in none of the lines' colors.
constexpr Color MarkColor = GOLD;

struct GraphLabel {
    std::string text { };
    Color       color { };
};

// Runs, walks and hikes, which give pace rather than speed.
[[nodiscard]] bool is_on_foot(Activity const &activity);
// "5:02 min/km".
[[nodiscard]] std::string format_pace(float32 seconds_per_km);
// The same pace without the unit, "5:02".
[[nodiscard]] std::string pace_minutes(float32 seconds_per_km);

// FIT has a cadence on foot per leg: steps a minute are twice it.
struct CadenceUnit {
    std::string_view unit;
    float64          scale;
};

[[nodiscard]] CadenceUnit cadence_unit(Activity const &activity);

// A heart rate of 0 isn't one: the smoothing for the graph writes it into
// the first records.
[[nodiscard]] std::optional<u8> real_heart_rate(record const &rec);

// The banner under the cursor while comparing: "Pwr 245 / 238 W",
// "HR 142 / 145 bpm", "Cad 88 / 87 rpm", for what either one has.
std::vector<GraphLabel> compare_banner(Activity const &activity, record const &shown, std::optional<record> const &other);

// The lines of the activity's data panel, in two columns. Fields without
// data in the activity are left out, and so are lines without fields.
std::vector<DataLine> data_fields(Activity const &activity);

// The same for a segment of the activity, such as a lap's, which starts at
// record `first_record`. The fields are chosen by the activity's sport, and
// by the data the segment has. A field's mark_at is an index into the
// activity's records.
// While comparing, `comparison`'s lines take the place of the HR, power
// and cadence lines.
std::vector<DataLine> data_fields(Activity const &activity, Segment const &segment, size_t first_record, std::optional<Comparison> const &comparison = { });

// The most lines data_fields() gives for the whole activity or any of its
// laps, so that the data panel can be as high as the most it shows. While
// comparing, `whole` is the whole activity's comparison and `laps_compared`
// the laps', as lap_table() takes them.
[[nodiscard]] size_t most_data_lines(Activity const &activity, std::optional<Comparison> const &whole = { }, std::vector<std::optional<Comparison>> const &laps_compared = { });

// The lap table's columns and rows: Lap and Time, and Distance, Pace or
// Speed, Avg HR, Avg Power, Cadence and Climb if the activity has that
// data. A lap without the data has an empty cell.
struct LapTable {
    std::vector<TableColumn> columns { };
    std::vector<TableRow>    rows { };
};

// While comparing, with the laps' comparisons, a "Δ Power" column: "+7 W".
LapTable lap_table(Activity const &activity, std::vector<std::optional<Comparison>> const &laps_compared = { });

}
