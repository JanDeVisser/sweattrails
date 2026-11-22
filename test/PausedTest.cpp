/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the track a Wahoo logs while it's paused: reading it from the FIT
 * file, and which of it the map draws. See specs/64-wahoo-paused-track.md.
 */

#include <cmath>
#include <optional>
#include <vector>

#include <fit/FITPatch.h>
#include <storage/Activity.h>
#include <storage/Gaps.h>
#include <storage/Paused.h>
#include <storage/Storage.h>

#include <TestApp.h>
#include <TestFit.h>
#include <TestRun.h>

using namespace ST;

namespace {

constexpr u16 Wahoo = 32;

struct Loaded {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };

    explicit Loaded(std::string_view bytes, LoadingDepth depth = LoadingDepth::Deep)
    {
        activity.depth = depth;
        MUST(activity.load_from_slice(bytes));
    }
};

std::string wahoo_run()
{
    return Test::run({ .manufacturer = Wahoo, .paused_track = true });
}

// A record `seconds` after the start, `north` metres north of home, or
// without a position.
record at(u32 seconds, std::optional<double> north)
{
    return record {
        .timestamp = DateTime::from_timestamp(static_cast<i32>(static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET + seconds)),
        .position = north.transform([](double n) {
            return Coordinates { .lat = static_cast<float32>(Test::HomeLat + n / Test::MetresPerDegree), .lon = static_cast<float32>(Test::HomeLon) };
        }),
    };
}

// A record a second from `from` up to `to`, 3 m a second north.
std::vector<record> every_second(u32 from, u32 to)
{
    std::vector<record> ret;
    for (auto seconds = from; seconds < to; ++seconds) {
        ret.push_back(at(seconds, 3.0 * seconds));
    }
    return ret;
}

std::vector<record> joined(std::vector<record> a, std::vector<record> const &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}

// The ELEMNT's paused track is read with the record's fields, and the one
// it writes after the end has no position. The records are as they were.
void test_load_paused_track()
{
    Loaded      wahoo { wahoo_run() };
    auto const &paused = wahoo.activity.paused;
    CHECK_EQ(paused.size(), 301uz);
    if (paused.size() != 301) {
        return;
    }
    CHECK_EQ(paused[0].timestamp.timestamp, at(100, { }).timestamp.timestamp);
    CHECK(paused[0].position.has_value());
    CHECK(paused[0].position && std::abs(paused[0].position->lat - at(100, 300).position->lat) < 1e-5f);
    CHECK(paused[0].altitude && std::abs(*paused[0].altitude - 10) < 0.01f);
    CHECK(paused[0].speed && std::abs(*paused[0].speed - 3) < 0.01f);
    CHECK(paused[0].distance && std::abs(*paused[0].distance - 297) < 0.01f);
    CHECK(!paused[300].position.has_value());
    CHECK_EQ(wahoo.activity.records.size(), Loaded { Test::run() }.activity.records.size());
}

// Message 0xFF00 may be anything in another maker's file.
void test_paused_track_is_wahoos_only()
{
    Loaded garmin { Test::run({ .manufacturer = 1, .paused_track = true }) };
    CHECK(garmin.activity.paused.empty());
    CHECK_EQ(garmin.activity.records.size(), 200uz);
}

// The map is only drawn for an activity loaded deep.
void test_no_paused_track_on_a_shallow_load()
{
    Loaded wahoo { wahoo_run(), LoadingDepth::Shallow };
    CHECK(wahoo.activity.paused.empty());
}

// The gap from 99 s to 400 s, with its 300 positions; the one after the
// end isn't drawn.
void test_paused_tracks()
{
    Loaded     wahoo { wahoo_run() };
    auto const tracks = paused_tracks(wahoo.activity.records, wahoo.activity.paused);
    CHECK_EQ(tracks.size(), 1uz);
    if (tracks.size() != 1) {
        return;
    }
    CHECK_EQ(tracks[0].before, 99uz);
    CHECK_EQ(tracks[0].positions.size(), 300uz);
}

void test_two_pauses()
{
    auto const          records = joined(joined(every_second(0, 10), every_second(50, 60)), every_second(100, 110));
    std::vector<record> paused;
    for (auto seconds : { 20u, 30u, 70u, 80u, 90u }) {
        paused.push_back(at(seconds, 3.0 * seconds));
    }
    auto const tracks = paused_tracks(records, paused);
    CHECK_EQ(tracks.size(), 2uz);
    if (tracks.size() != 2) {
        return;
    }
    CHECK_EQ(tracks[0].before, 9uz);
    CHECK_EQ(tracks[0].positions.size(), 2uz);
    CHECK_EQ(tracks[1].before, 19uz);
    CHECK_EQ(tracks[1].positions.size(), 3uz);
}

// Only what's strictly between the records around the pause: not at
// their times, not before the first record or after the last, and not
// without a fix.
void test_paused_tracks_leave_out()
{
    auto const          records = joined(every_second(10, 20), every_second(60, 70));
    std::vector<record> paused {
        at(5, 15),
        at(19, 57),
        at(30, { }),
        record { .timestamp = at(40, { }).timestamp, .position = Coordinates { } }, // No fix
        at(50, 150),
        at(60, 180),
        at(80, 240),
    };
    auto const tracks = paused_tracks(records, paused);
    CHECK_EQ(tracks.size(), 1uz);
    if (tracks.size() != 1) {
        return;
    }
    CHECK_EQ(tracks[0].positions.size(), 1uz);
    CHECK(tracks[0].positions[0] == *at(50, 150).position);
}

// Records 10 s apart count as moving, as a range's stats have them: a
// stop at a light that short is drawn solid.
void test_short_pause_isnt_one()
{
    auto const records = joined(every_second(0, 10), every_second(19, 30));
    auto const tracks = paused_tracks(records, std::vector { at(12, 36), at(15, 45) });
    CHECK(tracks.empty());
}

void test_pause_without_a_paused_track()
{
    auto const records = joined(every_second(0, 10), every_second(100, 110));
    CHECK(paused_tracks(records, std::vector<record> { }).empty());
}

// A gap filled from a friend's file (specs/30-fill-gaps.md) has records
// again, so its paused track isn't drawn any more.
void test_filled_gap_has_no_paused_track()
{
    auto const bytes = wahoo_run();
    Loaded     watched { bytes };
    auto const gaps = find_gaps(watched.activity.recorded);
    CHECK_EQ(gaps.size(), 1uz);
    std::vector<record> friend_run;
    for (auto seconds = 0u; seconds < 500; ++seconds) {
        auto r = at(seconds, 3.0 * seconds);
        r.distance = static_cast<float32>(3.0 * seconds);
        friend_run.push_back(r);
    }
    auto const fills = plan_fills(watched.activity.recorded, gaps, friend_run);
    CHECK_EQ(fills.size(), 1uz);
    auto const patched = FIT::patch_fit(bytes, fills);
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    Loaded filled { *patched };
    CHECK_EQ(filled.activity.records.size(), 500uz);
    CHECK_EQ(filled.activity.paused.size(), 301uz);
    CHECK(paused_tracks(filled.activity.records, filled.activity.paused).empty());
}

int main()
{
    test_load_paused_track();
    test_paused_track_is_wahoos_only();
    test_no_paused_track_on_a_shallow_load();
    test_paused_tracks();
    test_two_pauses();
    test_paused_tracks_leave_out();
    test_short_pause_isnt_one();
    test_pause_without_a_paused_track();
    test_filled_gap_has_no_paused_track();
    return ST::Test::test_result();
}
