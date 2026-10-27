/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay's fields, their units and formats, and the text element's
 * templates.
 */

#include <cstdlib>
#include <ctime>
#include <format>
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
        return fill_template(*parsed, values_at_record(OverlaySource { activity }, record), units, "--");
    }
};

std::string shown(TestActivity const &t, OverlayField field, size_t record = 0, Units units = Units::Metric)
{
    auto const unit = default_unit(field, units, t.activity.sport);
    return format_field(field, field_value(OverlaySource { t.activity }, record, field), unit, default_spec(field), "--");
}

void test_names()
{
    CHECK_EQ(OverlayField_name(OverlayField::heart_rate), "heart_rate");
    CHECK(overlay_field("heart_rate") == OverlayField::heart_rate);
    CHECK(!overlay_field("hr").has_value());
    CHECK(overlay_field("lr_balance") == OverlayField::lr_balance);
    CHECK(!overlay_field("power.avg").has_value());
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
        CHECK_EQ(format_field(OverlayField::speed, field_value(OverlaySource { t.activity }, 0, OverlayField::speed), *mps, ".2f", "--"), "10.00");
    }
    auto const meters = field_unit(OverlayField::distance, "m");
    CHECK(meters.has_value());
    if (meters) {
        CHECK_EQ(format_field(OverlayField::distance, field_value(OverlaySource { t.activity }, 0, OverlayField::distance), *meters, ".0f", "--"), "42195");
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

// The fields that were for a later slice until this one.
void test_later_fields()
{
    TestActivity t;
    CHECK_EQ(t.fill("Lap {lap} · {altitude:.0f}{altitude.unit}"), "Lap 1 · --m");
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

// An explicit unit for running cadence: rpm is the strides of one foot, as
// in the FIT file, so it's half the steps.
void test_running_cadence_in_rpm()
{
    TestActivity t { sport::Running };
    auto const   rpm = field_unit(OverlayField::cadence, "rpm");
    auto const   spm = field_unit(OverlayField::cadence, "spm");
    CHECK(rpm.has_value() && spm.has_value());
    if (rpm && spm) {
        auto const value = field_value(OverlaySource { t.activity }, 0, OverlayField::cadence);
        CHECK_EQ(format_field(OverlayField::cadence, value, *spm, ".0f", "--"), "176");
        CHECK_EQ(format_field(OverlayField::cadence, value, *rpm, ".0f", "--"), "88");
    }
    CHECK_EQ(t.fill("{cadence} {cadence.unit}"), "176 spm");
}

void test_elapsed()
{
    TestActivity t;
    // Before the start: not negative.
    t.activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start - 5 } });
    // Ten hours and more.
    t.activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 36'061 } });
    CHECK_EQ(shown(t, OverlayField::elapsed, 2), "0:00:00");
    CHECK_EQ(shown(t, OverlayField::elapsed, 3), "10:01:01");
    // A record past the end has no time either.
    CHECK_EQ(shown(t, OverlayField::elapsed, 4), "--");
    CHECK_EQ(t.fill("{elapsed:ms}"), "65:09");
    CHECK_EQ(t.fill("{elapsed:hms}"), "1:05:09");
    CHECK_EQ(format_field(OverlayField::elapsed, 3909.0, FieldUnit { }, "hms", "--"), "1:05:09");
    CHECK_EQ(t.fill("{elapsed:.1f}"), "error: `.1f` isn't a format for `elapsed`");
}

// An activity without records: everything is missing.
void test_no_records()
{
    TestActivity t;
    t.activity.records.clear();
    CHECK_EQ(shown(t, OverlayField::speed, 0), "--");
    CHECK_EQ(shown(t, OverlayField::elapsed, 0), "--");
    CHECK_EQ(t.fill("{speed} {speed.unit}"), "-- km/h");
}

