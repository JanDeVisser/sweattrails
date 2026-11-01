/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests patching a FIT file: filling a gap with records, and the distances
 * and totals after it, read back as the app reads an activity.
 */

#include <cmath>
#include <optional>
#include <utility>

#include <fit/FITPatch.h>
#include <storage/Activity.h>
#include <storage/Storage.h>

#include <TestApp.h>
#include <TestFit.h>
#include <TestRun.h>

using namespace ST;
using namespace ST::FIT;
using namespace ST::Test;

namespace {

// The friend's 900 m, from 100 s to 399 s, on from the 297 m at the stop.
FITFill gap()
{
    FITFill ret { .stop = Start + 99, .restart = Start + 400, .distance = 900 };
    for (auto at = 100u; at < 400; ++at) {
        auto const distance = 297.0 + 3 * (at - 99);
        ret.records.push_back(FillRecord { .timestamp = Start + at, .lat = lat_at(distance), .lon = home_lon(), .altitude = 10, .distance = distance, .speed = 3 });
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

// A data message's field of 4 bytes, little-endian, if it has one.
std::optional<u32> u32_of(std::string_view bytes, FITMessage const &message, u8 num)
{
    auto const field = message.field(num);
    if (!field || field->size != 4) {
        return { };
    }
    u32 ret { 0 };
    for (auto ix = 4uz; ix > 0; --ix) {
        ret = (ret << 8) | static_cast<u8>(bytes[message.offset + field->offset + ix - 1]);
    }
    return ret;
}

// The data message of global message number `global` at `timestamp`.
std::optional<FITMessage> message_at(FITScan const &scan, std::string_view bytes, u16 global, u32 timestamp)
{
    for (auto const &message : scan.messages) {
        if (!message.definition && message.global == global && u32_of(bytes, message, 253) == timestamp) {
            return message;
        }
    }
    return { };
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
    define(fit, 1, 20, record_fields(0, 0, 0));
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

// A watch writes its CRCs: the header's is worked out again, the file's
// isn't copied but written anew at the end, and the data size is the data's.
void test_patch_with_crcs()
{
    auto const bytes = with_crcs(run(), fit_crc);
    CHECK(scan_fit(bytes).has_value());
    auto const patched = patch_fit(bytes, { gap() });
    auto const plain = patch_fit(run(), { gap() });
    CHECK(patched.has_value() && plain.has_value());
    if (!patched || !plain) {
        return;
    }
    CHECK_EQ(patched->size(), plain->size());
    u32 data_size { 0 };
    for (auto ix = 8uz; ix > 4; --ix) {
        data_size = (data_size << 8) | static_cast<u8>((*patched)[ix - 1]);
    }
    CHECK_EQ(static_cast<size_t>(data_size) + 14 + 2, patched->size());
    CHECK_EQ(fit_crc(std::string_view { *patched }.substr(0, 14)), u16 { 0 });
    CHECK_EQ(fit_crc(*patched), u16 { 0 });
    // A header without a CRC keeps none.
    CHECK((*plain)[12] == 0 && (*plain)[13] == 0);
    Loaded const loaded { *patched };
    CHECK_EQ(loaded.activity.recorded.size(), 500uz);
    // The patched file can be patched again.
    CHECK(scan_fit(*patched).has_value());
}

// Only the timer events in the gap go: the timer's start and stop around
// the run stay, and so does another event in the gap.
void test_patch_keeps_other_events()
{
    auto const patched = patch_fit(run({ .other_events = true }), { gap() });
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    auto const scan = scan_fit(*patched);
    CHECK(scan.has_value());
    if (!scan) {
        return;
    }
    std::vector<std::pair<u32, u8>> events;
    for (auto const &message : scan->messages) {
        if (!message.definition && message.global == 21) {
            events.emplace_back(u32_of(*patched, message, 253).value_or(0), static_cast<u8>((*patched)[message.offset + message.field(0)->offset]));
        }
    }
    CHECK_EQ(events.size(), 3uz);
    if (events.size() == 3) {
        CHECK((events[0] == std::pair { Start, u8 { 0 } }));
        CHECK((events[1] == std::pair { Start + 200, u8 { 11 } }));
        CHECK((events[2] == std::pair { Start + 499, u8 { 0 } }));
    }
}

// Of two laps, only the one the watch stopped in gets the gap's distance
// and time.
void test_patch_only_the_lap_with_the_gap()
{
    auto const patched = patch_fit(run({ .two_laps = true }), { gap() });
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    Loaded const loaded { *patched };
    auto const  &laps = loaded.activity.device_laps;
    CHECK_EQ(laps.size(), 2uz);
    if (laps.size() == 2) {
        CHECK(std::abs(laps[0].segment.distance - 150.0f) < 0.01f);
        CHECK_EQ(laps[0].segment.moving.elapsed, 51u);
        CHECK(std::abs(laps[1].segment.distance - 1350.0f) < 0.01f);
        CHECK_EQ(laps[1].segment.moving.elapsed, 149u + 301u);
    }
    CHECK(std::abs(loaded.activity.segment.distance - 1500.0f) < 0.01f);
}

// A record after the gap without a distance, all ones, keeps none; the
// next one is raised.
void test_patch_keeps_an_invalid_distance()
{
    auto       bytes = run();
    auto const scan = scan_fit(bytes);
    CHECK(scan.has_value());
    if (!scan) {
        return;
    }
    auto const record = message_at(*scan, bytes, 20, Start + 450);
    CHECK(record.has_value());
    if (!record) {
        return;
    }
    bytes.replace(record->offset + record->field(5)->offset, 4, 4, '\xFF');
    auto const patched = patch_fit(bytes, { gap() });
    CHECK(patched.has_value());
    if (!patched) {
        return;
    }
    auto const patched_scan = scan_fit(*patched);
    CHECK(patched_scan.has_value());
    if (!patched_scan) {
        return;
    }
    auto const invalid = message_at(*patched_scan, *patched, 20, Start + 450);
    auto const next = message_at(*patched_scan, *patched, 20, Start + 451);
    CHECK(invalid && u32_of(*patched, *invalid, 5) == 0xFFFFFFFFu);
    CHECK(next && u32_of(*patched, *next, 5) == 135'300u); // 300 + 153 + 900 m
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
    test_patch_with_crcs();
    test_patch_keeps_other_events();
    test_patch_only_the_lap_with_the_gap();
    test_patch_keeps_an_invalid_distance();
    return Test::test_result();
}
