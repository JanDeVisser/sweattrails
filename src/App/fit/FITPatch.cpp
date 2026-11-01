/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>

#include <fit/FITPatch.h>

namespace ST::FIT {

namespace {

constexpr u8 CompressedTimestampHeader = 0x80;
constexpr u8 DefinitionHeader = 0x40;
constexpr u8 DeveloperDataFlag = 0x20;
constexpr u8 LocalTypeMask = 0x0F;

// The local type the added records are defined under. Watches rarely use
// it, but it doesn't matter if one does: its definition is put back after
// them.
constexpr u8 FillLocalType = 15;

constexpr u16 MesgSession = 18;
constexpr u16 MesgLap = 19;
constexpr u16 MesgRecord = 20;
constexpr u16 MesgEvent = 21;
constexpr u16 MesgActivity = 34;

constexpr u8 FieldTimestamp = 253;
constexpr u8 FieldStartTime = 2; // A lap's
constexpr u8 FieldEvent = 0;
constexpr u8 FieldDistance = 5; // A record's
constexpr u8 EventTimer = 0;

constexpr u32 InvalidUint32 = 0xFFFFFFFF;

u32 read_u32(std::string_view bytes, size_t at, bool big_endian)
{
    u32 ret { 0 };
    for (auto ix = 0uz; ix < 4; ++ix) {
        auto const byte = static_cast<u32>(static_cast<u8>(bytes[at + (big_endian ? ix : 3 - ix)]));
        ret = (ret << 8) | byte;
    }
    return ret;
}

u16 read_u16(std::string_view bytes, size_t at, bool big_endian)
{
    auto const first = static_cast<u16>(static_cast<u8>(bytes[at]));
    auto const second = static_cast<u16>(static_cast<u8>(bytes[at + 1]));
    return big_endian ? static_cast<u16>((first << 8) | second) : static_cast<u16>((second << 8) | first);
}

void write_u32(std::string &bytes, size_t at, u32 value, bool big_endian)
{
    for (auto ix = 0uz; ix < 4; ++ix) {
        auto const byte = static_cast<char>((value >> (8 * ix)) & 0xFF);
        bytes[at + (big_endian ? 3 - ix : ix)] = byte;
    }
}

void append_u32(std::string &bytes, u32 value)
{
    bytes.append(4, '\0');
    write_u32(bytes, bytes.size() - 4, value, false);
}

void append_u16(std::string &bytes, u16 value)
{
    bytes.push_back(static_cast<char>(value & 0xFF));
    bytes.push_back(static_cast<char>(value >> 8));
}

// What a definition says about the data messages of its local type.
struct Definition {
    u16                          global { 0 };
    bool                         big_endian { false };
    std::vector<FITMessageField> fields { };
    size_t                       size { 1 }; // Of a data message, the header byte included
};

// A data message's u32 field, if it has one of that size that's valid.
std::optional<u32> u32_field(std::string_view bytes, FITMessage const &message, u8 num)
{
    auto const field = message.field(num);
    if (!field || field->size != 4) {
        return { };
    }
    auto const value = read_u32(bytes, message.offset + field->offset, message.big_endian);
    if (value == InvalidUint32) {
        return { };
    }
    return value;
}

// Raises a data message's u32 field, copied into `out` at `at`, by `by`.
// An invalid value stays invalid.
void raise_u32(std::string &out, size_t at, FITMessage const &message, u8 num, u32 by)
{
    auto const field = message.field(num);
    if (!field || field->size != 4 || by == 0) {
        return;
    }
    auto const value = read_u32(out, at + field->offset, message.big_endian);
    if (value != InvalidUint32) {
        write_u32(out, at + field->offset, value + by, message.big_endian);
    }
}

// The definition of the added records, and the records, under
// FillLocalType.
std::string fill_messages(FITFill const &fill)
{
    struct FieldSpec {
        u8 num;
        u8 size;
        u8 base_type;
    };
    constexpr std::array<FieldSpec, 6> Fields { {
        { .num = FieldTimestamp, .size = 4, .base_type = 0x86 }, // uint32
        { .num = 0, .size = 4, .base_type = 0x85 },              // position_lat, sint32
        { .num = 1, .size = 4, .base_type = 0x85 },              // position_long, sint32
        { .num = 2, .size = 2, .base_type = 0x84 },              // altitude, uint16
        { .num = FieldDistance, .size = 4, .base_type = 0x86 },  // uint32
        { .num = 6, .size = 2, .base_type = 0x84 },              // speed, uint16
    } };
    std::string                        ret;
    ret.push_back(static_cast<char>(DefinitionHeader | FillLocalType));
    ret.push_back(0); // Reserved
    ret.push_back(0); // Little-endian
    append_u16(ret, MesgRecord);
    ret.push_back(static_cast<char>(Fields.size()));
    for (auto const &field : Fields) {
        ret.push_back(static_cast<char>(field.num));
        ret.push_back(static_cast<char>(field.size));
        ret.push_back(static_cast<char>(field.base_type));
    }
    auto const scaled = [](auto value, double scale, double offset, auto invalid) {
        using T = decltype(invalid);
        return value ? static_cast<T>(std::llround((static_cast<double>(*value) + offset) * scale)) : invalid;
    };
    for (auto const &record : fill.records) {
        ret.push_back(static_cast<char>(FillLocalType));
        append_u32(ret, record.timestamp);
        append_u32(ret, static_cast<u32>(record.lat.value_or(0x7FFFFFFF)));
        append_u32(ret, static_cast<u32>(record.lon.value_or(0x7FFFFFFF)));
        append_u16(ret, scaled(record.altitude, 5, 500, u16 { 0xFFFF }));
        append_u32(ret, scaled(record.distance, 100, 0, InvalidUint32));
        append_u16(ret, scaled(record.speed, 1000, 0, u16 { 0xFFFF }));
    }
    return ret;
}

}

std::optional<FITMessageField> FITMessage::field(u8 num) const
{
    if (auto const it = std::ranges::find(fields, num, &FITMessageField::num); it != fields.end()) {
        return *it;
    }
    return { };
}

std::expected<FITScan, std::string> scan_fit(std::string_view bytes)
{
    if (bytes.size() < 12 || bytes.substr(8, 4) != ".FIT") {
        return std::unexpected(std::string { "it isn't a FIT file" });
    }
    FITScan    ret { .header_size = static_cast<u8>(bytes[0]) };
    auto const data_size = read_u32(bytes, 4, false);
    auto const end = ret.header_size + static_cast<size_t>(data_size);
    // The data, and then nothing or its CRC: anything more is a second FIT
    // file chained to the first, which the patch doesn't take apart.
    if (ret.header_size < 12 || end > bytes.size() || (bytes.size() != end && bytes.size() != end + 2)) {
        return std::unexpected(std::string { "its size isn't what its header says" });
    }
    std::map<u8, Definition> definitions;
    auto const               truncated = std::unexpected(std::string { "it's cut short" });
    for (auto offset = ret.header_size; offset < end;) {
        auto const header = static_cast<u8>(bytes[offset]);
        if (header & CompressedTimestampHeader) {
            return std::unexpected(std::string { "it has compressed timestamps" });
        }
        FITMessage message { .offset = offset, .local_type = static_cast<u8>(header & LocalTypeMask) };
        if (header & DefinitionHeader) {
            if (offset + 6 > end) {
                return truncated;
            }
            Definition definition { .big_endian = bytes[offset + 2] != 0 };
            definition.global = read_u16(bytes, offset + 3, definition.big_endian);
            auto const count = static_cast<u8>(bytes[offset + 5]);
            auto       at = offset + 6;
            if (at + 3uz * count > end) {
                return truncated;
            }
            for (auto ix = 0; ix < count; ++ix, at += 3) {
                auto const size = static_cast<u8>(bytes[at + 1]);
                definition.fields.push_back(FITMessageField { .num = static_cast<u8>(bytes[at]), .offset = definition.size, .size = size });
                definition.size += size;
            }
            if (header & DeveloperDataFlag) {
                if (at + 1 > end) {
                    return truncated;
                }
                auto const developer_count = static_cast<u8>(bytes[at]);
                ++at;
                if (at + 3uz * developer_count > end) {
                    return truncated;
                }
                for (auto ix = 0; ix < developer_count; ++ix, at += 3) {
                    definition.size += static_cast<u8>(bytes[at + 1]);
                }
            }
            message.definition = true;
            message.global = definition.global;
            message.size = at - offset;
            definitions[message.local_type] = std::move(definition);
        } else {
            auto const definition = definitions.find(message.local_type);
            if (definition == definitions.end()) {
                return std::unexpected(std::format("it has a message of local type {} without a definition", message.local_type));
            }
            message.global = definition->second.global;
            message.big_endian = definition->second.big_endian;
            message.fields = definition->second.fields;
            message.size = definition->second.size;
        }
        if (offset + message.size > end) {
            return truncated;
        }
        offset += message.size;
        ret.messages.push_back(std::move(message));
    }
    return ret;
}

u16 fit_crc(std::string_view bytes, u16 crc)
{
    constexpr std::array<u16, 16> Table {
        0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
        0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400
    };
    for (auto const ch : bytes) {
        auto const byte = static_cast<u8>(ch);
        auto       tmp = Table[crc & 0xF];
        crc = static_cast<u16>(((crc >> 4) & 0x0FFF) ^ tmp ^ Table[byte & 0xF]);
        tmp = Table[crc & 0xF];
        crc = static_cast<u16>(((crc >> 4) & 0x0FFF) ^ tmp ^ Table[(byte >> 4) & 0xF]);
    }
    return crc;
}

std::expected<std::string, std::string> patch_fit(std::string_view bytes, std::vector<FITFill> const &fills)
{
    auto const scan = scan_fit(bytes);
    if (!scan) {
        return std::unexpected(scan.error());
    }
    auto const has_distance = std::ranges::any_of(scan->messages, [](FITMessage const &message) {
        return !message.definition && message.global == MesgRecord && message.field(FieldDistance);
    });
    if (!has_distance) {
        return std::unexpected(std::string { "its records have no distance" });
    }
    auto const in_gap = [&fills](u32 timestamp) {
        return std::ranges::any_of(fills, [timestamp](FITFill const &fill) { return fill.stop <= timestamp && timestamp <= fill.restart; });
    };
    auto const distance_before = [&fills](u32 timestamp) {
        auto ret = 0.0;
        for (auto const &fill : fills) {
            if (fill.restart <= timestamp) {
                ret += fill.distance;
            }
        }
        return static_cast<u32>(std::llround(ret * 100));
    };
    auto distance = 0.0;
    auto seconds = 0u;
    for (auto const &fill : fills) {
        distance += fill.distance;
        seconds += fill.restart - fill.stop;
    }
    auto const all_distance = static_cast<u32>(std::llround(distance * 100));
    auto const all_time = seconds * 1000;

    std::string       out { bytes.substr(0, scan->header_size) };
    std::vector<bool> inserted(fills.size(), false);
    std::string       fill_type_definition; // The watch's own, to put back
    u32               timestamp { 0 };
    for (auto const &message : scan->messages) {
        auto const raw = bytes.substr(message.offset, message.size);
        if (message.definition) {
            if (message.local_type == FillLocalType) {
                fill_type_definition = raw;
            }
            out += raw;
            continue;
        }
        timestamp = u32_field(bytes, message, FieldTimestamp).value_or(timestamp);
        // The watch's stop and start: the file reads as if it kept running.
        if (message.global == MesgEvent) {
            auto const event = message.field(FieldEvent);
            if (event && event->size == 1 && static_cast<u8>(bytes[message.offset + event->offset]) == EventTimer && in_gap(timestamp)) {
                continue;
            }
        }
        if (message.global == MesgRecord) {
            for (auto ix = 0uz; ix < fills.size(); ++ix) {
                if (!inserted[ix] && timestamp >= fills[ix].restart) {
                    out += fill_messages(fills[ix]);
                    out += fill_type_definition;
                    inserted[ix] = true;
                }
            }
        }
        auto const at = out.size();
        out += raw;
        switch (message.global) {
        case MesgRecord:
            raise_u32(out, at, message, FieldDistance, distance_before(timestamp));
            break;
        case MesgSession:
            raise_u32(out, at, message, 9, all_distance);
            raise_u32(out, at, message, 8, all_time);
            raise_u32(out, at, message, 59, all_time);
            break;
        case MesgActivity:
            raise_u32(out, at, message, 0, all_time);
            break;
        case MesgLap: {
            auto const start = u32_field(bytes, message, FieldStartTime);
            auto       lap_distance = 0.0;
            auto       lap_seconds = 0u;
            for (auto const &fill : fills) {
                if (start && *start <= fill.stop && timestamp >= fill.restart) {
                    lap_distance += fill.distance;
                    lap_seconds += fill.restart - fill.stop;
                }
            }
            raise_u32(out, at, message, 9, static_cast<u32>(std::llround(lap_distance * 100)));
            raise_u32(out, at, message, 8, lap_seconds * 1000);
            raise_u32(out, at, message, 52, lap_seconds * 1000);
            break;
        }
        default:
            break;
        }
    }
    if (!std::ranges::all_of(inserted, std::identity { })) {
        return std::unexpected(std::string { "it has no record after a gap" });
    }
    write_u32(out, 4, static_cast<u32>(out.size() - scan->header_size), false);
    if (scan->header_size >= 14 && read_u16(bytes, 12, false) != 0) {
        auto const header_crc = fit_crc(std::string_view { out }.substr(0, 12));
        out[12] = static_cast<char>(header_crc & 0xFF);
        out[13] = static_cast<char>(header_crc >> 8);
    }
    append_u16(out, fit_crc(out));
    return out;
}

}
