/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests loading an activity from a FIT file, built byte by byte in the
 * test: there are no FIT files in the repository.
 */

#include <optional>
#include <string>
#include <vector>

#include <storage/Activity.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

// A FIT timestamp: seconds since 1989-12-31.
constexpr u32 Start = 1'000'000'000;

// Base types as they're written in a field definition.
constexpr u8 Enum = 0x00;
constexpr u8 Uint8 = 0x02;
constexpr u8 Uint16 = 0x84;
constexpr u8 Uint32 = 0x86;

struct FieldDefinition {
    u8 num { 0 };
    u8 size { 0 };
    u8 base_type { 0 };
};

// The data messages of a little-endian FIT file, and the file around them.
struct FITBuilder {
    std::string data { };

    void byte(u8 b)
    {
        data.push_back(static_cast<char>(b));
    }

    void u16_le(u16 v)
    {
        byte(static_cast<u8>(v & 0xFF));
        byte(static_cast<u8>(v >> 8));
    }

    void u32_le(u32 v)
    {
        u16_le(static_cast<u16>(v & 0xFFFF));
        u16_le(static_cast<u16>(v >> 16));
    }

    void definition(u8 local, u16 global, std::vector<FieldDefinition> const &fields)
    {
        byte(0x40 | local);
        byte(0); // Reserved
        byte(0); // Little-endian
        u16_le(global);
        byte(static_cast<u8>(fields.size()));
        for (auto const &field : fields) {
            byte(field.num);
            byte(field.size);
            byte(field.base_type);
        }
    }

    // A data message's header: the values follow, in the definition's order.
    void message(u8 local)
    {
        byte(local);
    }

    [[nodiscard]] std::string file() const
    {
        FITBuilder header;
        header.byte(14);
        header.byte(0x20);
        header.u16_le(2100);
        header.u32_le(static_cast<u32>(data.size()));
        header.data += ".FIT";
        header.u16_le(0);
        return header.data + data;
    }
};

// FIT's invalid value for an enum, as a device without a sub-sport writes
// it.
constexpr u8 NoSubSport = 0xFF;

// A ride of `count` seconds, with its total ascent; 0xFFFF is FIT's
// invalid value, as a device without a climb writes it.
void add_session(FITBuilder &fit, u32 count, u16 total_ascent, u8 sub_sport = NoSubSport)
{
    fit.definition(0, 18,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 2, .size = 4, .base_type = Uint32 },
            { .num = 5, .size = 1, .base_type = Enum },
            { .num = 6, .size = 1, .base_type = Enum },
            { .num = 7, .size = 4, .base_type = Uint32 },
            { .num = 8, .size = 4, .base_type = Uint32 },
            { .num = 22, .size = 2, .base_type = Uint16 },
        });
    fit.message(0);
    fit.u32_le(Start + count);
    fit.u32_le(Start);
    fit.byte(2); // Cycling
    fit.byte(sub_sport);
    fit.u32_le(count * 1000);
    fit.u32_le(count * 1000);
    fit.u16_le(total_ascent);
}

void add_lap(FITBuilder &fit, u32 start, u32 count)
{
    fit.definition(1, 19,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 2, .size = 4, .base_type = Uint32 },
            { .num = 7, .size = 4, .base_type = Uint32 },
            { .num = 8, .size = 4, .base_type = Uint32 },
        });
    fit.message(1);
    fit.u32_le(Start + start + count);
    fit.u32_le(Start + start);
    fit.u32_le(count * 1000);
    fit.u32_le(count * 1000);
}

void add_record(FITBuilder &fit, u32 at, float32 altitude, u8 cadence)
{
    fit.definition(2, 20,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 2, .size = 2, .base_type = Uint16 },
            { .num = 4, .size = 1, .base_type = Uint8 },
        });
    fit.message(2);
    fit.u32_le(Start + at);
    // Altitude is in 1/5 m, from -500 m.
    fit.u16_le(static_cast<u16>((altitude + 500.0f) * 5.0f));
    fit.byte(cadence);
}

