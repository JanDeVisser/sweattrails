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
};

// A run at 3 m/s: 100 s, the watch stopped at 99 s and started again at
// 400 s, as a friend ran on for 900 m, and 100 s more. The watch's
// distance carries on from 297 m at the stop.
inline std::string run(RunOptions const &options = { })
{
    FITBuilder fit;
    add_file_id(fit, 1);
    define(fit, options.record_type, 20, record_fields(0, 0, 0, options.with_distance), options.big_endian);
    for (auto at = 0u; at < 100; ++at) {
        data(fit, options.record_type, record_fields(at, 3.0 * at, 3.0 * at, options.with_distance), options.big_endian);
    }
    std::vector<Field> const event {
        { .num = 253, .size = 4, .base_type = Uint32, .value = Start + 99 },
        { .num = 0, .size = 1, .base_type = Enum, .value = 0 }, // Timer
        { .num = 1, .size = 1, .base_type = Enum, .value = 4 }, // Stop all
    };
    message(fit, 2, 21, event);
    auto start = event;
    start[0].value = Start + 400;
    start[2].value = 0;
    message(fit, 2, 21, start);
    define(fit, options.record_type, 20, record_fields(0, 0, 0, options.with_distance), options.big_endian);
    for (auto at = 400u; at < 500; ++at) {
        data(fit, options.record_type, record_fields(at, 300.0 + 3 * (at - 400), 1200.0 + 3 * (at - 400), options.with_distance), options.big_endian);
    }
    message(fit, 3, 19,
        {
            { .num = 253, .size = 4, .base_type = Uint32, .value = Start + 499 },
            { .num = 2, .size = 4, .base_type = Uint32, .value = Start },
            { .num = 7, .size = 4, .base_type = Uint32, .value = 499'000 },
            { .num = 8, .size = 4, .base_type = Uint32, .value = 200'000 },
            { .num = 9, .size = 4, .base_type = Uint32, .value = 60'000 },
        });
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

}
