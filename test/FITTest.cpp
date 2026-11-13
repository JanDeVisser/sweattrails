/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests reading record messages from FIT files with developer fields
 * (specs/49-developer-field-numbers.md), with FIT files built byte by byte
 * with TestFit.h.
 */

#include <optional>
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

}

int main()
{
    test_developer_field_is_not_a_native_field();
    test_native_field_wins_over_developer_field();
    return test_result();
}
