/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Builds FIT files byte by byte, for tests of code that reads them: there
 * are no FIT files in the repository.
 */

#pragma once

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
inline constexpr u8 Uint32 = 0x86;
inline constexpr u8 String = 0x07;

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
inline constexpr u8 NoSubSport = 0xFF;

// A ride of `count` seconds, with its total ascent; 0xFFFF is FIT's
// invalid value, as a device without a climb writes it.
inline void add_session(FITBuilder &fit, u32 count, u16 total_ascent, u8 sub_sport = NoSubSport)
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

// A workout message with just its name, which becomes the title.
inline void add_workout(FITBuilder &fit, std::string_view name)
{
    fit.definition(3, 26, { { .num = 8, .size = static_cast<u8>(name.size() + 1), .base_type = String } });
    fit.message(3);
    fit.data += name;
    fit.byte(0);
}

}
