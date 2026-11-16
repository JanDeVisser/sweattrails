/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The legs of a multisport activity, one per session of its FIT file: see
 * specs/56-multisport.md.
 */

#pragma once

#include <span>
#include <string>
#include <vector>

#include <storage/Activity.h>

namespace ST {

// The whole activity's segment from its legs: the first's start, the
// last's end, and the sums of their times, distance and ascent. No ascent
// if no leg has one.
Segment whole_segment(std::span<Leg const> legs);

// Gives each leg of `activity`, loaded with its records and the FIT
// file's laps, its records, its laps and the stats of its records, and
// makes the legs the activity's laps.
void split_into_legs(Activity &activity);

// "Swim", "Bike", "Run", "Other" for a leg of no sport in particular,
// "T1", "T2", ... for the transitions in order, and FIT's name for any
// other sport.
std::string leg_name(Activity const &activity, size_t leg);

// The legs that have a tab of their own, after Whole: those that aren't
// transitions.
std::vector<size_t> leg_tabs(Activity const &activity);

// The tab that shows leg `leg`: 1 for the first after Whole, or 0,
// Whole, for a transition, which has none.
size_t tab_of_leg(Activity const &activity, size_t leg);

// Leg `leg` of `whole` as an activity of its own: its sport, numbers,
// records and laps, and the leg's names.
Activity leg_activity(Activity const &whole, size_t leg);

// The labels of a multisport activity's tabs: "Whole", then the tabs'
// legs' names.
std::vector<std::string> tab_labels(Activity const &whole);

// What tab `tab` of a multisport activity shows: the whole activity for
// 0, else leg_activity() of the tab's leg.
Activity tab_activity(Activity const &whole, size_t tab);

// The start the inbox files the activity by: a multisport activity's
// last leg's, as before multisport activities were (and as their names
// in the store are), or else its start.
DateTime filing_start(Activity const &activity);

// The legs' common title, for the whole activity: the longest common
// start of `titles`, the titles of the legs that aren't transitions,
// without the spaces and separators ("|", "-", "·", ":") it ends with,
// or the first title if they have none in common. `titled` says whether
// any of them has a title of its own, Strava's or the user's: if none
// has, there's no common title. See specs/58-multisport-leg-titles.md.
std::optional<std::string> common_title(std::span<std::string const> titles, bool titled);

// A leg's name, "Swim", and its description.
struct LegDescription {
    std::string name { };
    std::string description { };
};

// The legs' descriptions, as the whole activity's: a paragraph each,
// starting with its leg's name, "Swim: Choppy", leaving out the legs
// without one.
std::string legs_description(std::span<LegDescription const> legs);

// What the curves' table calls a best of leg `leg`: the leg's own title,
// Strava's or the user's, or else the whole's title and the leg's name,
// "IM Ottawa · Bike".
std::string best_leg_title(Activity const &whole, size_t leg);

}
