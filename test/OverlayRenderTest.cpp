/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Rendering an overlay to video, apart from drawing and ffmpeg: settings,
 * ranges, frames, timecodes, file names, finding ffmpeg, and alpha.
 */

#include <array>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <string>

#include <IO.h>
#include <JSON.h>

#include <overlay/Render.h>
#include <storage/Storage.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
namespace fs = std::filesystem;

namespace {

std::string settings_of(std::string_view json)
{
    auto const parsed = MUST_EVAL(JSONValue::deserialize(json));
    auto const settings = decode_render_settings(parsed, "/home/me");
    if (!settings) {
        return settings.error();
    }
    return std::format("{}x{} {} {}", settings->resolution.width, settings->resolution.height, settings->fps.to_string(), settings->folder.string());
}

void test_settings()
{
    auto const defaults = decode_render_settings({ }, "/home/me");
    CHECK(defaults.has_value());
    if (defaults) {
        CHECK(defaults->resolution == (Resolution { 1920, 1080 }));
        CHECK(defaults->fps == (FrameRate { 30000, 1001 }));
        CHECK(defaults->folder == fs::path { "/home/me/Movies" });
    }
    CHECK_EQ(settings_of("{}"), "1920x1080 30000/1001 /home/me/Movies");
    CHECK_EQ(settings_of(R"({ "resolution": "4k-portrait", "fps": "29.97", "folder": "~/Videos" })"), "2160x3840 30000/1001 /home/me/Videos");
    CHECK_EQ(settings_of(R"({ "resolution": "720p", "fps": "59.94", "folder": "/Volumes/Footage" })"), "1280x720 60000/1001 /Volumes/Footage");
    CHECK_EQ(settings_of(R"({ "resolution": { "width": 1080, "height": 1350 }, "fps": 25 })"), "1080x1350 25 /home/me/Movies");
    CHECK_EQ(settings_of(R"({ "fps": "24000/1001" })"), "1920x1080 24000/1001 /home/me/Movies");
    CHECK_EQ(settings_of(R"({ "fps": "23.976" })"), "1920x1080 24000/1001 /home/me/Movies");
    CHECK_EQ(settings_of(R"({ "fps": "50" })"), "1920x1080 50 /home/me/Movies");
    CHECK_EQ(settings_of(R"({ "folder": "~" })"), "1920x1080 30000/1001 /home/me/");
}

void test_settings_errors()
{
    CHECK(settings_of(R"({ "resolution": "8k" })").starts_with("\"resolution\" isn't"));
    CHECK(settings_of(R"({ "resolution": { "width": 1080 } })").starts_with("\"resolution\" isn't"));
    CHECK_EQ(settings_of(R"({ "resolution": { "width": 1081, "height": 1350 } })"), "\"resolution\": the width and the height have to be even");
    CHECK(settings_of(R"({ "fps": "fast" })").starts_with("\"fps\" isn't"));
    CHECK(settings_of(R"({ "fps": 0 })").starts_with("\"fps\" isn't"));
    CHECK(settings_of(R"({ "fps": "30000/0" })").starts_with("\"fps\" isn't"));
    CHECK_EQ(settings_of(R"({ "folder": 3 })"), "\"folder\" isn't a folder's path");
    CHECK_EQ(settings_of("[]"), "\"overlay\" isn't an object");
}

// Started at 8:32 local time, two hours long.
ActivityClock const Clock { .duration = 7200, .start_time_of_day = 8 * 3600 + 32 * 60, .date = "2025-06-14" };

std::string range_of(std::string_view text, ActivityClock const &clock = Clock)
{
    auto const range = parse_range(text, clock);
    return range ? std::format("{} {}", range->from, range->to) : range.error();
}

void test_ranges()
{
    CHECK_EQ(range_of("1:12:00-1:20:30"), "4320 4830");
    CHECK_EQ(range_of(" 1:12:00 - 1:20:30 "), "4320 4830");
    CHECK_EQ(range_of("12:00-20:30"), "720 1230");
    // Times of day, as the camera has them.
    CHECK_EQ(range_of("@09:44:00-@09:52:30"), "4320 4830");
    CHECK_EQ(range_of("@9:44-@9:52"), "4320 4800");
    // Either kind on either side.
    CHECK_EQ(range_of("1:12:00-@09:52:30"), "4320 4830");
    // Cut to the activity.
    CHECK_EQ(range_of("1:50:00-2:30:00"), "6600 7200");
    CHECK_EQ(range_of("@08:00:00-@08:40:00"), "0 480");
    // Through midnight.
    ActivityClock const late { .duration = 7200, .start_time_of_day = 23 * 3600 + 30 * 60, .date = "2025-06-14" };
    CHECK_EQ(range_of("@00:10:00-@00:20:00", late), "2400 3000");
}

void test_range_errors()
{
    CHECK_EQ(range_of("soon"), "`soon` isn't a range, such as 1:12:00-1:20:30, or @09:44:00-@09:52:30");
    CHECK_EQ(range_of("1:12:00"), "`1:12:00` isn't a range, such as 1:12:00-1:20:30, or @09:44:00-@09:52:30");
    CHECK(range_of("1:2:00-1:20:00").contains("isn't a range"));
    CHECK(range_of("1:12:60-1:20:00").contains("isn't a range"));
    CHECK(range_of("@25:00:00-@25:10:00").contains("isn't a range"));
    CHECK_EQ(range_of("1:20:30-1:12:00"), "`1:20:30-1:12:00` ends before it starts");
    CHECK_EQ(range_of("3:00:00-3:10:00"), "`3:00:00-3:10:00` isn't in the activity, which is 2:00:00 long");
}

void test_format_range()
{
    CHECK_EQ(format_range(RenderRange { .from = 4320, .to = 4830.4 }), "1:12:00-1:20:30");
    CHECK_EQ(format_range(RenderRange { .from = 0, .to = 59 }), "0:00:00-0:00:59");
    // What it writes, it reads.
    CHECK_EQ(range_of(format_range(RenderRange { .from = 300, .to = 900 })), "300 900");
}

void test_activity_clock()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    // 2025-06-15 15:06:40 UTC, which is 11:06:40 in Toronto in the summer.
    activity.segment.start_time.timestamp = 1'750'000'000;
    activity.utc_offset = -4 * 3600;
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = 1'750'000'000 } });
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = 1'750'003'910 } });
    auto const clock = activity_clock(activity);
    CHECK_EQ(clock.duration, 3910.0);
    CHECK_EQ(clock.start_time_of_day, 11 * 3600 + 6 * 60 + 40);
    CHECK_EQ(clock.date, "2025-06-15");
    // Late in the evening, a day earlier where it was.
    activity.utc_offset = -16 * 3600;
    CHECK_EQ(activity_clock(activity).date, "2025-06-14");
    CHECK_EQ(activity_clock(activity).start_time_of_day, 23 * 3600 + 6 * 60 + 40);
}

