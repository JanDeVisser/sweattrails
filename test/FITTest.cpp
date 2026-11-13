/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests reading record messages from FIT files with developer fields and
 * with half a position (specs/49-developer-field-numbers.md), with FIT
 * files built byte by byte with TestFit.h.
 */

#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <fit/FIT.h>
#include <fit/Profile.h>

#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using namespace ST::FIT;
using namespace ST::Test;

namespace {

// Waterloo, Ontario, in semicircles: 2³¹ of them make 180°.
constexpr i32 WaterlooLat = 518'302'720;    // 43.44°
constexpr i32 WaterlooLon = -960'655'360;   // -80.52°
constexpr i32 DeveloperValue = 518'000'000; // Around 43.4°, as Wahoo's "adc" field

constexpr u8 PositionLat = 0;
constexpr u8 PositionLong = 1;

void i32_le(FITBuilder &fit, i32 v)
{
    fit.u32_le(static_cast<u32>(v));
}

// The developer data index 0 with its field 0, a sint32, as Wahoo
// declares it.
void add_developer_field_zero(FITBuilder &fit)
{
    add_developer_data_id(fit, 0);
    add_field_description(fit, 0, 0, Sint32);
}

// The record messages of a FIT file, read the way the app reads them.
struct ReadRecords {
    FITFile                    fit_file;
    std::vector<FITDataRecord> records { };

    explicit ReadRecords(std::string const &file)
        : fit_file(file)
    {
        auto const read = fit_file.read_until([this](FITDataRecord const &rec) {
            if (rec.mesg_num == mesg_num::record) {
                records.push_back(rec);
            }
            return false; // Read on
        });
        CHECK(read.has_value());
    }

    std::optional<Coordinates> position(size_t ix) const
    {
        auto const rec = make_from_rec<record>(records[ix]);
        CHECK(rec.has_value());
        return rec ? rec->position : std::nullopt;
    }
};

// Wahoo's records without a position: their definition has no native
// position fields, but it has developer field 0.
void test_developer_field_is_not_a_native_field()
{
    FITBuilder fit;
    add_developer_field_zero(fit);
    fit.definition(0, 20, { { .num = 253, .size = 4, .base_type = Uint32 } }, { { .num = 0, .size = 4, .developer_data_index = 0 } });
    fit.message(0);
    fit.u32_le(Start);
    i32_le(fit, DeveloperValue);

    ReadRecords t { fit.file() };
    CHECK_EQ(t.records.size(), 1u);
    if (t.records.size() != 1) {
        return;
    }
    CHECK(!t.records[0].get_field(PositionLat).has_value());
    CHECK(!t.position(0).has_value());
}

void test_native_field_wins_over_developer_field()
{
    FITBuilder fit;
    add_developer_field_zero(fit);
    fit.definition(0, 20,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 0, .size = 4, .base_type = Sint32 },
            { .num = 1, .size = 4, .base_type = Sint32 },
        },
        { { .num = 0, .size = 4, .developer_data_index = 0 } });
    fit.message(0);
    fit.u32_le(Start);
    i32_le(fit, WaterlooLat);
    i32_le(fit, WaterlooLon);
    i32_le(fit, DeveloperValue);

    ReadRecords t { fit.file() };
    CHECK_EQ(t.records.size(), 1u);
    if (t.records.size() != 1) {
        return;
    }
    auto const lat = t.records[0].get_field(PositionLat);
    CHECK(lat && lat->value);
    if (lat && lat->value) {
        CHECK_EQ(lat->value->sint32, WaterlooLat);
    }
}

// A record with one coordinate of the two, with `num` the one it has.
std::string half_position_file(u8 num, i32 value)
{
    FITBuilder fit;
    fit.definition(0, 20,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = num, .size = 4, .base_type = Sint32 },
        });
    fit.message(0);
    fit.u32_le(Start);
    i32_le(fit, value);
    return fit.file();
}

void test_latitude_alone_is_no_position()
{
    ReadRecords t { half_position_file(PositionLat, WaterlooLat) };
    CHECK_EQ(t.records.size(), 1u);
    if (t.records.size() == 1) {
        CHECK(!t.position(0).has_value());
    }
}

void test_longitude_alone_is_no_position()
{
    ReadRecords t { half_position_file(PositionLong, WaterlooLon) };
    CHECK_EQ(t.records.size(), 1u);
    if (t.records.size() == 1) {
        CHECK(!t.position(0).has_value());
    }
}

std::string waterloo_file()
{
    FITBuilder fit;
    fit.definition(0, 20,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 0, .size = 4, .base_type = Sint32 },
            { .num = 1, .size = 4, .base_type = Sint32 },
        });
    fit.message(0);
    fit.u32_le(Start);
    i32_le(fit, WaterlooLat);
    i32_le(fit, WaterlooLon);
    return fit.file();
}

void test_both_coordinates_are_a_position()
{
    ReadRecords t { waterloo_file() };
    CHECK_EQ(t.records.size(), 1u);
    if (t.records.size() != 1) {
        return;
    }
    auto const position = t.position(0);
    CHECK(position.has_value());
    if (position) {
        CHECK(std::abs(position->lat - 43.44f) < 0.01f);
        CHECK(std::abs(position->lon + 80.52f) < 0.01f);
    }
}

// fitexplore prints records with format_record().
void test_formatted_position_has_the_right_sign()
{
    ReadRecords t { waterloo_file() };
    CHECK_EQ(t.records.size(), 1u);
    if (t.records.size() != 1) {
        return;
    }
    std::ostringstream out;
    format_record<mesg_num::record>(out, t.records[0]);
    auto const text = out.str();
    CHECK(text.contains(" 43.4º lat"));
    CHECK(text.contains("-80.5º long"));
}

}

int main()
{
    test_developer_field_is_not_a_native_field();
    test_native_field_wins_over_developer_field();
    test_latitude_alone_is_no_position();
    test_longitude_alone_is_no_position();
    test_both_coordinates_are_a_position();
    test_formatted_position_has_the_right_sign();
    return test_result();
}
