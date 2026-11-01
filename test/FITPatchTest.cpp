/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests patching a FIT file: filling a gap with records, and the distances
 * and totals after it, read back as the app reads an activity.
 */

#include <cmath>

#include <fit/FITPatch.h>
#include <storage/Activity.h>
#include <storage/Storage.h>

#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using namespace ST::FIT;
using namespace ST::Test;

namespace {

inline constexpr u8 Sint32 = 0x85;

struct Field {
    u8  num { 0 };
    u8  size { 0 };
    u8  base_type { 0 };
    u64 value { 0 };
};

void put(FITBuilder &fit, u64 value, u8 size, bool big_endian)
{
    for (auto ix = 0; ix < size; ++ix) {
        auto const shift = 8 * (big_endian ? size - 1 - ix : ix);
        fit.byte(static_cast<u8>((value >> shift) & 0xFF));
    }
}

void define(FITBuilder &fit, u8 local, u16 global, std::vector<Field> const &fields, bool big_endian = false)
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

void data(FITBuilder &fit, u8 local, std::vector<Field> const &fields, bool big_endian = false)
{
    fit.message(local);
    for (auto const &field : fields) {
        put(fit, field.value, field.size, big_endian);
    }
}

// A message with its own definition, as the tests don't mind repeating
// definitions.
void message(FITBuilder &fit, u8 local, u16 global, std::vector<Field> const &fields, bool big_endian = false)
{
    define(fit, local, global, fields, big_endian);
    data(fit, local, fields, big_endian);
}

// 3 m/s north, in semicircles: a degree is 111 km.
i32 lat_at(double metres)
{
    return static_cast<i32>(std::llround(metres / 111'195.0 * (2147483648.0 / 180)));
}

std::vector<Field> record_fields(u32 at, double distance, bool with_distance = true)
{
    std::vector<Field> ret {
        { .num = 253, .size = 4, .base_type = Uint32, .value = Start + at },
        { .num = 0, .size = 4, .base_type = Sint32, .value = static_cast<u32>(lat_at(distance)) },
        { .num = 1, .size = 4, .base_type = Sint32, .value = static_cast<u32>(lat_at(0)) },
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
std::string run(RunOptions const &options = { })
{
    FITBuilder fit;
    add_file_id(fit, 1);
    define(fit, options.record_type, 20, record_fields(0, 0, options.with_distance), options.big_endian);
    for (auto at = 0u; at < 100; ++at) {
        data(fit, options.record_type, record_fields(at, 3.0 * at, options.with_distance), options.big_endian);
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
    define(fit, options.record_type, 20, record_fields(0, 0, options.with_distance), options.big_endian);
    for (auto at = 400u; at < 500; ++at) {
        data(fit, options.record_type, record_fields(at, 300.0 + 3 * (at - 400), options.with_distance), options.big_endian);
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

// The friend's 900 m, from 100 s to 399 s, on from the 297 m at the stop.
FITFill gap()
{
    FITFill ret { .stop = Start + 99, .restart = Start + 400, .distance = 900 };
    for (auto at = 100u; at < 400; ++at) {
        auto const distance = 297.0 + 3 * (at - 99);
        ret.records.push_back(FillRecord { .timestamp = Start + at, .lat = lat_at(distance), .lon = lat_at(0), .altitude = 10, .distance = distance, .speed = 3 });
    }
    return ret;
}

struct Loaded {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    bool     loaded { false };

    explicit Loaded(std::string_view bytes)
    {
        activity.depth = LoadingDepth::Deep;
        auto const result = activity.load_from_slice(bytes);
        loaded = result && *result;
    }
};

size_t timer_events(std::string_view bytes)
{
    auto const scan = scan_fit(bytes);
    if (!scan) {
        return 0;
    }
    return static_cast<size_t>(std::ranges::count_if(scan->messages, [](FITMessage const &message) { return !message.definition && message.global == 21; }));
}

void test_crc()
{
    // CRC-16/ARC's check value, which FIT's CRC is.
    CHECK_EQ(fit_crc("123456789"), u16 { 0xBB3D });
}

void test_scan()
{
    auto const bytes = run();
    auto const scan = scan_fit(bytes);
    CHECK(scan.has_value());
    if (!scan) {
        return;
    }
    CHECK_EQ(scan->header_size, 14uz);
    auto const records = std::ranges::count_if(scan->messages, [](FITMessage const &m) { return !m.definition && m.global == 20; });
    CHECK_EQ(records, 200);
    auto const first = std::ranges::find_if(scan->messages, [](FITMessage const &m) { return !m.definition && m.global == 20; });
    CHECK_EQ(first->size, 18uz);
    CHECK(first->field(5).has_value() && first->field(5)->offset == 14);
}

void test_scan_developer_fields()
{
    FITBuilder fit;
    add_file_id(fit, 1);
    // A definition with a developer field of two bytes.
    fit.byte(0x40 | 0x20 | 6);
    fit.byte(0);
    fit.byte(0);
    fit.u16_le(20);
    fit.byte(1);
    fit.byte(253);
    fit.byte(4);
    fit.byte(Uint32);
    fit.byte(1); // One developer field
    fit.byte(0);
    fit.byte(2);
    fit.byte(0);
    fit.message(6);
    fit.u32_le(Start);
    fit.u16_le(0xBEEF);
    add_session(fit, 60, 0);
    auto const scan = scan_fit(fit.file());
    CHECK(scan.has_value());
    if (scan) {
        CHECK_EQ(scan->messages.size(), 6uz);
        CHECK_EQ(scan->messages[3].size, 7uz);
        CHECK_EQ(scan->messages[4].global, u16 { 18 });
    }
}

void test_scan_refuses()
{
    CHECK(!scan_fit("not a FIT file at all"));
    auto bytes = run();
    CHECK(!scan_fit(bytes.substr(0, bytes.size() - 5)));
    // A compressed timestamp header, after the definitions.
    FITBuilder fit;
    add_file_id(fit, 1);
    define(fit, 1, 20, record_fields(0, 0));
    fit.byte(0x80 | (1 << 5) | 3);
    fit.data.append(14, '\0');
    auto const compressed = scan_fit(fit.file());
    CHECK(!compressed && compressed.error() == "it has compressed timestamps");
}

void test_patch()
{
    auto const patched = patch_fit(run(), { gap() });
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    CHECK_EQ(fit_crc(*patched), u16 { 0 });
    CHECK_EQ(timer_events(*patched), 0uz);
    Loaded const loaded { *patched };
    CHECK(loaded.loaded);
    auto const &records = loaded.activity.recorded;
    CHECK_EQ(records.size(), 500uz);
    if (records.size() != 500) {
        return;
    }
    for (auto ix = 1uz; ix < records.size(); ++ix) {
        if (records[ix].timestamp.timestamp != records[ix - 1].timestamp.timestamp + 1 && ix != 100 && ix != 400) {
            CHECK_EQ(records[ix].timestamp.timestamp, records[ix - 1].timestamp.timestamp + 1);
            break;
        }
    }
    CHECK(records[100].distance && std::abs(*records[100].distance - 300.0f) < 0.01f);
    CHECK(!records[100].heart_rate);
    CHECK(records[100].speed && std::abs(*records[100].speed - 3.0f) < 0.01f);
    // The watch's own after the gap, on by 900 m, and with its heart rate.
    CHECK(records[400].distance && std::abs(*records[400].distance - 1200.0f) < 0.01f);
    CHECK(records[499].distance && std::abs(*records[499].distance - 1497.0f) < 0.01f);
    CHECK(records[499].heart_rate == u8 { 150 });
    // The totals: 301 s and 900 m more.
    CHECK(std::abs(loaded.activity.segment.distance - 1500.0f) < 0.01f);
    CHECK_EQ(loaded.activity.segment.moving.elapsed, 501u);
    CHECK_EQ(loaded.activity.segment.elapsed.elapsed, 499u);
    CHECK_EQ(loaded.activity.laps.size(), 1uz);
    if (!loaded.activity.laps.empty()) {
        CHECK(std::abs(loaded.activity.laps[0].segment.distance - 1500.0f) < 0.01f);
        CHECK_EQ(loaded.activity.laps[0].segment.moving.elapsed, 501u);
    }
}

// The watch's records under local type 15, the one the fill uses: the
// records after the fill still read with the watch's definition.
void test_patch_with_the_fill_type_taken()
{
    auto const patched = patch_fit(run({ .record_type = 15 }), { gap() });
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    Loaded const loaded { *patched };
    CHECK_EQ(loaded.activity.recorded.size(), 500uz);
    if (loaded.activity.recorded.size() == 500) {
        CHECK(loaded.activity.recorded[400].heart_rate == u8 { 150 });
        CHECK(loaded.activity.recorded[499].distance && std::abs(*loaded.activity.recorded[499].distance - 1497.0f) < 0.01f);
    }
}

void test_patch_big_endian()
{
    auto const patched = patch_fit(run({ .big_endian = true }), { gap() });
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    Loaded const loaded { *patched };
    CHECK_EQ(loaded.activity.recorded.size(), 500uz);
    if (loaded.activity.recorded.size() == 500) {
        CHECK(loaded.activity.recorded[400].distance && std::abs(*loaded.activity.recorded[400].distance - 1200.0f) < 0.01f);
    }
}

// Two gaps: the second's distance adds to the first's after it.
void test_patch_two_gaps()
{
    auto first = gap();
    first.restart = Start + 250;
    first.distance = 450;
    std::erase_if(first.records, [](FillRecord const &r) { return r.timestamp >= Start + 250; });
    // A second gap within the first's original span would overlap: put it
    // after the restart, between 450 and 460 s, with nothing in it.
    FITFill    second { .stop = Start + 449, .restart = Start + 450, .distance = 50 };
    auto const patched = patch_fit(run(), { first, second });
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    Loaded const loaded { *patched };
    auto const  &records = loaded.activity.recorded;
    CHECK_EQ(records.size(), 350uz);
    if (records.size() == 350) {
        CHECK(records[250].distance && std::abs(*records[250].distance - 750.0f) < 0.01f);  // 300 + 450
        CHECK(records[349].distance && std::abs(*records[349].distance - 1097.0f) < 0.01f); // 597 + 500
    }
    CHECK(std::abs(loaded.activity.segment.distance - 1100.0f) < 0.01f);
    CHECK_EQ(loaded.activity.segment.moving.elapsed, 200u + 151u + 1u);
}

void test_patch_refuses()
{
    auto const no_distance = patch_fit(run({ .with_distance = false }), { gap() });
    CHECK(!no_distance && no_distance.error() == "its records have no distance");
    auto late = gap();
    late.restart = Start + 600;
    auto const no_record_after = patch_fit(run(), { late });
    CHECK(!no_record_after && no_record_after.error() == "it has no record after a gap");
}

}

int main()
{
    test_crc();
    test_scan();
    test_scan_developer_fields();
    test_scan_refuses();
    test_patch();
    test_patch_with_the_fill_type_taken();
    test_patch_big_endian();
    test_patch_two_gaps();
    test_patch_refuses();
    return Test::test_result();
}