void test_template_braces()
{
    TestActivity t;
    CHECK_EQ(t.fill("{{{speed}}}"), "{36.0}");
    CHECK_EQ(t.fill("{speed}}}"), "36.0}");
    CHECK_EQ(t.fill("}}{{"), "}{");
    CHECK_EQ(t.fill("{speed:}"), "36.0");
    CHECK_EQ(t.fill("{speed:>7.2f}|"), "  36.00|");
    CHECK_EQ(t.fill("{heart_rate}"), "142");
    CHECK_EQ(t.fill("{}"), "error: `` isn't a field");
    CHECK_EQ(t.fill("{speed:{}}"), "error: `{` isn't a format for `speed`");
    CHECK_EQ(t.fill("{speed.units}"), "error: `speed.units` isn't a field");
    CHECK_EQ(t.fill("{ speed }"), "error: ` speed ` isn't a field");
    // A field without a unit has an empty one.
    CHECK_EQ(t.fill("[{lap.unit}]"), "[]");
}

// Specs that would make std::vformat throw at draw time are caught.
void test_specs_that_throw()
{
    CHECK(!is_valid_spec("{}"));
    CHECK(!is_valid_spec("{0}"));
    CHECK(!is_valid_spec("s"));
    CHECK(!is_valid_spec("}"));
    CHECK(is_valid_spec("+08.3e"));
}

std::string value_of(FieldValues const &values, OverlayField field)
{
    auto const value = values[field];
    return value ? std::format("{}", *value) : std::string { "none" };
}

// Records at 0, 1 and 10 seconds: the last two are more than MaxGap apart.
struct InterpolatedActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    InterpolatedActivity()
    {
        activity.segment.start_time.timestamp = Start;
        activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start }, .heart_rate = 140, .speed = 10.0f, .power = 200 });
        activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 1 }, .speed = 12.0f, .power = 300 });
        activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 10 }, .heart_rate = 150, .speed = 20.0f, .power = 100 });
    }
};

void test_values_at_time()
{
    InterpolatedActivity t;
    auto const           half = values_at_time(OverlaySource { t.activity }, 0.5);
    CHECK_EQ(value_of(half, OverlayField::speed), "11");
    CHECK_EQ(value_of(half, OverlayField::power), "250");
    // The later record has no heart rate: the earlier's.
    CHECK_EQ(value_of(half, OverlayField::heart_rate), "140");
    CHECK_EQ(value_of(half, OverlayField::elapsed), "0.5");
    CHECK_EQ(value_of(half, OverlayField::cadence), "none");
    CHECK_EQ(value_of(values_at_time(OverlaySource { t.activity }, 0), OverlayField::speed), "10");
    CHECK_EQ(value_of(values_at_time(OverlaySource { t.activity }, 1), OverlayField::speed), "12");
    // Over the gap: the earlier record.
    CHECK_EQ(value_of(values_at_time(OverlaySource { t.activity }, 5), OverlayField::speed), "12");
    CHECK_EQ(value_of(values_at_time(OverlaySource { t.activity }, 9.9), OverlayField::power), "300");
    CHECK_EQ(value_of(values_at_time(OverlaySource { t.activity }, 10), OverlayField::speed), "20");
    // Past the ends: the first and the last record, with the time running on.
    CHECK_EQ(value_of(values_at_time(OverlaySource { t.activity }, -3), OverlayField::speed), "10");
    auto const after = values_at_time(OverlaySource { t.activity }, 20);
    CHECK_EQ(value_of(after, OverlayField::speed), "20");
    CHECK_EQ(value_of(after, OverlayField::elapsed), "20");
}

void test_values_without_records()
{
    InterpolatedActivity t;
    t.activity.records.clear();
    auto const values = values_at_time(OverlaySource { t.activity }, 7);
    CHECK_EQ(value_of(values, OverlayField::speed), "none");
    CHECK_EQ(value_of(values, OverlayField::elapsed), "7");
}

