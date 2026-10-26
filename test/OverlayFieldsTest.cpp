/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay's fields, their units and formats, and the text element's
 * templates.
 */

#include <string>
#include <vector>

#include <overlay/Fields.h>
#include <overlay/Template.h>
#include <storage/Storage.h>

#include <TestApp.h>

using namespace ST;

namespace {

constexpr u32 Start = 1'750'000'000;

struct TestActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    explicit TestActivity(sport the_sport = sport::Cycling)
    {
        activity.sport = the_sport;
        activity.segment.start_time.timestamp = Start;
        // 0: everything; 1: no power, heart rate or cadence.
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + 3909 },
            .heart_rate = 142,
            .cadence = 88,
            .distance = 42'195.0f,
            .speed = 10.0f,
            .power = 251,
        });
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + 3910 },
            .distance = 42'205.0f,
            .speed = 0.0f,
        });
    }

    [[nodiscard]] std::string fill(std::string_view text, size_t record = 0, Units units = Units::Metric) const
    {
        auto const parsed = parse_template(text);
        if (!parsed) {
            return "error: " + parsed.error();
        }
        return fill_template(*parsed, activity, record, units, "--");
    }
};

std::string shown(TestActivity const &t, OverlayField field, size_t record = 0, Units units = Units::Metric)
{
    auto const unit = default_unit(field, units, t.activity.sport);
    return format_field(field, field_value(t.activity, record, field), unit, default_spec(field), "--");
}

void test_names()
{
    CHECK_EQ(OverlayField_name(OverlayField::heart_rate), "heart_rate");
    CHECK(overlay_field("heart_rate") == OverlayField::heart_rate);
    CHECK(!overlay_field("hr").has_value());
    CHECK(is_later_field("lap"));
    CHECK(!is_later_field("speed"));
    CHECK(!is_later_field("hr"));
}

void test_metric()
{
    TestActivity t;
    CHECK_EQ(shown(t, OverlayField::speed), "36.0");
    CHECK_EQ(shown(t, OverlayField::power), "251");
    CHECK_EQ(shown(t, OverlayField::heart_rate), "142");
    CHECK_EQ(shown(t, OverlayField::cadence), "88");
    CHECK_EQ(shown(t, OverlayField::distance), "42.2");
    CHECK_EQ(shown(t, OverlayField::elapsed), "1:05:09");
}

void test_imperial()
{
    TestActivity t;
    CHECK_EQ(shown(t, OverlayField::speed, 0, Units::Imperial), "22.4");
    CHECK_EQ(shown(t, OverlayField::distance, 0, Units::Imperial), "26.2");
    CHECK_EQ(default_unit(OverlayField::speed, Units::Imperial, sport::Cycling).name, "mph");
    CHECK_EQ(default_unit(OverlayField::distance, Units::Imperial, sport::Cycling).name, "mi");
    CHECK_EQ(default_unit(OverlayField::power, Units::Imperial, sport::Cycling).name, "W");
}

void test_explicit_units()
{
    TestActivity t;
    auto const   mps = field_unit(OverlayField::speed, "m/s");
    CHECK(mps.has_value());
    if (mps) {
        CHECK_EQ(format_field(OverlayField::speed, field_value(t.activity, 0, OverlayField::speed), *mps, ".2f", "--"), "10.00");
    }
    auto const meters = field_unit(OverlayField::distance, "m");
    CHECK(meters.has_value());
    if (meters) {
        CHECK_EQ(format_field(OverlayField::distance, field_value(t.activity, 0, OverlayField::distance), *meters, ".0f", "--"), "42195");
    }
    CHECK(!field_unit(OverlayField::speed, "km").has_value());
    CHECK(!field_unit(OverlayField::power, "kW").has_value());
}

void test_missing()
{
    TestActivity t;
    CHECK_EQ(shown(t, OverlayField::power, 1), "--");
    CHECK_EQ(shown(t, OverlayField::heart_rate, 1), "--");
    CHECK_EQ(shown(t, OverlayField::speed, 1), "0.0");
    // A record past the end has nothing.
    CHECK_EQ(shown(t, OverlayField::speed, 5), "--");
}

// FIT records the strides of one foot; runners count steps.
void test_running_cadence()
{
    TestActivity t { sport::Running };
    CHECK_EQ(shown(t, OverlayField::cadence), "176");
    CHECK_EQ(default_unit(OverlayField::cadence, Units::Metric, sport::Running).name, "spm");
    CHECK_EQ(default_unit(OverlayField::cadence, Units::Metric, sport::Cycling).name, "rpm");
}

void test_specs()
{
    CHECK(is_valid_spec(".1f"));
    CHECK(is_valid_spec(">6.0f"));
    CHECK(is_valid_spec(""));
    CHECK(!is_valid_spec(".1q"));
    CHECK(!is_valid_spec("d"));
    CHECK(!is_valid_spec("{"));
}

void test_templates()
{
    TestActivity t;
    CHECK_EQ(t.fill("Col du Galibier"), "Col du Galibier");
    CHECK_EQ(t.fill("{speed} {speed.unit}"), "36.0 km/h");
    CHECK_EQ(t.fill("{distance:.2f} {distance.unit}"), "42.20 km");
    CHECK_EQ(t.fill("{distance.unit}", 0, Units::Imperial), "mi");
    CHECK_EQ(t.fill("{elapsed}"), "1:05:09");
    CHECK_EQ(t.fill("{power} W · {heart_rate} bpm"), "251 W · 142 bpm");
    CHECK_EQ(t.fill("{power}", 1), "--");
    CHECK_EQ(t.fill("{{speed}}"), "{speed}");
    CHECK_EQ(t.fill(""), "");
}

// A field of a later slice is missing, and has no unit yet.
void test_later_fields()
{
    TestActivity t;
    CHECK_EQ(t.fill("Lap {lap} · {altitude:.0f}{altitude.unit}"), "Lap -- · --");
}

void test_template_errors()
{
    TestActivity t;
    CHECK_EQ(t.fill("{hr}"), "error: `hr` isn't a field");
    CHECK_EQ(t.fill("{speed:.1q}"), "error: `.1q` isn't a format for `speed`");
    CHECK_EQ(t.fill("{speed.unit:.1f}"), "error: `{speed.unit:.1f}`: a unit has no format");
    CHECK_EQ(t.fill("{speed"), "error: a `{` without a `}`");
    CHECK_EQ(t.fill("speed}"), "error: a `}` without a `{`");
}

}

int main()
{
    test_names();
    test_metric();
    test_imperial();
    test_explicit_units();
    test_missing();
    test_running_cadence();
    test_specs();
    test_templates();
    test_later_fields();
    test_template_errors();
    return ST::Test::test_result();
}
