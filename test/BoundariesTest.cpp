/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests where a selection extends to: the stops in an activity's records,
 * and the next km, lap boundary or stop back from a record or on from it.
 */

#include <optional>
#include <vector>

#include <storage/Activity.h>
#include <storage/Boundaries.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

using Records = std::vector<record>;

// `count` records a second apart, moving at 4 m/s, 4 m further each.
Records moving(size_t count)
{
    Records ret(count);
    for (auto ix = 0uz; ix < count; ++ix) {
        ret[ix].timestamp.timestamp = 1000 + static_cast<u32>(ix);
        ret[ix].speed = 4.0f;
        ret[ix].distance = 4.0f * static_cast<float>(ix);
    }
    return ret;
}

// Records `first` to `last` stand still, at the same distance.
void stand_still(Records &records, size_t first, size_t last)
{
    for (auto ix = first; ix <= last; ++ix) {
        records[ix].speed = 0.0f;
        records[ix].distance = records[first - (first > 0 ? 1 : 0)].distance;
    }
}

// Records from `from` on are `seconds` later: a pause that recorded
// nothing before `from`.
void pause_before(Records &records, size_t from, u32 seconds)
{
    for (auto ix = from; ix < records.size(); ++ix) {
        records[ix].timestamp.timestamp += seconds;
    }
}

struct TestActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    explicit TestActivity(Records records, std::vector<std::pair<size_t, size_t>> const &laps = { })
    {
        activity.records = records;
        activity.recorded = std::move(records);
        for (auto const &[first, end] : laps) {
            activity.laps.push_back(Lap { .first_record = first, .end_record = end });
        }
    }

    [[nodiscard]] std::optional<size_t> to(size_t from, Boundary boundary, Direction direction) const
    {
        return extend_to(activity, from, boundary, direction);
    }
};

void test_a_pause_is_a_stop()
{
    auto records = moving(40);
    pause_before(records, 20, 30);
    std::vector<Stop> const expected { { .last_moving = 19, .first_moving = 20 } };
    CHECK(stops(records) == expected);
}

// The moving time's gap: a pause of RangeMovingGap seconds or less isn't
// a stop, as the moving time counts it.
void test_a_short_pause_is_not_a_stop()
{
    auto records = moving(40);
    pause_before(records, 20, RangeMovingGap - 1);
    CHECK(stops(records).empty());
}

void test_standing_still_is_a_stop()
{
    auto records = moving(60);
    stand_still(records, 20, 34);
    std::vector<Stop> const expected { { .last_moving = 19, .first_moving = 35 } };
    CHECK(stops(records) == expected);
}

// A few seconds below RangeMovingSpeed, a sharp turn say, isn't.
void test_a_slowdown_is_not_a_stop()
{
    auto records = moving(60);
    stand_still(records, 20, 25);
    CHECK(stops(records).empty());
}

void test_without_speeds_only_pauses_stop()
{
    auto records = moving(60);
    for (auto &rec : records) {
        rec.speed.reset();
    }
    pause_before(records, 30, 60);
    std::vector<Stop> const expected { { .last_moving = 29, .first_moving = 30 } };
    CHECK(stops(records) == expected);
}

void test_stops_at_the_start_and_end()
{
    auto records = moving(80);
    stand_still(records, 0, 14);
    stand_still(records, 60, 79);
    std::vector<Stop> const expected { { .first_moving = 15 }, { .last_moving = 59 } };
    CHECK(stops(records) == expected);
}

// 600 records at 4 m: whole km at records 250 and 500.
void test_km_on()
{
    TestActivity const t { moving(600) };
    CHECK(t.to(100, Boundary::Km, Direction::On) == std::optional<size_t> { 250 });
    // Already on the km: the next one.
    CHECK(t.to(250, Boundary::Km, Direction::On) == std::optional<size_t> { 500 });
    CHECK(t.to(520, Boundary::Km, Direction::On) == std::optional<size_t> { });
}