void test_values_at_record()
{
    TestActivity t { sport::Running };
    auto const   values = values_at_record(OverlaySource { t.activity }, 0);
    CHECK(values.sport == sport::Running);
    CHECK_EQ(value_of(values, OverlayField::power), "251");
    CHECK_EQ(value_of(values, OverlayField::elapsed), "3909");
    CHECK_EQ(value_of(values_at_record(OverlaySource { t.activity }, 1), OverlayField::power), "none");
}

// 2025-06-15 15:06:40 UTC is 11:06:40 in Toronto, in the summer.
constexpr i32 Toronto = -4 * 3600;

// The fields of this slice, on records that have them.
struct NewFieldsActivity {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    NewFieldsActivity()
    {
        activity.sport = sport::Running;
        activity.title = "Rückweg";
        activity.utc_offset = Toronto;
        activity.segment.start_time.timestamp = Start;
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + 3909 },
            .altitude = 1234.4f,
            .speed = 3.7f,
            .temperature = 21,
            .left_right_balance = 0x80 | 52,
        });
        // Walking: too slow for a pace; a balance without its side.
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + 3910 },
            .speed = 0.2f,
            .temperature = -3,
            .left_right_balance = 52,
        });
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + 3911 },
            .speed = 0.0f,
            .left_right_balance = 0xff,
        });
    }

    [[nodiscard]] std::string field(OverlayField f, size_t record = 0, Units units = Units::Metric, std::string_view spec = "") const
    {
        auto const unit = default_unit(f, units, activity.sport);
        auto const s = spec.empty() ? default_spec(f) : spec;
        return format_value(values_at_record(OverlaySource { activity }, record), f, unit, s, "--");
    }

    [[nodiscard]] std::string fill(std::string_view text, size_t record = 0, Units units = Units::Metric) const
    {
        auto const parsed = parse_template(text);
        if (!parsed) {
            return "error: " + parsed.error();
        }
        return fill_template(*parsed, values_at_record(OverlaySource { activity }, record), units, "--");
    }
};

void test_pace()
{
    NewFieldsActivity t;
    // 3.7 m/s is 270 s/km.
    CHECK_EQ(t.field(OverlayField::pace), "4:30");
    CHECK_EQ(t.field(OverlayField::pace, 0, Units::Imperial), "7:15");
    CHECK_EQ(t.fill("{pace} {pace.unit}"), "4:30 min/km");
    CHECK_EQ(t.fill("{pace.unit}", 0, Units::Imperial), "min/mi");
    // Below 1 km/h, and standing still.
    CHECK_EQ(t.field(OverlayField::pace, 1), "--");
    CHECK_EQ(t.field(OverlayField::pace, 2), "--");
    CHECK_EQ(t.fill("{pace:hms}"), "error: `hms` isn't a format for `pace`");
}

void test_altitude_and_temperature()
{
    NewFieldsActivity t;
    CHECK_EQ(t.field(OverlayField::altitude), "1234");
    CHECK_EQ(t.field(OverlayField::altitude, 0, Units::Imperial), "4050");
    CHECK_EQ(t.field(OverlayField::altitude, 1), "--");
    CHECK_EQ(t.fill("{temperature}{temperature.unit}"), "21°C");
    CHECK_EQ(t.fill("{temperature}{temperature.unit}", 0, Units::Imperial), "70°F");
    CHECK_EQ(t.fill("{temperature}", 1, Units::Imperial), "27");
    CHECK_EQ(t.fill("{temperature:.1f}", 1), "-3.0");
    auto const kelvin = field_unit(OverlayField::temperature, "K");
    CHECK(!kelvin.has_value());
}

// Left/right: bit 0x80 says it's the right leg's share.
void test_balance()
{
    NewFieldsActivity t;
    CHECK_EQ(t.field(OverlayField::lr_balance), "48/52");
    CHECK_EQ(t.fill("{lr_balance:.1f}"), "48.0/52.0");
    // Without the bit the side isn't known; 0xff is none.
    CHECK_EQ(t.field(OverlayField::lr_balance, 1), "--");
    CHECK_EQ(t.field(OverlayField::lr_balance, 2), "--");
}

