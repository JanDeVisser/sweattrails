/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Builds FIT files byte by byte, for tests of code that reads them: there
 * are no FIT files in the repository.
 */

#pragma once

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <Logging.h>

namespace ST::Test {

// A FIT timestamp: seconds since 1989-12-31.
inline constexpr u32 Start = 1'000'000'000;

// Base types as they're written in a field definition.
inline constexpr u8 Enum = 0x00;
inline constexpr u8 Uint8 = 0x02;
inline constexpr u8 Uint16 = 0x84;
inline constexpr u8 Sint32 = 0x85;
inline constexpr u8 Uint32 = 0x86;
inline constexpr u8 String = 0x07;

struct FieldDefinition {
    u8 num { 0 };
    u8 size { 0 };
    u8 base_type { 0 };
};

// A developer field in a definition: its number within the developer data
// index that a field_description message declared it in.
struct DeveloperFieldDefinition {
    u8 num { 0 };
    u8 size { 0 };
    u8 developer_data_index { 0 };
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
        definition_header(0x40 | local, global, fields);
    }

    // A definition with developer fields after the native ones. Their
    // field_description messages have to come first.
    void definition(u8 local, u16 global, std::vector<FieldDefinition> const &fields, std::vector<DeveloperFieldDefinition> const &developer_fields)
    {
        definition_header(0x60 | local, global, fields);
        byte(static_cast<u8>(developer_fields.size()));
        for (auto const &field : developer_fields) {
            byte(field.num);
            byte(field.size);
            byte(field.developer_data_index);
        }
    }

    void definition_header(u8 header, u16 global, std::vector<FieldDefinition> const &fields)
    {
        byte(header);
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

// The developer_data_id message that opens developer data index `index`.
inline void add_developer_data_id(FITBuilder &fit, u8 index)
{
    fit.definition(7, 207, { { .num = 3, .size = 1, .base_type = Uint8 } });
    fit.message(7);
    fit.byte(index);
}

// The field_description message that declares developer field `num` of
// developer data index `index`, of a base type such as Sint32.
inline void add_field_description(FITBuilder &fit, u8 index, u8 num, u8 base_type)
{
    fit.definition(8, 206,
        {
            { .num = 0, .size = 1, .base_type = Uint8 },
            { .num = 1, .size = 1, .base_type = Uint8 },
            { .num = 2, .size = 1, .base_type = Uint8 },
        });
    fit.message(8);
    fit.byte(index);
    fit.byte(num);
    fit.byte(base_type);
}

// FIT's invalid value for an enum, as a device without a sub-sport writes
// it.
inline constexpr u8 NoSubSport = 0xFF;

// The file_id message of an activity file, made by `manufacturer`, such as
// 260 for Zwift.
inline void add_file_id(FITBuilder &fit, u16 manufacturer)
{
    fit.definition(4, 0,
        {
            { .num = 0, .size = 1, .base_type = Enum },
            { .num = 1, .size = 2, .base_type = Uint16 },
            { .num = 4, .size = 4, .base_type = Uint32 },
        });
    fit.message(4);
    fit.byte(4); // An activity
    fit.u16_le(manufacturer);
    fit.u32_le(Start); // Created
}

// A ride of `count` seconds, with its total ascent; 0xFFFF is FIT's
// invalid value, as a device without a climb writes it. It starts `start`
// seconds after Start. Another `sport` than 2, cycling, makes it another
// activity.
inline void add_session(FITBuilder &fit, u32 count, u16 total_ascent, u8 sub_sport = NoSubSport, u32 start = 0, u8 sport = 2)
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
    fit.u32_le(Start + start + count);
    fit.u32_le(Start + start);
    fit.byte(sport);
    fit.byte(sub_sport);
    fit.u32_le(count * 1000);
    fit.u32_le(count * 1000);
    fit.u16_le(total_ascent);
}

// A record a second for `count` seconds from `start` after Start, at a
// heart rate and a power.
inline void add_records(FITBuilder &fit, u32 count, u8 heart_rate, u16 power, u32 start = 0)
{
    fit.definition(5, 20,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 3, .size = 1, .base_type = Uint8 },
            { .num = 7, .size = 2, .base_type = Uint16 },
        });
    for (auto at = 0u; at < count; ++at) {
        fit.message(5);
        fit.u32_le(Start + start + at);
        fit.byte(heart_rate);
        fit.u16_le(power);
    }
}

// A record a second for `count` seconds after Start, with the distance
// at `speed` m/s, as a run's.
inline void add_distance_records(FITBuilder &fit, u32 count, float speed)
{
    fit.definition(5, 20,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 5, .size = 4, .base_type = Uint32 },
        });
    for (auto at = 0u; at < count; ++at) {
        fit.message(5);
        fit.u32_le(Start + at);
        fit.u32_le(static_cast<u32>(std::lround(speed * static_cast<float>(at) * 100)));
    }
}

// A workout message with just its name, which becomes the title.
inline void add_workout(FITBuilder &fit, std::string_view name)
{
    fit.definition(3, 26, { { .num = 8, .size = static_cast<u8>(name.size() + 1), .base_type = String } });
    fit.message(3);
    fit.data += name;
    fit.byte(0);
}

// An activity that's only a summary, as a program makes them from the
// totals: maker 255, "development", a session, and no records. It starts
// `start` seconds after Start.
inline std::string summary_fit(u32 start = 0)
{
    FITBuilder fit;
    add_file_id(fit, 255);
    add_session(fit, 600, 10, NoSubSport, start);
    return fit.file();
}

// What a Wahoo device recorded of the same: the session, and a record a
// second.
inline std::string recorded_fit(u32 start = 0)
{
    FITBuilder fit;
    add_file_id(fit, 32);
    add_records(fit, 600, 140, 200, start);
    add_session(fit, 600, 10, NoSubSport, start);
    return fit.file();
}

}