void test_km_back()
{
    TestActivity const t { moving(600) };
    CHECK(t.to(300, Boundary::Km, Direction::Back) == std::optional<size_t> { 250 });
    CHECK(t.to(250, Boundary::Km, Direction::Back) == std::optional<size_t> { 0 });
    // In the first km: its start, the first record.
    CHECK(t.to(100, Boundary::Km, Direction::Back) == std::optional<size_t> { 0 });
    CHECK(t.to(0, Boundary::Km, Direction::Back) == std::optional<size_t> { });
}

void test_no_km_without_distances()
{
    auto records = moving(600);
    for (auto &rec : records) {
        rec.distance.reset();
    }
    TestActivity const t { records };
    CHECK(t.to(100, Boundary::Km, Direction::On) == std::optional<size_t> { });
    CHECK(t.to(100, Boundary::Km, Direction::Back) == std::optional<size_t> { });
}

void test_lap_on()
{
    TestActivity const t { moving(300), { { 0, 100 }, { 100, 200 }, { 200, 300 } } };
    CHECK(t.to(50, Boundary::Lap, Direction::On) == std::optional<size_t> { 99 });
    // At the end of a lap: the end of the next.
    CHECK(t.to(99, Boundary::Lap, Direction::On) == std::optional<size_t> { 199 });
    CHECK(t.to(299, Boundary::Lap, Direction::On) == std::optional<size_t> { });
}

void test_lap_back()
{
    TestActivity const t { moving(300), { { 0, 100 }, { 100, 200 }, { 200, 300 } } };
    CHECK(t.to(150, Boundary::Lap, Direction::Back) == std::optional<size_t> { 100 });
    CHECK(t.to(100, Boundary::Lap, Direction::Back) == std::optional<size_t> { 0 });
    CHECK(t.to(0, Boundary::Lap, Direction::Back) == std::optional<size_t> { });
}

void test_no_laps_to_go_to_with_one_lap()
{
    TestActivity const t { moving(300), { { 0, 300 } } };
    CHECK(t.to(150, Boundary::Lap, Direction::On) == std::optional<size_t> { });
    CHECK(t.to(150, Boundary::Lap, Direction::Back) == std::optional<size_t> { });
}

// Two stops: standing still at 100–119, and a pause before 200.
void test_stop_on_and_back()
{
    auto records = moving(300);
    stand_still(records, 100, 119);
    pause_before(records, 200, 60);
    TestActivity const t { records };
    CHECK(t.to(50, Boundary::Stop, Direction::On) == std::optional<size_t> { 99 });
    CHECK(t.to(99, Boundary::Stop, Direction::On) == std::optional<size_t> { 199 });
    CHECK(t.to(250, Boundary::Stop, Direction::On) == std::optional<size_t> { });
    CHECK(t.to(250, Boundary::Stop, Direction::Back) == std::optional<size_t> { 200 });
    CHECK(t.to(200, Boundary::Stop, Direction::Back) == std::optional<size_t> { 120 });
    CHECK(t.to(50, Boundary::Stop, Direction::Back) == std::optional<size_t> { });
}

// The analysis smooths the speeds in `records`: the stops come from the
// speeds as recorded.
void test_stops_from_the_recorded_speeds()
{
    auto records = moving(300);
    stand_still(records, 100, 119);
    TestActivity t { records };
    for (auto &rec : t.activity.records) {
        rec.speed = 4.0f;
    }
    CHECK(t.to(50, Boundary::Stop, Direction::On) == std::optional<size_t> { 99 });
}

void test_from_past_the_records()
{
    TestActivity const t { moving(10) };
    CHECK(t.to(10, Boundary::Stop, Direction::Back) == std::optional<size_t> { });
}

}

int main()
{
    test_a_pause_is_a_stop();
    test_a_short_pause_is_not_a_stop();
    test_standing_still_is_a_stop();
    test_a_slowdown_is_not_a_stop();
    test_without_speeds_only_pauses_stop();
    test_stops_at_the_start_and_end();
    test_km_on();
    test_km_back();
    test_no_km_without_distances();
    test_lap_on();
    test_lap_back();
    test_no_laps_to_go_to_with_one_lap();
    test_stop_on_and_back();
    test_stops_from_the_recorded_speeds();
    test_from_past_the_records();
    return Test::test_result();
}
