/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

#include <StringUtil.h>

#include <storage/Laps.h>

namespace ST {

namespace {

constexpr std::string_view SplitExample = "Split every distance in km or time in minutes, such as \"1 km\" or \"10 min\".";

i64 time_of(record const &rec)
{
    return static_cast<i64>(rec.timestamp.timestamp);
}

// The first record at or after `time`.
size_t record_at_or_after(std::vector<record> const &records, i64 time)
{
    auto const it = std::ranges::lower_bound(records, time, { }, time_of);
    return static_cast<size_t>(it - records.begin());
}

}

std::expected<Split, std::string> parse_split(std::string_view text)
{
    auto const trimmed = strip(text);
    float32    number { 0 };
    auto const [end, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), number);
    if (ec != std::errc { } || !(number > 0) || !std::isfinite(number)) {
        return std::unexpected(std::string { SplitExample });
    }
    auto const unit = strip(std::string_view { end, trimmed.data() + trimmed.size() });
    if (unit == "km") {
        return Split { .kind = Split::Kind::Distance, .every = number * 1000.0f };
    }
    if (unit == "min") {
        return Split { .kind = Split::Kind::Time, .every = number * 60.0f };
    }
    return std::unexpected(std::string { SplitExample });
}

std::expected<std::vector<i64>, std::string> split_starts(Activity const &activity, Split const &split)
{
    auto const &records = activity.recorded;
    if (records.empty()) {
        return std::vector<i64> { };
    }
    // How far each record is from the start, in the split's unit.
    auto const from_start = [&records, &split](record const &rec) -> std::optional<float32> {
        if (split.kind == Split::Kind::Time) {
            return static_cast<float32>(time_of(rec) - time_of(records.front()));
        }
        if (!rec.distance || !records.front().distance) {
            return { };
        }
        return *rec.distance - *records.front().distance;
    };
    if (split.kind == Split::Kind::Distance && !from_start(records.back())) {
        return std::unexpected(std::string { "This activity has no distance to split by: split it by time, such as \"10 min\"." });
    }
    auto const total = from_start(records.back()).value_or(0);
    if (total / split.every > static_cast<float32>(MaxLaps)) {
        return std::unexpected(std::format("That would make more than {} laps: split by more.", MaxLaps));
    }
    std::vector<i64> starts;
    auto             next = split.every;
    for (auto const &rec : records) {
        auto const at = from_start(rec);
        if (at && *at >= next) {
            starts.push_back(time_of(rec));
            // A record past more than one multiple, after a gap, starts one
            // lap: the next starts at the multiple after it.
            next = (std::floor(*at / split.every) + 1) * split.every;
        }
    }
    return starts;
}

std::vector<i64> mark_starts(Activity const &activity, std::vector<i64> const &starts, size_t first, size_t last)
{
    auto const &records = activity.recorded;
    if (first > last || last >= records.size()) {
        return starts;
    }
    auto const snap = [&starts](i64 time) {
        for (auto const start : starts) {
            if (std::abs(start - time) <= MarkSnapSeconds) {
                return start;
            }
        }
        return time;
    };
    auto const       from = snap(time_of(records[first]));
    auto const       to = (last + 1 < records.size()) ? std::optional { snap(time_of(records[last + 1])) } : std::nullopt;
    std::vector<i64> ret;
    for (auto const start : starts) {
        if (start < from || (to && start > *to)) {
            ret.push_back(start);
        }
    }
    ret.push_back(from);
    if (to) {
        ret.push_back(*to);
    }
    std::ranges::sort(ret);
    auto const duplicates = std::ranges::unique(ret);
    ret.erase(duplicates.begin(), duplicates.end());
    return ret;
}

std::vector<Lap> laps_at(Activity const &activity, std::vector<i64> const &starts)
{
    auto const &records = activity.recorded;
    if (records.empty()) {
        return { };
    }
    std::vector<size_t> firsts { 0 };
    auto                sorted = starts;
    std::ranges::sort(sorted);
    for (auto const start : sorted) {
        auto const first = record_at_or_after(records, start);
        if (first > firsts.back() && first < records.size()) {
            firsts.push_back(first);
        }
    }
    std::vector<Lap> laps;
    for (size_t ix = 0; ix < firsts.size(); ++ix) {
        auto const end = (ix + 1 < firsts.size()) ? firsts[ix + 1] : records.size();
        Lap        lap { .first_record = firsts[ix], .end_record = end };
        // Up to the next lap's first record, as a device's lap runs up to
        // the next one's start: the laps add up to the activity.
        auto const last = (end < records.size()) ? end : end - 1;
        if (auto segment = range_segment(activity, firsts[ix], last); segment) {
            lap.segment = std::move(*segment);
        }
        laps.push_back(std::move(lap));
    }
    return laps;
}

std::vector<i64> device_lap_starts(Activity const &activity)
{
    std::vector<i64> starts;
    for (size_t ix = 1; ix < activity.device_laps.size(); ++ix) {
        auto const &lap = activity.device_laps[ix];
        if (lap.first_record < activity.recorded.size() && lap.end_record > lap.first_record) {
            starts.push_back(time_of(activity.recorded[lap.first_record]));
        }
    }
    return starts;
}

}
