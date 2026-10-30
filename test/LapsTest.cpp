/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests laps made after the fact: splits, marked ranges, and the laps their
 * starts make.
 */

#include <string>
#include <vector>

#include <storage/Activity.h>
#include <storage/Laps.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

constexpr i64 Start = 1'000'000;

// `count` records a second apart, 5 m apart, as recorded.
struct TestActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    explicit TestActivity(u32 count = 600, bool with_distance = true)
    {
        for (u32 ix = 0; ix < count; ++ix) {
            record rec { .timestamp = DateTime { .timestamp = static_cast<u32>(Start) + ix } };
            if (with_distance) {
                rec.distance = 5.0f * static_cast<float32>(ix);
                rec.speed = 5.0f;
            }
            activity.recorded.push_back(rec);
        }
        activity.records = activity.recorded;
    }
};

std::string error_of(std::expected<Split, std::string> const &split)
{
    return split ? std::string { "<none>" } : split.error();
}

void test_parse_split()
{
    auto const km = parse_split("1 km");
    CHECK(km.has_value() && km->kind == Split::Kind::Distance && km->every == 1000.0f);
    auto const short_km = parse_split(" 0.4km ");
    CHECK(short_km.has_value() && short_km->every == 400.0f);
    auto const minutes = parse_split("10 min");
    CHECK(minutes.has_value() && minutes->kind == Split::Kind::Time && minutes->every == 600.0f);
    for (auto const *bad : { "", "km", "1", "1 mi", "0 km", "-1 km", "1 km 2", "ten min" }) {
        CHECK(error_of(parse_split(bad)).starts_with("Split every distance in km or time in minutes"));
    }
}

// 600 records, 5 m and 1 s apart: 2,995 m in 599 s.
void test_split_by_distance()
{
    TestActivity t;
    auto const   starts = split_starts(t.activity, Split { .kind = Split::Kind::Distance, .every = 1000 });
    CHECK(starts.has_value());
    // At 1,000 m and 2,000 m: records 200 and 400. The last lap is what's
    // left, 995 m.
    CHECK((starts.value_or(std::vector<i64> { }) == std::vector<i64> { Start + 200, Start + 400 }));
    auto const laps = laps_at(t.activity, *starts);
    CHECK_EQ(laps.size(), 3uz);
    if (laps.size() == 3) {
        CHECK_EQ(laps[0].first_record, 0uz);
        CHECK_EQ(laps[0].end_record, 200uz);
        CHECK_EQ(laps[2].end_record, 600uz);
        CHECK_EQ(laps[2].segment.distance, 995.0f);
    }
}

void test_split_by_time()
{
    TestActivity t;
    auto const   starts = split_starts(t.activity, Split { .kind = Split::Kind::Time, .every = 240 });
    CHECK((starts.value_or(std::vector<i64> { }) == std::vector<i64> { Start + 240, Start + 480 }));
    // Longer than the activity: one lap.
    auto const one = split_starts(t.activity, Split { .kind = Split::Kind::Time, .every = 3600 });
    CHECK(one.has_value() && one->empty());
}

// A gap past more than one split makes one lap of it: no empty laps.
void test_split_over_a_gap()
{
    TestActivity t { 10 };
    t.activity.recorded[5].distance = 3000;
    for (size_t ix = 6; ix < 10; ++ix) {
        t.activity.recorded[ix].distance = 3000 + 5.0f * static_cast<float32>(ix);
    }
    auto const starts = split_starts(t.activity, Split { .kind = Split::Kind::Distance, .every = 1000 });
    CHECK((starts.value_or(std::vector<i64> { }) == std::vector<i64> { Start + 5 }));
}

void test_split_refused()
{
    TestActivity indoors { 600, false };
    auto const   no_distance = split_starts(indoors.activity, Split { .kind = Split::Kind::Distance, .every = 1000 });
    CHECK(!no_distance.has_value() && no_distance.error().starts_with("This activity has no distance"));
    CHECK(split_starts(indoors.activity, Split { .kind = Split::Kind::Time, .every = 60 }).has_value());
    TestActivity t;
    auto const   tiny = split_starts(t.activity, Split { .kind = Split::Kind::Distance, .every = 1 });
    CHECK(!tiny.has_value() && tiny.error().starts_with("That would make more than 1000 laps"));
}