void test_records_range()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    CHECK(records_range(activity, 0, 10) == (RenderRange { }));
    activity.segment.start_time.timestamp = 1'750'000'000;
    for (u32 t : { 0u, 1u, 2u, 10u, 11u }) {
        activity.records.push_back(record { .timestamp = DateTime { .timestamp = 1'750'000'000 + t } });
    }
    CHECK(records_range(activity, 1, 3) == (RenderRange { .from = 1, .to = 10 }));
    CHECK(records_range(activity, 0, 4) == (RenderRange { .from = 0, .to = 11 }));
    // Past the end is the end.
    CHECK(records_range(activity, 2, 99) == (RenderRange { .from = 2, .to = 11 }));
}

void test_initial_render_range()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = 1'750'000'000;
    for (u32 t : { 0u, 1u, 2u, 10u, 11u }) {
        activity.records.push_back(record { .timestamp = DateTime { .timestamp = 1'750'000'000 + t } });
    }
    CHECK(initial_render_range(activity, std::pair { 1uz, 3uz }) == (RenderRange { .from = 1, .to = 10 }));
    // One record, or none: the whole activity.
    CHECK(initial_render_range(activity, std::pair { 2uz, 2uz }) == (RenderRange { .from = 0, .to = 11 }));
    CHECK(initial_render_range(activity, std::nullopt) == (RenderRange { .from = 0, .to = 11 }));
}

void test_ffmpeg_arguments()
{
    RenderSettings const settings { .resolution = { 1920, 1080 }, .fps = { 30000, 1001 }, .folder = "/m" };
    auto const           args = ffmpeg_arguments(settings, Clock, RenderRange { .from = 4320, .to = 4830 }, "/m/out.mov");
    std::string          line;
    for (auto const &arg : args) {
        line += (line.empty() ? "" : " ") + arg;
    }
    CHECK_EQ(line,
        "-hide_banner -loglevel error -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30000/1001 -i - -vf vflip "
        "-c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le -timecode 09:44:00;02 -n /m/out.mov");
}

void test_frames()
{
    RenderRange const range { .from = 0, .to = 60 };
    FrameRate const   ntsc { 30000, 1001 };
    CHECK_EQ(frame_count(range, ntsc), 1798uz);
    CHECK_EQ(frame_count(range, FrameRate { 25, 1 }), 1500uz);
    CHECK_EQ(frame_time(range, ntsc, 0), 0.0);
    CHECK_EQ(frame_time(range, FrameRate { 25, 1 }, 25), 1.0);
    CHECK_EQ(frame_time(RenderRange { .from = 4320, .to = 4830 }, FrameRate { 25, 1 }, 50), 4322.0);
    // Three hours in, exactly where it should be, not adding up 1001/30000s.
    auto const n = 323'676uz;
    CHECK_EQ(frame_time(range, ntsc, n), static_cast<double>(n * 1001) / 30000.0);
    CHECK_EQ(frame_count(RenderRange { .from = 10, .to = 5 }, ntsc), 0uz);
}

// The time of frame `frame` since midnight.
double at_frame(i64 frame, FrameRate const &rate)
{
    return static_cast<double>(frame * rate.den) / static_cast<double>(rate.num);
}

void test_timecodes()
{
    CHECK_EQ(timecode(3600.5, FrameRate { 30, 1 }), "01:00:00:15");
    CHECK_EQ(timecode(35040, FrameRate { 25, 1 }), "09:44:00:00");
    CHECK_EQ(timecode(35040.08, FrameRate { 25, 1 }), "09:44:00:02");
    // Drop frame: ;00 and ;01 don't exist at minutes that aren't tens.
    FrameRate const ntsc { 30000, 1001 };
    CHECK_EQ(timecode(at_frame(1799, ntsc), ntsc), "00:00:59;29");
    CHECK_EQ(timecode(at_frame(1800, ntsc), ntsc), "00:01:00;02");
    CHECK_EQ(timecode(at_frame(17981, ntsc), ntsc), "00:09:59;29");
    CHECK_EQ(timecode(at_frame(17982, ntsc), ntsc), "00:10:00;00");
    CHECK_EQ(timecode(at_frame(17982 + 1800, ntsc), ntsc), "00:11:00;02");
    // It keeps up with the clock.
    CHECK(timecode(35040, ntsc).starts_with("09:44:00;"));
    CHECK(timecode(23 * 3600 + 59 * 60, ntsc).starts_with("23:59:00;"));
    FrameRate const ntsc60 { 60000, 1001 };
    CHECK_EQ(timecode(at_frame(3599, ntsc60), ntsc60), "00:00:59;59");
    CHECK_EQ(timecode(at_frame(3600, ntsc60), ntsc60), "00:01:00;04");
    // 23.976 has no drop frame, and falls behind the clock.
    CHECK_EQ(timecode(3600, FrameRate { 24000, 1001 }), "00:59:56:10");
}

// Ranges around midnight, as a time of day. Reviewer's tests.
void test_ranges_at_midnight()
{
    // Started at 23:50, so 00:05 is the next day.
    ActivityClock const late { .duration = 7200, .start_time_of_day = 23 * 3600 + 50 * 60, .date = "2025-06-14" };
    CHECK_EQ(range_of("@23:55-@00:05", late), "300 900");
    // A little before the start, on the same day, is cut to the start.
    CHECK_EQ(range_of("@23:40-@23:55", late), "0 300");
    // Started at 00:10: 23:55 is a little before the start, the day
    // before, and is cut to the start as well.
    ActivityClock const early { .duration = 7200, .start_time_of_day = 10 * 60, .date = "2025-06-15" };
    CHECK_EQ(range_of("@23:55-@00:20", early), "0 600");
    CHECK_EQ(range_of("@00:15-@00:20", early), "300 600");
}

// The time zone the system has, where an activity has no utc_offset.
void test_activity_clock_without_offset()
{
    auto const *old = std::getenv("TZ");
    std::string saved { (old != nullptr) ? old : "" };
    ::setenv("TZ", "America/Toronto", 1);
    ::tzset();
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = 1'750'000'000; // 2025-06-15 15:06:40 UTC
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = 1'750'000'000 } });
    auto const clock = activity_clock(activity);
    CHECK_EQ(clock.start_time_of_day, 11 * 3600 + 6 * 60 + 40);
    CHECK_EQ(clock.date, "2025-06-15");
    if (old != nullptr) {
        ::setenv("TZ", saved.c_str(), 1);
    } else {
        ::unsetenv("TZ");
    }
    ::tzset();
}

// Every drop-frame label for two hours, counted label by label: ;00 and
// ;01 (;00 to ;03 at 59.94) are skipped at each minute but every tenth.
// Reviewer's test.
void test_every_drop_frame_label(FrameRate const &rate, i64 nominal, i64 drop)
{
    i64  h { 0 }, m { 0 }, s { 0 }, f { 0 };
    auto mismatches { 0 };
    for (i64 frame = 0; frame < 2 * 3600 * nominal; ++frame) {
        auto const expected = std::format("{:02}:{:02}:{:02};{:02}", h, m, s, f);
        if (auto const label = timecode(at_frame(frame, rate), rate); label != expected && ++mismatches <= 3) {
            CHECK_EQ(label, expected);
        }
        if (++f == nominal) {
            f = 0;
            if (++s == 60) {
                s = 0;
                if (++m == 60) {
                    m = 0;
                    ++h;
                }
                if (m % 10 != 0) {
                    f = drop;
                }
            }
        }
    }
    CHECK_EQ(mismatches, 0);
}

void test_timecodes_at_midnight()
{
    // The last frame of the day, and the timecode wrapping round.
    CHECK_EQ(timecode(24 * 3600 - 1.0 / 25, FrameRate { 25, 1 }), "23:59:59:24");
    CHECK_EQ(timecode(24 * 3600, FrameRate { 25, 1 }), "00:00:00:00");
    CHECK_EQ(timecode(0, FrameRate { 30000, 1001 }), "00:00:00;00");
}

void test_output_path()
{
    ActivityClock const clock { .duration = 7200, .start_time_of_day = 11 * 3600 + 6 * 60 + 40, .date = "2025-06-15" };
    RenderRange const   range { .from = 120, .to = 300 };
    auto const          none = [](fs::path const &) { return false; };
    CHECK(output_path("/m", clock, range, none) == fs::path { "/m/2025-06-15-1108.overlay.mov" });
    auto const taken = [](fs::path const &p) { return p.filename() != "2025-06-15-1108-3.overlay.mov"; };
    CHECK(output_path("/m", clock, range, taken) == fs::path { "/m/2025-06-15-1108-3.overlay.mov" });
}

void test_find_program()
{
    Test::TempDir first { "programs-first" };
    Test::TempDir second { "programs-second" };
    Test::TempDir extra { "programs-extra" };
    MUST(write_file_by_name((first.path / "ffmpeg").string(), std::string_view { "not runnable" }));
    MUST(write_file_by_name((second.path / "ffmpeg").string(), std::string_view { "#!/bin/sh\n" }));
    fs::permissions(second.path / "ffmpeg", fs::perms::owner_all);
    MUST(write_file_by_name((extra.path / "ffprobe").string(), std::string_view { "#!/bin/sh\n" }));
    fs::permissions(extra.path / "ffprobe", fs::perms::owner_all);
    auto const path = std::format("{}::{}", first.path.string(), second.path.string());
    CHECK(find_program("ffmpeg", path, { }) == second.path / "ffmpeg");
    // Not on the PATH, as for an app opened from the Finder.
    CHECK(find_program("ffprobe", path, { extra.path }) == extra.path / "ffprobe");
    CHECK(!find_program("ffprobe", path, { }).has_value());
    CHECK(!find_program("ffmpeg", "", { first.path }).has_value());
}

void test_estimated_size()
{
    CHECK_EQ(estimated_gigabytes(Resolution { 1920, 1080 }, RenderRange { .from = 0, .to = 600 }), 25.0);
    CHECK_EQ(estimated_gigabytes(Resolution { 3840, 2160 }, RenderRange { .from = 0, .to = 60 }), 10.0);
}

void test_unpremultiply()
{
    std::array<unsigned char, 16> pixels {
        64, 32, 0, 128,    // half transparent
        10, 20, 30, 0,     // transparent: left alone
        1, 2, 3, 255,      // opaque
        200, 200, 200, 100 // brighter than it can be: kept at 255
    };
    unpremultiply(pixels);
    CHECK_EQ(std::format("{} {} {} {}", pixels[0], pixels[1], pixels[2], pixels[3]), "128 64 0 128");
    CHECK_EQ(std::format("{} {} {} {}", pixels[4], pixels[5], pixels[6], pixels[7]), "10 20 30 0");
    CHECK_EQ(std::format("{} {} {} {}", pixels[8], pixels[9], pixels[10], pixels[11]), "1 2 3 255");
    CHECK_EQ(std::format("{} {} {} {}", pixels[12], pixels[13], pixels[14], pixels[15]), "255 255 255 100");
}

}

int main()
{
    test_settings();
    test_settings_errors();
    test_ranges();
    test_range_errors();
    test_format_range();
    test_activity_clock();
    test_records_range();
    test_initial_render_range();
    test_ffmpeg_arguments();
    test_frames();
    test_timecodes();
    test_ranges_at_midnight();
    test_activity_clock_without_offset();
    test_every_drop_frame_label(FrameRate { 30000, 1001 }, 30, 2);
    test_every_drop_frame_label(FrameRate { 60000, 1001 }, 60, 4);
    test_timecodes_at_midnight();
    test_output_path();
    test_find_program();
    test_estimated_size();
    test_unpremultiply();
    return ST::Test::test_result();
}