void test_laps()
{
    NewFieldsActivity t;
    CHECK_EQ(t.field(OverlayField::lap, 2), "1");
    t.activity.laps.push_back(Lap { .first_record = 0, .end_record = 2 });
    t.activity.laps.push_back(Lap { .first_record = 2, .end_record = 3 });
    CHECK_EQ(t.field(OverlayField::lap, 1), "1");
    CHECK_EQ(t.fill("Lap {lap}", 2), "Lap 2");
}

void test_title()
{
    NewFieldsActivity t;
    CHECK_EQ(t.fill("{title}"), "Rückweg");
    CHECK_EQ(t.field(OverlayField::title), "Rückweg");
    CHECK_EQ(t.fill("{title:>10}"), "error: `>10` isn't a format for `title`");
    t.activity.title.clear();
    CHECK_EQ(t.fill("{title}"), "--");
}

void test_date_and_time_of_day()
{
    NewFieldsActivity t;
    CHECK_EQ(t.field(OverlayField::date), "15 Jun 2025");
    CHECK_EQ(t.fill("{date:%Y-%m-%d}"), "2025-06-15");
    CHECK_EQ(t.fill("{date:%A}"), "Sunday");
    // 3909 s after 11:06:40.
    CHECK_EQ(t.field(OverlayField::time_of_day), "12:11");
    CHECK_EQ(t.fill("{time_of_day:hms}"), "12:11:49");
    CHECK_EQ(t.fill("{time_of_day:hh}"), "error: `hh` isn't a format for `time_of_day`");
    CHECK_EQ(t.fill("{date:%Q}"), "error: `%Q` isn't a format for `date`");
    // Ten hours east, it's the next day.
    t.activity.utc_offset = 10 * 3600;
    CHECK_EQ(t.field(OverlayField::date), "16 Jun 2025");
    CHECK_EQ(t.field(OverlayField::time_of_day), "02:11");
}

// Without a utc_offset, the system's time zone has it.
void test_time_of_day_without_offset()
{
    auto const *old = std::getenv("TZ");
    std::string saved { (old != nullptr) ? old : "" };
    ::setenv("TZ", "America/Toronto", 1);
    ::tzset();
    NewFieldsActivity t;
    t.activity.utc_offset.reset();
    CHECK_EQ(t.field(OverlayField::time_of_day), "12:11");
    if (old != nullptr) {
        ::setenv("TZ", saved.c_str(), 1);
    } else {
        ::unsetenv("TZ");
    }
    ::tzset();
}

// Between records: the clock runs, the lap doesn't glide.
void test_new_fields_between_records()
{
    NewFieldsActivity t;
    t.activity.laps.push_back(Lap { .first_record = 0, .end_record = 1 });
    t.activity.laps.push_back(Lap { .first_record = 1, .end_record = 3 });
    OverlaySource const source { t.activity };
    auto const          values = values_at_time(source, 3909.6);
    CHECK_EQ(format_value(values, OverlayField::lap, FieldUnit { }, ".0f", "--"), "1");
    CHECK_EQ(format_value(values, OverlayField::time_of_day, FieldUnit { }, "hms", "--"), "12:11:49");
    CHECK_EQ(format_value(values, OverlayField::title, FieldUnit { }, "", "--"), "Rückweg");
    // Long after the last record, the clock still runs.
    CHECK_EQ(format_value(values_at_time(source, 3909 + 600), OverlayField::time_of_day, FieldUnit { }, "hms", "--"), "12:21:49");
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
    test_running_cadence_in_rpm();
    test_elapsed();
    test_no_records();
    test_template_braces();
    test_specs_that_throw();
    test_values_at_time();
    test_values_without_records();
    test_values_at_record();
    test_pace();
    test_altitude_and_temperature();
    test_balance();
    test_laps();
    test_title();
    test_date_and_time_of_day();
    test_time_of_day_without_offset();
    test_new_fields_between_records();
    return ST::Test::test_result();
}