struct LoadedActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    bool     loaded { false };

    explicit LoadedActivity(std::string const &file)
    {
        activity.depth = LoadingDepth::Deep;
        auto const res = activity.load_from_slice(file);
        loaded = res.has_value() && *res;
    }
};

void test_session_total_ascent_is_loaded()
{
    FITBuilder fit;
    add_session(fit, 3600, 345);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(t.activity.segment.total_ascent.has_value());
    CHECK_EQ(t.activity.segment.total_ascent.value_or(0), 345);
    CHECK_EQ(t.activity.segment.climb(), 345.0f);
    // The session's other fields still load, after the new one.
    CHECK_EQ(t.activity.segment.moving.elapsed, 3600u);
}

void test_session_without_total_ascent()
{
    FITBuilder fit;
    add_session(fit, 3600, 0xFFFF);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(!t.activity.segment.total_ascent.has_value());
}

void test_session_sub_sport_is_loaded()
{
    FITBuilder fit;
    add_session(fit, 3600, 345, 8); // Mountain
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(t.activity.sport == sport::Cycling);
    CHECK(t.activity.sub_sport == FIT::sub_sport::mountain);
}

void test_session_without_sub_sport()
{
    FITBuilder fit;
    add_session(fit, 3600, 345);
    LoadedActivity t { fit.file() };
    CHECK(t.loaded);
    CHECK(!t.activity.sub_sport.has_value());
}

// Two laps of 20 records. Lap 1 jitters by 1 m and has a stop at record
// 5; lap 2 climbs from 100 m to 110 m in 1 m steps. The session has the
// device's climb, the laps work out their own, with the hysteresis.
std::string two_laps()
{
    FITBuilder fit;
    add_lap(fit, 0, 20);
    add_lap(fit, 20, 20);
    for (u32 ix = 0; ix < 40; ++ix) {
        auto altitude = (ix < 20) ? 100.0f + static_cast<float32>(ix % 2) : 100.0f + static_cast<float32>(std::min(ix - 20, 10u));
        add_record(fit, ix, altitude, (ix == 5) ? 0 : 90);
    }
    add_session(fit, 40, 345);
    return fit.file();
}

void test_laps_work_out_their_own_climb()
{
    LoadedActivity t { two_laps() };
    CHECK(t.loaded);
    CHECK_EQ(t.activity.records.size(), 40uz);
    CHECK_EQ(t.activity.laps.size(), 2uz);
    if (t.activity.laps.size() != 2) {
        return;
    }
    CHECK_EQ(t.activity.segment.climb(), 345.0f);
    auto const &lap1 = t.activity.laps[0].segment;
    auto const &lap2 = t.activity.laps[1].segment;
    CHECK(!lap1.total_ascent.has_value());
    CHECK_EQ(lap1.climb(), 0.0f);
    CHECK_EQ(lap2.climb(), 9.0f);
}

// The session's cadence leaves out the stop at record 5: the average is
// 90, and so is the minimum, over the 30 records after the stop. A lap's
// cadence should be worked out from the same, recorded, cadence: smoothed,
// the records after the stop would be pedaling at 60, and pull the
// average down. A lap of 20 records has no minimum.
void test_lap_cadence_is_from_the_recorded_cadence()
{
    LoadedActivity t { two_laps() };
    CHECK(t.loaded);
    auto const &session = t.activity.segment.cadence;
    CHECK(session.has_value());
    if (session) {
        CHECK_EQ(session->average, 90.0f);
        CHECK_EQ(session->min.value_or(0), 90.0f);
        CHECK_EQ(session->min_at, 35uz);
    }
    if (t.activity.laps.empty()) {
        CHECK(!t.activity.laps.empty());
        return;
    }
    auto const &lap = t.activity.laps[0].segment.cadence;
    CHECK(lap.has_value());
    if (lap) {
        CHECK_EQ(lap->average, 90.0f);
        CHECK(!lap->min.has_value());
    }
}

}

int main()
{
    test_session_total_ascent_is_loaded();
    test_session_without_total_ascent();
    test_session_sub_sport_is_loaded();
    test_session_without_sub_sport();
    test_laps_work_out_their_own_climb();
    test_lap_cadence_is_from_the_recorded_cadence();
    return ST::Test::test_result();
}
