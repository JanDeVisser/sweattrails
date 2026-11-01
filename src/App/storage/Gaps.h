/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Gaps in an activity, where the watch was stopped and moved while it
 * was, filled with the track of another activity of the same stretch. See
 * specs/30-fill-gaps.md.
 */

#pragma once

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Date.h>

#include <Coordinates.h>
#include <fit/FITPatch.h>
#include <fit/Profile.h>

namespace ST {

struct Activity;

// A gap: more than this long, and this far, between two records.
constexpr u32     GapTime = 90;      // s
constexpr float64 GapDistance = 100; // m
// The other activity covers a gap if it has records this close to the
// stop and the restart, in time and in place.
constexpr u32     CoverTime = 10;      // s
constexpr float64 CoverDistance = 200; // m

struct Gap {
    size_t      before { 0 }; // The record before it
    DateTime    stop { };
    DateTime    restart { };
    Coordinates from { };
    Coordinates to { };
};

[[nodiscard]] std::vector<Gap> find_gaps(std::span<record const> records);

// A fill for each gap that `other`, the records of another activity,
// covers. The fills' distances go on from the activity's at the stop.
[[nodiscard]] std::vector<FIT::FITFill> plan_fills(std::span<record const> records, std::vector<Gap> const &gaps, std::span<record const> other);

// "Fill 2 gaps: 1.4 km at 0:23:10, 1.6 km at 0:41:05?", the times since
// the activity's `start`.
[[nodiscard]] std::string fill_question(std::vector<FIT::FITFill> const &fills, DateTime const &start);

// What's wrong with dropping `paths` on the window, if anything.
[[nodiscard]] std::optional<std::string> drop_problem(std::vector<std::string> const &paths, bool showing_activity);

// The activity's FIT file, and where its original is kept once it's
// filled: `<start>.fit.original`.
[[nodiscard]] std::filesystem::path fit_path(Activity const &activity);
[[nodiscard]] std::filesystem::path original_path(Activity const &activity);

// Patches the fills into the activity's FIT file, keeping the original
// the first time. Why not, if it can't.
std::expected<void, std::string> fill_gaps(Activity const &activity, std::vector<FIT::FITFill> const &fills);

// Puts the original back.
std::expected<void, std::string> remove_fills(Activity const &activity);

}
