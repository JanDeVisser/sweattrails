/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

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

// The lines of the activity's data panel, in two columns. Fields without
// data in the activity are left out, and so are lines without fields.
std::vector<DataLine> data_fields(Activity const &activity);

}
