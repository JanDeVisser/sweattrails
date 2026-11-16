/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <ranges>

#include <storage/Legs.h>

namespace ST {

using namespace ST::FIT;

Segment whole_segment(std::span<Leg const> legs)
{
    if (legs.empty()) {
        return { };
    }
    Segment ret {
        .start_time = legs.front().segment.start_time,
        .end_time = legs.back().segment.end_time,
    };
    // With their fractions of a second: whole seconds would lose up to one
    // a leg.
    auto const seconds = [](Duration const &duration) {
        return static_cast<float64>(duration.elapsed) + static_cast<float64>(duration.fraction);
    };
    float64 elapsed { 0 };
    float64 moving { 0 };
    for (auto const &leg : legs) {
        elapsed += seconds(leg.segment.elapsed);
        moving += seconds(leg.segment.moving);
        ret.distance += leg.segment.distance;
        if (leg.segment.total_ascent) {
            ret.total_ascent = static_cast<u16>(ret.total_ascent.value_or(0) + *leg.segment.total_ascent);
        }
    }
    ret.elapsed = Duration { elapsed };
    ret.moving = Duration { moving };
    return ret;
}

namespace {

// The device's laps in leg `ix`: those the session lists, or, if it
// doesn't, or lists laps the file doesn't have, those that start within
// it.
std::vector<Lap> laps_of_leg(std::vector<Leg> const &legs, size_t ix, std::vector<Lap> const &laps)
{
    auto const &leg = legs[ix];
    if (leg.first_lap_index && leg.num_laps && *leg.first_lap_index + *leg.num_laps <= laps.size()) {
        auto const first = laps.begin() + *leg.first_lap_index;
        return { first, first + *leg.num_laps };
    }
    auto const from = leg.segment.start_time.timestamp;
    auto const within = [&legs, ix, from](Lap const &lap) {
        auto const start = lap.segment.start_time.timestamp;
        return start >= from && (ix + 1 == legs.size() || start < legs[ix + 1].segment.start_time.timestamp);
    };
    return laps | std::views::filter(within) | std::ranges::to<std::vector>();
}

}

void split_into_legs(Activity &activity)
{
    auto      &records = activity.records;
    auto      &legs = activity.legs;
    auto const first_at_or_after = [&records](DateTime const &t) {
        auto const at = std::ranges::lower_bound(records, t.timestamp, { }, [](record const &rec) { return rec.timestamp.timestamp; });
        return static_cast<size_t>(at - records.begin());
    };
    for (size_t ix = 0; ix < legs.size(); ++ix) {
        auto &leg = legs[ix];
        leg.first_record = first_at_or_after(leg.segment.start_time);
        leg.end_record = (ix + 1 < legs.size()) ? first_at_or_after(legs[ix + 1].segment.start_time) : records.size();
        leg.end_record = std::max(leg.end_record, leg.first_record);
        leg.segment.analyze(std::span { records }.subspan(leg.first_record, leg.end_record - leg.first_record), false);
        leg.laps = laps_of_leg(legs, ix, activity.laps);
        // Without a sidecar, read_names() gives it none.
        if (leg.title.empty()) {
            leg.title = leg.original_title = leg_name(activity, ix);
        }
    }
    activity.laps.clear();
    for (size_t ix = 0; ix < legs.size(); ++ix) {
        activity.laps.push_back(Lap {
            .first_record = legs[ix].first_record,
            .end_record = legs[ix].end_record,
            .title = leg_name(activity, ix),
            .segment = legs[ix].segment,
        });
    }
}

std::string leg_name(Activity const &activity, size_t leg)
{
    auto const &legs = activity.legs;
    switch (legs[leg].sport.value_or(sport::Generic)) {
    case sport::Swimming:
        return "Swim";
    case sport::Cycling:
    case sport::E_biking:
        return "Bike";
    case sport::Running:
        return "Run";
    case sport::Generic:
        return "Other";
    case sport::Transition: {
        auto const before = std::ranges::count_if(legs.begin(), legs.begin() + static_cast<std::ptrdiff_t>(leg), [](Leg const &other) {
            return other.sport == sport::Transition;
        });
        return std::format("T{}", before + 1);
    }
    default: {
        std::string ret { tag(*legs[leg].sport) };
        std::ranges::replace(ret, '_', ' ');
        return ret;
    }
    }
}

std::vector<size_t> leg_tabs(Activity const &activity)
{
    std::vector<size_t> ret;
    for (size_t ix = 0; ix < activity.legs.size(); ++ix) {
        if (activity.legs[ix].sport != sport::Transition) {
            ret.push_back(ix);
        }
    }
    return ret;
}

size_t tab_of_leg(Activity const &activity, size_t leg)
{
    auto const tabs = leg_tabs(activity);
    auto const tab = std::ranges::find(tabs, leg);
    return (tab == tabs.end()) ? 0 : static_cast<size_t>(tab - tabs.begin()) + 1;
}

Activity leg_activity(Activity const &whole, size_t leg)
{
    auto const &the_leg = whole.legs[leg];
    auto const  first = the_leg.first_record;
    auto const  count = the_leg.end_record - first;
    auto const  slice = [first, count](std::vector<record> const &records) {
        // `recorded` is empty on a shallow load.
        if (first + count > records.size()) {
            return std::vector<record> { };
        }
        auto const from = records.begin() + static_cast<std::ptrdiff_t>(first);
        return std::vector<record> { from, from + static_cast<std::ptrdiff_t>(count) };
    };
    Activity ret { whole };
    ret.sport = the_leg.sport.value_or(sport::Generic);
    ret.sub_sport = the_leg.sub_sport;
    ret.segment = the_leg.segment;
    ret.records = slice(whole.records);
    ret.recorded = slice(whole.recorded);
    ret.laps.clear();
    for (auto lap : the_leg.laps) {
        lap.first_record = std::clamp(lap.first_record, first, first + count) - first;
        lap.end_record = std::clamp(lap.end_record, first, first + count) - first;
        ret.laps.push_back(std::move(lap));
    }
    ret.device_laps = ret.laps;
    ret.user_lap_starts = { };
    ret.legs.clear();
    ret.leg = LegShown {
        .name = leg_name(whole, leg),
        .number = tab_of_leg(whole, leg),
        .count = leg_tabs(whole).size(),
        .index = leg,
    };
    ret.title = the_leg.title;
    ret.original_title = the_leg.original_title;
    ret.notes = the_leg.notes;
    ret.original_notes = the_leg.original_notes;
    return ret;
}

std::vector<std::string> tab_labels(Activity const &whole)
{
    std::vector<std::string> ret { "Whole" };
    for (auto const leg : leg_tabs(whole)) {
        ret.push_back(leg_name(whole, leg));
    }
    return ret;
}

Activity tab_activity(Activity const &whole, size_t tab)
{
    auto const tabs = leg_tabs(whole);
    if (tab == 0 || tab > tabs.size()) {
        return whole;
    }
    return leg_activity(whole, tabs[tab - 1]);
}

DateTime filing_start(Activity const &activity)
{
    return activity.is_multisport() ? activity.legs.back().segment.start_time : activity.segment.start_time;
}

std::optional<std::string> common_title(std::span<std::string const> titles, bool titled)
{
    if (!titled || titles.empty()) {
        return { };
    }
    std::string_view common { titles.front() };
    for (auto const &title : titles.subspan(1)) {
        auto const [here, there] = std::ranges::mismatch(common, title);
        common = common.substr(0, static_cast<size_t>(here - common.begin()));
    }
    // Whole words only: "Run 1" and "Ride" have "R" in common, which isn't
    // a title. Cutting at a space or a separator, which are ASCII, is
    // never halfway through a UTF-8 character either.
    auto const boundary = [](char c) { return std::string_view { " |-:" }.contains(c); };
    auto const mid_word = std::ranges::any_of(titles, [&common, &boundary](std::string const &title) {
        return title.size() > common.size() && !boundary(title[common.size()]);
    });
    if (mid_word && !common.empty() && !boundary(common.back())) {
        auto const last = std::ranges::find_if(common | std::views::reverse, boundary);
        common = common.substr(0, static_cast<size_t>(common.rend() - last));
    }
    auto const trim_end = [&common]() {
        for (std::string_view const separator : { " ", "|", "-", "·", ":" }) {
            if (common.ends_with(separator)) {
                common.remove_suffix(separator.size());
                return true;
            }
        }
        return false;
    };
    while (trim_end()) { }
    return common.empty() ? titles.front() : std::string { common };
}

std::string legs_description(std::span<LegDescription const> legs)
{
    std::string ret;
    for (auto const &leg : legs) {
        auto const description = strip(leg.description);
        if (description.empty()) {
            continue;
        }
        if (!ret.empty()) {
            ret += "\n\n";
        }
        ret += std::format("{}: {}", leg.name, description);
    }
    return ret;
}

std::string best_leg_title(Activity const &whole, size_t leg)
{
    auto const  name = leg_name(whole, leg);
    auto const &title = whole.legs[leg].title;
    if (!title.empty() && title != name) {
        return title;
    }
    return whole.title.empty() ? name : std::format("{} · {}", whole.title, name);
}

}