// A range in one lap makes three; one across a start makes the laps it
// touches two, with the starts in it gone.
void test_mark()
{
    TestActivity t;
    CHECK((mark_starts(t.activity, { }, 100, 199) == std::vector<i64> { Start + 100, Start + 200 }));
    CHECK((mark_starts(t.activity, { Start + 150, Start + 400 }, 100, 199) == std::vector<i64> { Start + 100, Start + 200, Start + 400 }));
    CHECK((mark_starts(t.activity, { Start + 120, Start + 150, Start + 180 }, 100, 199) == std::vector<i64> { Start + 100, Start + 200 }));
}

// From the first record, or to the last, a range needs only one start.
void test_mark_at_the_ends()
{
    TestActivity t;
    CHECK((mark_starts(t.activity, { }, 0, 99) == std::vector<i64> { Start, Start + 100 }));
    CHECK((laps_at(t.activity, mark_starts(t.activity, { }, 0, 99)).size() == 2uz));
    CHECK((mark_starts(t.activity, { }, 500, 599) == std::vector<i64> { Start + 500 }));
    // Out of bounds: as they were.
    CHECK((mark_starts(t.activity, { Start + 300 }, 500, 600) == std::vector<i64> { Start + 300 }));
}

// An end within 5 s of a start takes it.
void test_mark_snaps()
{
    TestActivity t;
    CHECK((mark_starts(t.activity, { Start + 97, Start + 300 }, 100, 204) == std::vector<i64> { Start + 97, Start + 205, Start + 300 }));
    CHECK((mark_starts(t.activity, { Start + 97, Start + 208 }, 100, 204) == std::vector<i64> { Start + 97, Start + 208 }));
    CHECK((mark_starts(t.activity, { Start + 94, Start + 300 }, 100, 204) == std::vector<i64> { Start + 94, Start + 100, Start + 205, Start + 300 }));
}

// The laps' records, with the stats of their records.
void test_laps_at()
{
    TestActivity t;
    auto const   laps = laps_at(t.activity, { Start + 100, Start + 300 });
    CHECK_EQ(laps.size(), 3uz);
    if (laps.size() != 3) {
        return;
    }
    CHECK_EQ(laps[1].first_record, 100uz);
    CHECK_EQ(laps[1].end_record, 300uz);
    CHECK_EQ(laps[1].segment.distance, 995.0f);
    CHECK_EQ(laps[1].segment.elapsed.elapsed, 199u);
    // No starts: one lap, the whole activity.
    auto const one = laps_at(t.activity, { });
    CHECK(one.size() == 1 && one[0].first_record == 0 && one[0].end_record == 600);
}

// Starts that would make empty laps are passed over: before or at the
// first record, after the last, two on one record, out of order.
void test_laps_at_passes_over_empty_laps()
{
    TestActivity t;
    // A pause of 10 s after record 200: 205 and 208 are both on record 201.
    for (size_t ix = 201; ix < t.activity.recorded.size(); ++ix) {
        t.activity.recorded[ix].timestamp.timestamp += 9;
    }
    auto const laps = laps_at(t.activity, { Start + 400, Start - 10, Start, Start + 205, Start + 208, Start + 5000 });
    CHECK_EQ(laps.size(), 3uz);
    if (laps.size() == 3) {
        CHECK_EQ(laps[1].first_record, 201uz);
        CHECK_EQ(laps[2].first_record, 391uz);
    }
    CHECK(laps_at(TestActivity { 0 }.activity, { Start }).empty());
}

void test_device_lap_starts()
{
    TestActivity t;
    t.activity.device_laps = {
        Lap { .first_record = 0, .end_record = 100 },
        Lap { .first_record = 100, .end_record = 100 },
        Lap { .first_record = 100, .end_record = 600 },
    };
    CHECK((device_lap_starts(t.activity) == std::vector<i64> { Start + 100 }));
}

}

int main()
{
    test_parse_split();
    test_split_by_distance();
    test_split_by_time();
    test_split_over_a_gap();
    test_split_refused();
    test_mark();
    test_mark_at_the_ends();
    test_mark_snaps();
    test_laps_at();
    test_laps_at_passes_over_empty_laps();
    test_device_lap_starts();
    return ST::Test::test_result();
}
