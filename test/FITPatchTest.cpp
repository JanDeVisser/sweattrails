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
