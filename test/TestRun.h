/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A run with a gap where the watch was stopped, as a FIT file, for the
 * tests of filling it.
 */

#pragma once

#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <TestFit.h>

namespace ST::Test {

inline constexpr u8 Sint32 = 0x85;

struct Field {
    u8  num { 0 };
    u8  size { 0 };
    u8  base_type { 0 };
    u64 value { 0 };
};

inline void put(FITBuilder &fit, u64 value, u8 size, bool big_endian)
{
    for (auto ix = 0; ix < size; ++ix) {
        auto const shift = 8 * (big_endian ? size - 1 - ix : ix);
        fit.byte(static_cast<u8>((value >> shift) & 0xFF));
    }
}

inline void define(FITBuilder &fit, u8 local, u16 global, std::vector<Field> const &fields, bool big_endian = false)
{
    fit.byte(0x40 | local);
    fit.byte(0);
    fit.byte(big_endian ? 1 : 0);
    put(fit, global, 2, big_endian);
    fit.byte(static_cast<u8>(fields.size()));
    for (auto const &field : fields) {
        fit.byte(field.num);
        fit.byte(field.size);
        fit.byte(field.base_type);
    }
}

inline void data(FITBuilder &fit, u8 local, std::vector<Field> const &fields, bool big_endian = false)
{
    fit.message(local);
    for (auto const &field : fields) {
        put(fit, field.value, field.size, big_endian);
    }
}

// A message with its own definition, as the tests don't mind repeating
// definitions.
inline void message(FITBuilder &fit, u8 local, u16 global, std::vector<Field> const &fields, bool big_endian = false)
{
    define(fit, local, global, fields, big_endian);
    data(fit, local, fields, big_endian);
}

// The run is in Amsterdam: at 0° north and east, a device has no fix.
inline constexpr double HomeLat = 52.37;
inline constexpr double HomeLon = 4.90;
inline constexpr double MetresPerDegree = 111'195.0;

inline i32 semicircles_of(double degrees)
{
    return static_cast<i32>(std::llround(degrees * (2147483648.0 / 180)));
}

// `metres` north of home, in semicircles.
inline i32 lat_at(double metres)
{
    return semicircles_of(HomeLat + metres / MetresPerDegree);
}

inline i32 home_lon()
{
    return semicircles_of(HomeLon);
}

// A record `north` metres north of the start, at the watch's `distance`.
inline std::vector<Field> record_fields(u32 at, double distance, double north, bool with_distance = true)
{
    std::vector<Field> ret {
        { .num = 253, .size = 4, .base_type = Uint32, .value = Start + at },
        { .num = 0, .size = 4, .base_type = Sint32, .value = static_cast<u32>(lat_at(north)) },
        { .num = 1, .size = 4, .base_type = Sint32, .value = static_cast<u32>(home_lon()) },
        { .num = 3, .size = 1, .base_type = Uint8, .value = 150 },
    };
    if (with_distance) {
        ret.push_back({ .num = 5, .size = 4, .base_type = Uint32, .value = static_cast<u64>(std::llround(distance * 100)) });
    }
    return ret;
}

struct RunOptions {
    u8   record_type { 1 };
    bool big_endian { false };
    bool with_distance { true };
    // A timer start at 0 s and a stop at the end, around the run, and a
    // battery event (11) in the gap, at 200 s: none of them is the gap's.
    bool other_events { false };
    // Two laps, to 50 s and from 51 s, rather than one: only the second
    // spans the gap.
    bool two_laps { false };
};

inline std::vector<Field> event_fields(u32 at, u8 event, u8 event_type)
{
    return {
        { .num = 253, .size = 4, .base_type = Uint32, .value = Start + at },
        { .num = 0, .size = 1, .base_type = Enum, .value = event },
        { .num = 1, .size = 1, .base_type = Enum, .value = event_type },
    };
}

inline std::vector<Field> lap_fields(u32 from, u32 to, u32 timer_ms, u32 distance_cm)
{
    return {
        { .num = 253, .size = 4, .base_type = Uint32, .value = Start + to },
        { .num = 2, .size = 4, .base_type = Uint32, .value = Start + from },
        { .num = 7, .size = 4, .base_type = Uint32, .value = (to - from) * 1000 },
        { .num = 8, .size = 4, .base_type = Uint32, .value = timer_ms },
        { .num = 9, .size = 4, .base_type = Uint32, .value = distance_cm },
    };
}

// A run at 3 m/s: 100 s, the watch stopped at 99 s and started again at
// 400 s, as a friend ran on for 900 m, and 100 s more. The watch's
// distance carries on from 297 m at the stop.
inline std::string run(RunOptions const &options = { })
{
    FITBuilder fit;
    add_file_id(fit, 1);
    if (options.other_events) {
        message(fit, 2, 21, event_fields(0, 0, 0));
    }
    define(fit, options.record_type, 20, record_fields(0, 0, 0, options.with_distance), options.big_endian);
    for (auto at = 0u; at < 100; ++at) {
        data(fit, options.record_type, record_fields(at, 3.0 * at, 3.0 * at, options.with_distance), options.big_endian);
        if (options.two_laps && at == 50) {
            message(fit, 3, 19, lap_fields(0, 50, 51'000, 15'000));
        }
    }
    std::vector<Field> const event {
        { .num = 253, .size = 4, .base_type = Uint32, .value = Start + 99 },
        { .num = 0, .size = 1, .base_type = Enum, .value = 0 }, // Timer
        { .num = 1, .size = 1, .base_type = Enum, .value = 4 }, // Stop all
    };
    message(fit, 2, 21, event);
    if (options.other_events) {
        message(fit, 2, 21, event_fields(200, 11, 3));
    }
    auto start = event;
    start[0].value = Start + 400;
    start[2].value = 0;
    message(fit, 2, 21, start);
    define(fit, options.record_type, 20, record_fields(0, 0, 0, options.with_distance), options.big_endian);
    for (auto at = 400u; at < 500; ++at) {
        data(fit, options.record_type, record_fields(at, 300.0 + 3 * (at - 400), 1200.0 + 3 * (at - 400), options.with_distance), options.big_endian);
    }
    if (options.other_events) {
        message(fit, 2, 21, event_fields(499, 0, 4));
    }
    if (options.two_laps) {
        message(fit, 3, 19, lap_fields(51, 499, 149'000, 45'000));
    } else {
        message(fit, 3, 19, lap_fields(0, 499, 200'000, 60'000));
    }
    message(fit, 4, 18,
        {
            { .num = 253, .size = 4, .base_type = Uint32, .value = Start + 499 },
            { .num = 2, .size = 4, .base_type = Uint32, .value = Start },
            { .num = 5, .size = 1, .base_type = Enum, .value = 1 }, // Running
            { .num = 7, .size = 4, .base_type = Uint32, .value = 499'000 },
            { .num = 8, .size = 4, .base_type = Uint32, .value = 200'000 },
            { .num = 9, .size = 4, .base_type = Uint32, .value = 60'000 },
        });
    return fit.file();
}

// A run at 3 m/s with two gaps, the watch stopped from 99 s to 200 s and
// from 299 s to 400 s, each time as the friend ran on for 303 m. The
// watch's distance carries on from where it stopped, as a watch's does:
// 297 m at 99 s and 200 s, 594 m at 299 s and 400 s, and 891 m at the end.
// Its position is always 3 m a second north of the start, as the friend's
// of GapsTest.
inline std::string run_two_gaps()
{
    FITBuilder fit;
    add_file_id(fit, 1);
    auto distance = 0.0;
    for (auto const [from, to] : { std::pair { 0u, 100u }, std::pair { 200u, 300u }, std::pair { 400u, 500u } }) {
        if (from > 0) {
            message(fit, 2, 21, event_fields(from, 0, 0)); // The watch started again
        }
        define(fit, 1, 20, record_fields(0, 0, 0));
        for (auto at = from; at < to; ++at) {
            data(fit, 1, record_fields(at, distance + 3.0 * (at - from), 3.0 * at));
        }
        distance += 3.0 * (to - 1 - from);
        if (to < 500) {
            message(fit, 2, 21, event_fields(to - 1, 0, 4)); // The watch stopped
        }
    }
    message(fit, 3, 19, lap_fields(0, 499, 300'000, 89'100));
    message(fit, 4, 18,
        {
            { .num = 253, .size = 4, .base_type = Uint32, .value = Start + 499 },
            { .num = 2, .size = 4, .base_type = Uint32, .value = Start },
            { .num = 5, .size = 1, .base_type = Enum, .value = 1 }, // Running
            { .num = 7, .size = 4, .base_type = Uint32, .value = 499'000 },
            { .num = 8, .size = 4, .base_type = Uint32, .value = 300'000 },
            { .num = 9, .size = 4, .base_type = Uint32, .value = 89'100 },
        });
    return fit.file();
}

// `bytes`, a file of FITBuilder's, with the CRCs a watch writes: the
// header's, and the file's at the end.
inline std::string with_crcs(std::string bytes, u16 (*crc)(std::string_view, u16))
{
    auto const header_crc = crc(std::string_view { bytes }.substr(0, 12), 0);
    bytes[12] = static_cast<char>(header_crc & 0xFF);
    bytes[13] = static_cast<char>(header_crc >> 8);
    auto const file_crc = crc(bytes, 0);
    bytes.push_back(static_cast<char>(file_crc & 0xFF));
    bytes.push_back(static_cast<char>(file_crc >> 8));
    return bytes;
}

}
