/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * What rendering an overlay to video needs that isn't drawing or ffmpeg:
 * the settings, the range, the frames' times, the timecode, the file's
 * name, finding ffmpeg, and straight alpha. See specs/video-overlay.md.
 */

#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <JSON.h>

#include <overlay/Layout.h>
#include <storage/Activity.h>

namespace ST {

namespace fs = std::filesystem;

// Frames per second, exactly: 30000/1001 is 29.97.
struct FrameRate {
    i64 num { 30000 };
    i64 den { 1001 };

    [[nodiscard]] double      fps() const { return static_cast<double>(num) / static_cast<double>(den); }
    [[nodiscard]] std::string to_string() const; // For ffmpeg's -r: "30000/1001"

    bool operator==(FrameRate const &) const = default;
};

struct Resolution {
    int width { 1920 };
    int height { 1080 };

    bool operator==(Resolution const &) const = default;
};

struct RenderSettings {
    Resolution resolution { };
    FrameRate  fps { };
    fs::path   folder { };
};

// `settings.json`'s "overlay" object, or nothing for the defaults: 1080p,
// 29.97 and ~/Movies. `home` replaces a leading `~` in the folder.
std::expected<RenderSettings, std::string> decode_render_settings(std::optional<JSONValue> const &overlay, fs::path const &home);

// Seconds since the activity's start.
struct RenderRange {
    double from { 0 };
    double to { 0 };

    bool operator==(RenderRange const &) const = default;
};

// What a range is read against: how long the activity is, from its start
// to its last record, and the local time of day it started at, in seconds
// since midnight where it was, by utc_offset, or else the system's time
// zone. `date` is that day, as "2025-06-14".
struct ActivityClock {
    double      duration { 0 };
    int         start_time_of_day { 0 };
    std::string date { };
};

ActivityClock activity_clock(Activity const &activity);

// `1:12:00-1:20:30`, elapsed, or `@09:44:00-@09:52:30`, times of day; cut to
// the activity. An error for a range that doesn't parse, ends before it
// starts, or isn't in the activity at all.
std::expected<RenderRange, std::string> parse_range(std::string_view text, ActivityClock const &clock);
// As elapsed times, for the box to start with.
std::string format_range(RenderRange const &range);
// The time records `first` to `last` take up, as the graph's selection or
// a lap has them.
RenderRange records_range(Activity const &activity, size_t first, size_t last);

// The range a render starts its box with, and the preview shows: the
// graph's selection of records `first` to `last`, which is the lap when a
// lap is selected; the whole activity without one, or for a selection of
// one record, which isn't a range.
RenderRange initial_render_range(Activity const &activity, std::optional<std::pair<size_t, size_t>> selection);

// Where the preview's moment, `elapsed` seconds into the activity, would
// be in a video of the initial range. At the range's very end, which is
// the selection's last record, it's at the video's last frame, which is
// what the video shows there, rather than just past it.
VideoTime preview_time(Activity const &activity, double elapsed, std::optional<std::pair<size_t, size_t>> selection);

// The frames in the range, and when frame `n` is, from integers rather
// than added up, so that a long render doesn't drift.
size_t frame_count(RenderRange const &range, FrameRate const &rate);
double frame_time(RenderRange const &range, FrameRate const &rate, size_t n);

// The timecode at a time of day: HH:MM:SS:FF, or drop frame, HH:MM:SS;FF,
// for 29.97 and 59.94.
std::string timecode(double seconds_since_midnight, FrameRate const &rate);

// `<folder>/<date>-<HHMM>.overlay.mov` for the range's start, with a number
// added while `exists` says the name is taken.
fs::path output_path(fs::path const &folder, ActivityClock const &clock, RenderRange const &range, std::function<bool(fs::path const &)> const &exists);

// ffmpeg's arguments, after the program, for frames of RGBA on its stdin,
// upside down as OpenGL reads them, to ProRes 4444 with the time of day's
// timecode.
std::vector<std::string> ffmpeg_arguments(RenderSettings const &settings, ActivityClock const &clock, RenderRange const &range, fs::path const &output);

// The program in one of the `PATH`'s directories, or else in one of
// `extra`, if it's there and can be run.
std::optional<fs::path> find_program(std::string_view name, std::string_view path, std::vector<fs::path> const &extra);

// About how big the file will be, in GB: ProRes 4444 is some 2.5 GB a
// minute at 1080p.
double estimated_gigabytes(Resolution const &resolution, RenderRange const &range);

// Turns the colors of RGBA pixels drawn with premultiplied alpha into
// straight ones, which ProRes 4444 holds.
void unpremultiply(std::span<unsigned char> rgba);

}
