/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <charconv>
#include <cmath>
#include <ctime>
#include <format>
#include <map>
#include <system_error>

#include <unistd.h>

#include <StringUtil.h>

#include <overlay/Render.h>

namespace ST {

namespace {

constexpr int SecondsPerDay = 24 * 3600;

std::string string_of(JSONValue const &json)
{
    std::string ret;
    (void) json.convert(ret);
    return ret;
}

std::optional<i64> integer(std::string_view text)
{
    i64 ret { 0 };
    auto const [end, err] = std::from_chars(text.data(), text.data() + text.size(), ret);
    if (err != std::errc { } || end != text.data() + text.size()) {
        return { };
    }
    return ret;
}

std::optional<Resolution> preset(std::string_view name)
{
    static std::map<std::string, Resolution, std::less<>> const presets {
        { "720p", { 1280, 720 } },
        { "1080p", { 1920, 1080 } },
        { "1440p", { 2560, 1440 } },
        { "4k", { 3840, 2160 } },
    };
    auto       base = name;
    auto const portrait = base.ends_with("-portrait");
    if (portrait) {
        base.remove_suffix(9);
    }
    auto const it = presets.find(base);
    if (it == presets.end()) {
        return { };
    }
    return portrait ? Resolution { .width = it->second.height, .height = it->second.width } : it->second;
}

std::expected<Resolution, std::string> resolution(JSONValue const &json)
{
    auto const error = std::unexpected(std::string { "\"resolution\" isn't 720p, 1080p, 1440p or 4k, with or without -portrait, nor a width and a height" });
    if (json.is_string()) {
        if (auto const r = preset(string_of(json)); r) {
            return *r;
        }
        return error;
    }
    auto const width = json.try_get<int>("width");
    auto const height = json.try_get<int>("height");
    if (!json.is_object() || !width || !height || *width <= 0 || *height <= 0) {
        return error;
    }
    // ProRes, like most codecs, wants an even width and height.
    if (*width % 2 != 0 || *height % 2 != 0) {
        return std::unexpected(std::string { "\"resolution\": the width and the height have to be even" });
    }
    return Resolution { .width = *width, .height = *height };
}

std::expected<FrameRate, std::string> frame_rate(JSONValue const &json)
{
    auto const error = std::unexpected(std::string { "\"fps\" isn't a frame rate, such as 25, \"30000/1001\" or \"29.97\"" });
    if (json.is_integer()) {
        i64 n { 0 };
        if (json.convert(n) && n > 0) {
            return FrameRate { .num = n, .den = 1 };
        }
        return error;
    }
    if (!json.is_string()) {
        return error;
    }
    auto const text = string_of(json);
    // The NTSC rates, as they're usually written.
    static std::map<std::string, FrameRate, std::less<>> const ntsc {
        { "23.976", { 24000, 1001 } },
        { "29.97", { 30000, 1001 } },
        { "59.94", { 60000, 1001 } },
    };
    if (auto const it = ntsc.find(text); it != ntsc.end()) {
        return it->second;
    }
    auto const slash = text.find('/');
    auto const num = integer(std::string_view { text }.substr(0, slash));
    auto const den = (slash == std::string::npos) ? std::optional<i64> { 1 } : integer(std::string_view { text }.substr(slash + 1));
    if (!num || !den || *num <= 0 || *den <= 0) {
        return error;
    }
    return FrameRate { .num = *num, .den = *den };
}

// `h:mm:ss`, or `mm:ss`, in seconds.
std::optional<int> clock_time(std::string_view text)
{
    auto const parts = split(text, ':');
    if (parts.size() < 2 || parts.size() > 3) {
        return { };
    }
    int ret { 0 };
    for (auto ix = 0uz; ix < parts.size(); ++ix) {
        auto const part = integer(strip(parts[ix]));
        if (!part || *part < 0 || (ix > 0 && (*part > 59 || strip(parts[ix]).length() != 2))) {
            return { };
        }
        ret = ret * 60 + static_cast<int>(*part);
    }
    return ret;
}

// A point of the range in seconds since the start, or nothing.
std::optional<double> range_point(std::string_view text, ActivityClock const &clock)
{
    text = strip(text);
    if (!text.starts_with('@')) {
        auto const t = clock_time(text);
        return t ? std::optional<double> { *t } : std::nullopt;
    }
    // A time of day without seconds is `h:mm`, not minutes and seconds.
    text = text.substr(1);
    auto const t = clock_time((split(text, ':').size() == 2) ? std::format("{}:00", text) : std::string { text });
    if (!t || *t >= SecondsPerDay) {
        return { };
    }
    // Long before the start is after midnight, on a ride through it. A
    // little before the start is cut to the start, like any range.
    auto elapsed = *t - clock.start_time_of_day;
    if (elapsed < -SecondsPerDay / 2) {
        elapsed += SecondsPerDay;
    }
    return elapsed;
}

std::string clock_text(double seconds)
{
    auto const total = static_cast<long>(std::max(0.0, std::round(seconds)));
    return std::format("{}:{:02}:{:02}", total / 3600, (total / 60) % 60, total % 60);
}

}

std::string FrameRate::to_string() const
{
    return (den == 1) ? std::format("{}", num) : std::format("{}/{}", num, den);
}

std::expected<RenderSettings, std::string> decode_render_settings(std::optional<JSONValue> const &overlay, fs::path const &home)
{
    RenderSettings ret { .folder = home / "Movies" };
    if (!overlay || overlay->is_null()) {
        return ret;
    }
    if (!overlay->is_object()) {
        return std::unexpected(std::string { "\"overlay\" isn't an object" });
    }
    if (auto const r = overlay->get("resolution"); r) {
        ret.resolution = TRY_EVAL(resolution(*r));
    }
    if (auto const fps = overlay->get("fps"); fps) {
        ret.fps = TRY_EVAL(frame_rate(*fps));
    }
    if (auto const folder = overlay->get("folder"); folder) {
        if (!folder->is_string() || string_of(*folder).empty()) {
            return std::unexpected(std::string { "\"folder\" isn't a folder's path" });
        }
        auto const text = string_of(*folder);
        if (text == "~" || text.starts_with("~/")) {
            ret.folder = home / std::string_view { text }.substr(std::min<size_t>(2, text.length()));
        } else {
            ret.folder = text;
        }
    }
    return ret;
}

ActivityClock activity_clock(Activity const &activity)
{
    auto const    start = static_cast<i64>(activity.segment.start_time.timestamp);
    ActivityClock ret { };
    if (!activity.records.empty()) {
        ret.duration = std::max(0.0, static_cast<double>(static_cast<i64>(activity.records.back().timestamp.timestamp) - start));
    }
    auto offset = activity.utc_offset.value_or(0);
    if (!activity.utc_offset) {
        auto const t = static_cast<std::time_t>(start);
        std::tm    local { };
        localtime_r(&t, &local);
        offset = static_cast<i32>(local.tm_gmtoff);
    }
    auto const local = start + offset;
    ret.start_time_of_day = static_cast<int>(((local % SecondsPerDay) + SecondsPerDay) % SecondsPerDay);
    auto const t = static_cast<std::time_t>(local);
    std::tm    day { };
    gmtime_r(&t, &day);
    ret.date = std::format("{:04}-{:02}-{:02}", day.tm_year + 1900, day.tm_mon + 1, day.tm_mday);
    return ret;
}

std::expected<RenderRange, std::string> parse_range(std::string_view text, ActivityClock const &clock)
{
    auto const usage = std::format("`{}` isn't a range, such as 1:12:00-1:20:30, or @09:44:00-@09:52:30", strip(text));
    auto const dash = text.find('-');
    if (dash == std::string_view::npos) {
        return std::unexpected(usage);
    }
    auto const from = range_point(text.substr(0, dash), clock);
    auto const to = range_point(text.substr(dash + 1), clock);
    if (!from || !to) {
        return std::unexpected(usage);
    }
    if (*to <= *from) {
        return std::unexpected(std::format("`{}` ends before it starts", strip(text)));
    }
    if (*from >= clock.duration || *to <= 0) {
        return std::unexpected(std::format("`{}` isn't in the activity, which is {} long", strip(text), clock_text(clock.duration)));
    }
    return RenderRange { .from = std::max(0.0, *from), .to = std::min(clock.duration, *to) };
}

std::string format_range(RenderRange const &range)
{
    return std::format("{}-{}", clock_text(range.from), clock_text(range.to));
}

size_t frame_count(RenderRange const &range, FrameRate const &rate)
{
    auto const length = std::max(0.0, range.to - range.from);
    return static_cast<size_t>(std::floor(length * static_cast<double>(rate.num) / static_cast<double>(rate.den)));
}

double frame_time(RenderRange const &range, FrameRate const &rate, size_t n)
{
    return range.from + static_cast<double>(static_cast<i64>(n) * rate.den) / static_cast<double>(rate.num);
}

// As ffmpeg's av_timecode_adjust_ntsc_framenum2(): drop frame skips the
// labels ;00 and ;01 (;00 to ;03 at 59.94) at every minute but each tenth.
std::string timecode(double seconds_since_midnight, FrameRate const &rate)
{
    auto const nominal = static_cast<i64>(std::llround(rate.fps()));
    auto       frame = static_cast<i64>(std::llround(seconds_since_midnight * static_cast<double>(rate.num) / static_cast<double>(rate.den)));
    auto const drop_frame = rate.den == 1001 && (nominal == 30 || nominal == 60);
    if (drop_frame) {
        auto const drop = (nominal == 30) ? 2 : 4;
        auto const per_ten_minutes = nominal * 600 - drop * 9;
        auto const per_minute = nominal * 60 - drop;
        auto const tens = frame / per_ten_minutes;
        auto const rest = frame % per_ten_minutes;
        frame += drop * 9 * tens + ((rest > drop) ? drop * ((rest - drop) / per_minute) : 0);
    }
    auto const ff = frame % nominal;
    auto const total_seconds = frame / nominal;
    return std::format("{:02}:{:02}:{:02}{}{:02}", (total_seconds / 3600) % 24, (total_seconds / 60) % 60, total_seconds % 60,
        drop_frame ? ';' : ':', ff);
}

fs::path output_path(fs::path const &folder, ActivityClock const &clock, RenderRange const &range, std::function<bool(fs::path const &)> const &exists)
{
    auto const start = (clock.start_time_of_day + static_cast<int>(range.from)) % SecondsPerDay;
    auto const stem = std::format("{}-{:02}{:02}", clock.date, start / 3600, (start / 60) % 60);
    auto       path = folder / std::format("{}.overlay.mov", stem);
    for (auto n = 2; exists(path); ++n) {
        path = folder / std::format("{}-{}.overlay.mov", stem, n);
    }
    return path;
}

std::optional<fs::path> find_program(std::string_view name, std::string_view path, std::vector<fs::path> const &extra)
{
    std::vector<fs::path> dirs;
    for (auto const &dir : split(path, ':')) {
        if (!dir.empty()) {
            dirs.emplace_back(dir);
        }
    }
    dirs.insert(dirs.end(), extra.begin(), extra.end());
    for (auto const &dir : dirs) {
        std::error_code ec { };
        auto const      program = dir / name;
        if (fs::is_regular_file(program, ec) && ::access(program.c_str(), X_OK) == 0) {
            return program;
        }
    }
    return { };
}

double estimated_gigabytes(Resolution const &resolution, RenderRange const &range)
{
    auto const pixels = static_cast<double>(resolution.width) * static_cast<double>(resolution.height) / (1920.0 * 1080.0);
    return 2.5 * pixels * std::max(0.0, range.to - range.from) / 60.0;
}

void unpremultiply(std::span<unsigned char> rgba)
{
    for (auto ix = 0uz; ix + 3 < rgba.size(); ix += 4) {
        auto const alpha = rgba[ix + 3];
        if (alpha == 0 || alpha == 255) {
            continue;
        }
        for (auto c = ix; c < ix + 3; ++c) {
            rgba[c] = static_cast<unsigned char>(std::min(255, (rgba[c] * 255 + alpha / 2) / alpha));
        }
    }
}

}
