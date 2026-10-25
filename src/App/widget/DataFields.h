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

// The line under the activity's title: the distance, the active time and
// the TSS, as far as the activity has them, e.g. "42.3 km · 1:32:10 · TSS 87".
std::string activity_summary(Activity const &activity);

// The line under the title while lap `lap` is selected: which lap it is,
// and its summary, e.g. "Lap 3 of 8 · 1.00 km · 4:52".
std::string lap_summary(Activity const &activity, size_t lap);

// The lines of the activity's data panel, in two columns. Fields without
// data in the activity are left out, and so are lines without fields.
std::vector<DataLine> data_fields(Activity const &activity);

// The same for a segment of the activity, such as a lap's, which starts at
// record `first_record`. The fields are chosen by the activity's sport, and
// by the data the segment has. A field's mark_at is an index into the
// activity's records.
std::vector<DataLine> data_fields(Activity const &activity, Segment const &segment, size_t first_record);

// The lap table's columns and rows: Lap and Time, and Distance, Pace or
// Speed, Avg HR, Avg Power, Cadence and Climb if the activity has that
// data. A lap without the data has an empty cell.
struct LapTable {
    std::vector<TableColumn> columns { };
    std::vector<TableRow>    rows { };
};

LapTable lap_table(Activity const &activity);

}
