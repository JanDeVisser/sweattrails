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

// Reviewer: the threshold of standing still, 1 s records. In the middle, 10
// records still, which the moving time leaves 10 s out of, are a stop, and
// 9 aren't.
void test_ten_seconds_still_in_the_middle()
{
    auto records = moving(60);
    stand_still(records, 20, 29);
    std::vector<Stop> const expected { { .last_moving = 19, .first_moving = 30 } };
    CHECK(stops(records) == expected);

    auto fewer = moving(60);
    stand_still(fewer, 20, 28);
    CHECK(stops(fewer).empty());
}

// Reviewer: the same 10 records still at the end, 10 s that the moving time
// leaves out, are "10 s or more of records slower than RangeMovingSpeed" as
// they are in the middle, so a stop.
void test_ten_seconds_still_at_the_end()
{
    auto records = moving(70);
    stand_still(records, 60, 69);
    std::vector<Stop> const expected { { .last_moving = 59 } };
    CHECK(stops(records) == expected);
}

// Reviewer: a clock going back, by a minute, is no pause, also not at the
// start or the end.
void test_a_clock_going_back_is_not_a_stop()
{
    auto records = moving(60);
    for (auto ix = 30uz; ix < records.size(); ++ix) {
        records[ix].timestamp.timestamp -= 60;
    }
    CHECK(stops(records).empty());

    auto at_start = moving(60);
    stand_still(at_start, 0, 4);
    at_start[0].timestamp.timestamp += 100;
    CHECK(stops(at_start).empty());
}

// Reviewer: no records, a single one, all of them standing still.
void test_no_stops_in_too_few_records()
{
    CHECK(stops(Records { }).empty());
    CHECK(stops(moving(1)).empty());
    auto still = moving(60);
    stand_still(still, 0, 59);
    CHECK(stops(still).empty());

    TestActivity const empty { Records { } };
    CHECK(!empty.to(0, Boundary::Km, Direction::On).has_value());
    CHECK(!empty.to(0, Boundary::Stop, Direction::Back).has_value());
}

// Reviewer: a stop at the start and the end are where Back and On go to.
void test_stop_on_and_back_to_the_ends()
{
    auto records = moving(80);
    stand_still(records, 0, 14);
    stand_still(records, 60, 79);
    TestActivity const t { records };
    CHECK(t.to(30, Boundary::Stop, Direction::Back) == std::optional<size_t> { 15 });
    CHECK(t.to(15, Boundary::Stop, Direction::Back) == std::optional<size_t> { });
    CHECK(t.to(30, Boundary::Stop, Direction::On) == std::optional<size_t> { 59 });
    CHECK(t.to(59, Boundary::Stop, Direction::On) == std::optional<size_t> { });
    // From inside the stop at the end: back to the start's.
    CHECK(t.to(70, Boundary::Stop, Direction::Back) == std::optional<size_t> { 15 });
}

// Reviewer: from a record inside a stop, standing at a traffic light: on to
// the next stop, back to the end of the one before.
void test_stop_from_inside_a_stop()
{
    auto records = moving(300);
    stand_still(records, 100, 119);
    stand_still(records, 200, 219);
    TestActivity const t { records };
    CHECK(t.to(110, Boundary::Stop, Direction::On) == std::optional<size_t> { 199 });
    CHECK(t.to(110, Boundary::Stop, Direction::Back) == std::optional<size_t> { });
    CHECK(t.to(210, Boundary::Stop, Direction::Back) == std::optional<size_t> { 120 });
}

// Reviewer: records that start without a distance, and a first distance
// past the first km, as a recording started late can have.
void test_km_with_distances_from_partway()
{
    auto records = moving(600);
    for (auto ix = 0uz; ix < 50; ++ix) {
        records[ix].distance.reset();
    }
    TestActivity const t { records };
    CHECK(t.to(10, Boundary::Km, Direction::On) == std::optional<size_t> { 250 });
    CHECK(t.to(10, Boundary::Km, Direction::Back) == std::optional<size_t> { 0 });
    CHECK(t.to(260, Boundary::Km, Direction::Back) == std::optional<size_t> { 250 });

    auto late = moving(600);
    for (auto &rec : late) {
        *rec.distance += 1500;
    }
    // At 1500 m to 3896 m: whole km at records 125 (2000 m) and 375.
    TestActivity const l { late };
    CHECK(l.to(0, Boundary::Km, Direction::On) == std::optional<size_t> { 125 });
    CHECK(l.to(100, Boundary::Km, Direction::Back) == std::optional<size_t> { 0 });
    CHECK(l.to(125, Boundary::Km, Direction::Back) == std::optional<size_t> { 0 });
    CHECK(l.to(200, Boundary::Km, Direction::Back) == std::optional<size_t> { 125 });
}

// Reviewer: an empty lap, which a watch can log, is no boundary, and
// records before the first lap and after the last are in none.
void test_laps_empty_and_not_covering_the_records()
{
    TestActivity const empty { moving(300), { { 0, 100 }, { 100, 100 }, { 100, 300 } } };
    CHECK(empty.to(50, Boundary::Lap, Direction::On) == std::optional<size_t> { 99 });
    CHECK(empty.to(99, Boundary::Lap, Direction::On) == std::optional<size_t> { 299 });
    CHECK(empty.to(150, Boundary::Lap, Direction::Back) == std::optional<size_t> { 100 });
    CHECK(empty.to(100, Boundary::Lap, Direction::Back) == std::optional<size_t> { 0 });

    TestActivity const partial { moving(300), { { 20, 100 }, { 100, 250 } } };
    CHECK(partial.to(5, Boundary::Lap, Direction::On) == std::optional<size_t> { 99 });
    CHECK(partial.to(260, Boundary::Lap, Direction::On) == std::optional<size_t> { });
    CHECK(partial.to(260, Boundary::Lap, Direction::Back) == std::optional<size_t> { 100 });
    CHECK(partial.to(20, Boundary::Lap, Direction::Back) == std::optional<size_t> { });
}

// Reviewer: smart recording, records a few seconds apart. One record
// still, 6 s after the one before, and the next moving 5 s after it: no
// pause of more than 10 s, and 6 s standing still, which the moving time
// leaves out. A slowdown, not a stop, though the two moving records are
// 11 s apart.
void test_a_slowdown_between_sparse_records_is_not_a_stop()
{
    auto records = moving(40);
    pause_before(records, 20, 5);
    pause_before(records, 21, 4);
    stand_still(records, 20, 20);
    CHECK(stops(records).empty());
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
    test_ten_seconds_still_in_the_middle();
    test_ten_seconds_still_at_the_end();
    test_a_clock_going_back_is_not_a_stop();
    test_no_stops_in_too_few_records();
    test_stop_on_and_back_to_the_ends();
    test_stop_from_inside_a_stop();
    test_km_with_distances_from_partway();
    test_laps_empty_and_not_covering_the_records();
    test_a_slowdown_between_sparse_records_is_not_a_stop();
    return Test::test_result();
}
