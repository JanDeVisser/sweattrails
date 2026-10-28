/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay's fields, their units and formats, and the text element's
 * templates.
 */

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <exception>
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
    // Over the gap, a pause: not moving.
    CHECK_EQ(value_of(values_at_time(OverlaySource { t.activity }, 5), OverlayField::speed), "0");
    CHECK_EQ(value_of(values_at_time(OverlaySource { t.activity }, 9.9), OverlayField::power), "0");
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

// During a pause, as the spec's table has it.
void test_pause()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = Start;
    activity.utc_offset = 0;
    activity.title = "Stop";
    activity.records.push_back(record {
        .timestamp = DateTime { .timestamp = Start },
        .altitude = 500.0f,
        .heart_rate = 150,
        .cadence = 90,
        .distance = 1000.0f,
        .speed = 8.0f,
        .temperature = 18,
        .left_right_balance = 0x80 | 50,
    });
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 60 }, .distance = 1000.0f, .speed = 8.0f });
    OverlaySource const source { activity };
    auto const          values = values_at_time(source, 30);
    // Not moving: no power meter is still no power.
    CHECK_EQ(value_of(values, OverlayField::speed), "0");
    CHECK_EQ(value_of(values, OverlayField::cadence), "0");
    CHECK_EQ(value_of(values, OverlayField::power), "none");
    CHECK_EQ(value_of(values, OverlayField::pace), "none");
    // Not measuring.
    CHECK_EQ(value_of(values, OverlayField::heart_rate), "none");
    CHECK_EQ(value_of(values, OverlayField::temperature), "none");
    CHECK_EQ(value_of(values, OverlayField::lr_balance), "none");
    // Held.
    CHECK_EQ(value_of(values, OverlayField::distance), "1000");
    CHECK_EQ(value_of(values, OverlayField::altitude), "500");
    CHECK_EQ(value_of(values, OverlayField::lap), "1");
    CHECK_EQ(values.title, "Stop");
    // Running on.
    CHECK_EQ(value_of(values, OverlayField::elapsed), "30");
    CHECK_EQ(value_of(values, OverlayField::time_of_day), std::format("{}", (Start + 30) % 86400));
    // At the record before the pause, its own values.
    CHECK_EQ(value_of(values_at_time(source, 0), OverlayField::heart_rate), "150");
}

// A record every 5 m, at 5 m/s: flat to 100 m, a ramp of 20% to 140 m,
// flat again, to 300 m.
struct Climb {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    Climb()
    {
        activity.segment.start_time.timestamp = Start;
        for (auto d = 0u; d <= 300; d += 5) {
            auto const altitude = (d <= 100) ? 0.0 : (d <= 140) ? (d - 100) * 0.2
                                                                : 8.0;
            activity.records.push_back(record {
                .timestamp = DateTime { .timestamp = Start + d / 5 },
                .altitude = static_cast<f32>(altitude),
                .distance = static_cast<f32>(d),
            });
        }
    }
};

std::string grade_of(std::optional<double> grade)
{
    return grade ? std::format("{:.1f}", *grade) : std::string { "none" };
}

void test_grade()
{
    Climb               c;
    OverlaySource const source { c.activity };
    CHECK_EQ(grade_of(grade_at(source, 50, 30)), "0.0");
    // On the ramp, 30 m keeps its 20%; 100 m flattens it.
    CHECK_EQ(grade_of(grade_at(source, 120, 30)), "20.0");
    CHECK_EQ(grade_of(grade_at(source, 120, 100)), "8.0");
    // Half on the ramp.
    CHECK_EQ(grade_of(grade_at(source, 140, 30)), "10.0");
    // Within half the window of the ends.
    CHECK_EQ(grade_of(grade_at(source, 10, 30)), "none");
    CHECK_EQ(grade_of(grade_at(source, 290, 30)), "none");
    CHECK_EQ(grade_of(grade_at(source, 15, 30)), "0.0");
    // The default window, at a record and between them.
    CHECK_EQ(grade_of(values_at_record(source, 24)[OverlayField::grade]), "20.0");
    CHECK_EQ(grade_of(values_at_time(source, 24.5)[OverlayField::grade]), "20.0");
    CHECK_EQ(grade_of(values_at_time(source, 24.5).grade(100)), "8.0");
}

// A pause on the ramp: the window reaches past it, and during the pause
// the grade is held.
void test_grade_through_a_pause()
{
    Climb c;
    auto &records = c.activity.records;
    // From 120 m on, everything a minute later.
    for (auto &r : records) {
        if (*r.distance > 120) {
            r.timestamp.timestamp += 60;
        }
    }
    OverlaySource const source { c.activity };
    // 120 m is at 24 s; the next record, at 125 m, at 85 s.
    CHECK_EQ(grade_of(values_at_time(source, 24)[OverlayField::grade]), "20.0");
    CHECK_EQ(grade_of(values_at_time(source, 50)[OverlayField::grade]), "20.0");
    CHECK_EQ(grade_of(values_at_time(source, 85)[OverlayField::grade]), "20.0");
}

void test_grade_without_altitude()
{
    Climb c;
    for (auto &r : c.activity.records) {
        r.altitude.reset();
    }
    OverlaySource const source { c.activity };
    CHECK(source.profile.empty());
    CHECK_EQ(grade_of(grade_at(source, 120, 30)), "none");
    CHECK_EQ(grade_of(values_at_record(source, 24)[OverlayField::grade]), "none");
    CHECK_EQ(grade_of(values_at_record(source, 24).grade(50)), "none");
}

// A pause at the exact record times: the record's own values at either
// end, the pause table only strictly between them. Distance is held at the
// earlier record's, also when it grew across the gap (a tunnel).
void test_pause_at_record_times()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = Start;
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start }, .heart_rate = 150, .distance = 1000.0f, .speed = 8.0f, .power = 200 });
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 6 }, .heart_rate = 160, .distance = 1500.0f, .speed = 9.0f, .power = 210 });
    OverlaySource const source { activity };
    auto const          at_start = values_at_time(source, 0);
    CHECK_EQ(value_of(at_start, OverlayField::power), "200");
    CHECK_EQ(value_of(at_start, OverlayField::heart_rate), "150");
    auto const just_after = values_at_time(source, 0.01);
    CHECK_EQ(value_of(just_after, OverlayField::power), "0");
    CHECK_EQ(value_of(just_after, OverlayField::heart_rate), "none");
    CHECK_EQ(value_of(values_at_time(source, 5.99), OverlayField::distance), "1000");
    auto const at_end = values_at_time(source, 6);
    CHECK_EQ(value_of(at_end, OverlayField::power), "210");
    CHECK_EQ(value_of(at_end, OverlayField::heart_rate), "160");
    CHECK_EQ(value_of(at_end, OverlayField::distance), "1500");
    // Exactly MaxGap apart is no pause: interpolated.
    activity.records[1].timestamp.timestamp = Start + 5;
    OverlaySource const five { activity };
    CHECK_EQ(value_of(values_at_time(five, 2.5), OverlayField::power), "205");
}

// The clock isn't interpolated across midnight, where it goes back to 0;
// the date stays the start's.
void test_time_of_day_across_midnight()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.utc_offset = 0;
    // 1'750'032'000 is 2025-06-16 00:00:00 UTC.
    constexpr u32 Midnight = 1'750'032'000;
    activity.segment.start_time.timestamp = Midnight - 10;
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Midnight - 1 }, .speed = 5.0f });
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Midnight + 1 }, .speed = 5.0f });
    OverlaySource const source { activity };
    auto const          clock = [&source](double seconds) {
        return format_value(values_at_time(source, seconds), OverlayField::time_of_day, FieldUnit { }, "hms", "--");
    };
    auto const date = [&source](double seconds) {
        return format_value(values_at_time(source, seconds), OverlayField::date, FieldUnit { }, "%d %b %Y", "--");
    };
    CHECK_EQ(clock(9.5), "23:59:59");
    CHECK_EQ(clock(10.0), "00:00:00");
    CHECK_EQ(clock(10.5), "00:00:00");
    CHECK_EQ(date(10.5), "15 Jun 2025");
    CHECK_EQ(format_value(values_at_record(source, 1), OverlayField::time_of_day, FieldUnit { }, "hms", "--"), "00:00:01");
}

// Pace is the inverse of the interpolated speed, not interpolated itself:
// 2 and 4 m/s halfway is 3 m/s, 5:33 min/km, not the average 6:15.
void test_pace_between_records()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.sport = sport::Running;
    activity.segment.start_time.timestamp = Start;
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start }, .speed = 2.0f });
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 1 }, .speed = 4.0f });
    OverlaySource const source { activity };
    auto const          values = values_at_time(source, 0.5);
    CHECK_EQ(format_value(values, OverlayField::pace, default_unit(OverlayField::pace, Units::Metric, sport::Running), "ms", "--"), "5:33");
    // 3 m/s is 8:56 min/mi.
    CHECK_EQ(format_value(values, OverlayField::pace, default_unit(OverlayField::pace, Units::Imperial, sport::Running), "ms", "--"), "8:56");
    CHECK_EQ(format_value(values, OverlayField::pace, *field_unit(OverlayField::pace, "min/mi"), "ms", "--"), "8:56");
}

// The lap changes at the record that starts it, not before.
void test_lap_at_its_change()
{
    NewFieldsActivity t;
    t.activity.laps.push_back(Lap { .first_record = 0, .end_record = 2 });
    t.activity.laps.push_back(Lap { .first_record = 2, .end_record = 3 });
    OverlaySource const source { t.activity };
    auto const          lap = [&source](double seconds) {
        return format_value(values_at_time(source, seconds), OverlayField::lap, FieldUnit { }, ".0f", "--");
    };
    CHECK_EQ(lap(3910.0), "1");
    CHECK_EQ(lap(3910.99), "1");
    CHECK_EQ(lap(3911.0), "2");
    CHECK_EQ(lap(5000.0), "2");
}

// A percentage over 100 isn't a balance.
void test_balance_out_of_range()
{
    NewFieldsActivity t;
    t.activity.records[0].left_right_balance = 0x80 | 101;
    CHECK_EQ(t.field(OverlayField::lr_balance), "--");
    t.activity.records[0].left_right_balance = 0x80 | 100;
    CHECK_EQ(t.field(OverlayField::lr_balance), "0/100");
}

// Every date spec that is_valid_spec() lets through formats any date
// without throwing: a layout can't take the app down while drawing.
void test_date_specs_format()
{
    auto const day = static_cast<double>(Start) + Toronto;
    for (auto const spec : { "%d %b %Y", "%A", "%a %e", "%F", "%D", "%x", "%j", "%U", "%W", "%V", "%G", "%g", "%u", "%w",
             "%C", "%y", "%h", "%B", "%m", "%H:%M", "%T", "%R", "%p", "%I", "%r", "%c", "%X", "%Z", "%z", "%Ez", "%Oz",
             "%Ey", "%EY", "%Od", "%Om", "%n", "%t", "%%", "", ">20%F", "L%A", "%S", "%M" }) {
        if (!is_valid_spec(OverlayField::date, spec)) {
            continue;
        }
        try {
            (void) format_field(OverlayField::date, day, FieldUnit { }, spec, "--");
            (void) format_field(OverlayField::date, -1.0e6, FieldUnit { }, spec, "--");
        } catch (std::exception const &) {
            CHECK_EQ(std::string { spec }, "a spec that throws");
        }
    }
    // Duration-like specs aren't dates.
    CHECK(!is_valid_spec(OverlayField::date, "%Q"));
    CHECK(!is_valid_spec(OverlayField::date, "hms"));
    CHECK(!is_valid_spec(OverlayField::date, "%"));
}

// The specs each kind takes, and refuses.
void test_specs_per_kind()
{
    CHECK(is_valid_spec(OverlayField::elapsed, "hms"));
    CHECK(is_valid_spec(OverlayField::elapsed, "ms"));
    CHECK(!is_valid_spec(OverlayField::elapsed, ".0f"));
    CHECK(!is_valid_spec(OverlayField::elapsed, ""));
    CHECK(is_valid_spec(OverlayField::pace, "ms"));
    CHECK(!is_valid_spec(OverlayField::pace, "hms"));
    CHECK(!is_valid_spec(OverlayField::pace, ".1f"));
    CHECK(is_valid_spec(OverlayField::time_of_day, "hm"));
    CHECK(is_valid_spec(OverlayField::time_of_day, "hms"));
    CHECK(!is_valid_spec(OverlayField::time_of_day, "%H:%M"));
    CHECK(is_valid_spec(OverlayField::lr_balance, ".1f"));
    CHECK(!is_valid_spec(OverlayField::lr_balance, "hms"));
    CHECK(is_valid_spec(OverlayField::title, ""));
    CHECK(!is_valid_spec(OverlayField::title, "s"));
    CHECK(is_valid_spec(OverlayField::lap, ".0f"));
    CHECK(is_valid_spec(OverlayField::altitude, ".0f"));
    CHECK(!is_valid_spec(OverlayField::altitude, "%d"));
}

// Flat road, a record every 5 m, 1 s apart: a profile to vary.
Activity flat_road(Storage &storage, int to)
{
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = Start;
    for (auto d = 0; d <= to; d += 5) {
        activity.records.push_back(record {
            .timestamp = DateTime { .timestamp = Start + static_cast<u32>(d / 5) },
            .altitude = 100.0f,
            .distance = static_cast<f32>(d),
        });
    }
    return activity;
}

// Records standing still at one distance, and a device going back in
// distance, don't upset the grade around them.
void test_grade_with_duplicate_and_backward_distances()
{
    Climb c;
    auto &records = c.activity.records;
    // Standing still at 120 m on the ramp, five records at the same place.
    auto const at = std::ranges::find_if(records, [](record const &r) { return *r.distance == 120.0f; });
    auto const still = *at;
    records.insert(at + 1, 5, still);
    OverlaySource const standing { c.activity };
    CHECK_EQ(grade_of(grade_at(standing, 120, 30)), "20.0");
    CHECK_EQ(grade_of(grade_at(standing, 50, 30)), "0.0");
    // A record going back to 118 m after 125 m: left out, not searched.
    Climb      back;
    auto const after = std::ranges::find_if(back.activity.records, [](record const &r) { return *r.distance == 125.0f; });
    back.activity.records.insert(after + 1, record { .timestamp = after->timestamp, .altitude = 3.6f, .distance = 118.0f });
    OverlaySource const backwards { back.activity };
    CHECK(std::ranges::is_sorted(backwards.profile, { }, &OverlaySource::ProfilePoint::distance));
    CHECK_EQ(grade_of(grade_at(backwards, 120, 30)), "20.0");
}

// Records without distance or altitude here and there are skipped.
void test_grade_with_gaps_in_the_records()
{
    Climb c;
    for (auto ix = 0uz; ix < c.activity.records.size(); ix += 3) {
        if (*c.activity.records[ix].distance > 100 && *c.activity.records[ix].distance < 140) {
            ((ix % 2) ? c.activity.records[ix].altitude : c.activity.records[ix].distance).reset();
        }
    }
    OverlaySource const source { c.activity };
    CHECK_EQ(grade_of(grade_at(source, 120, 30)), "20.0");
    // A profile of one point has no grade.
    Storage  storage { };
    Activity one = flat_road(storage, 0);
    CHECK_EQ(grade_of(grade_at(OverlaySource { one }, 0, 10)), "none");
}

// The window's ends at the profile's: exactly at the end is still there.
void test_grade_at_the_ends()
{
    Storage             storage { };
    Activity            road = flat_road(storage, 300);
    OverlaySource const source { road };
    CHECK_EQ(grade_of(grade_at(source, 285, 30)), "0.0");
    CHECK_EQ(grade_of(grade_at(source, 285.1, 30)), "none");
    CHECK_EQ(grade_of(grade_at(source, 14.9, 30)), "none");
    CHECK_EQ(grade_of(grade_at(source, 100, 200)), "0.0");
    CHECK_EQ(grade_of(grade_at(source, 99, 200)), "none");
    // Past the last record, the frame's distance is the last one: no grade.
    CHECK_EQ(grade_of(values_at_time(source, 1000)[OverlayField::grade]), "none");
}

// A pause on a flat road, after which the altimeter reads 4 m higher, as a
// barometer does when the weather changes during a long stop. The spec
// says a window with a pause in it uses the riding on either side only, as
// if the pause weren't there: the road is flat on either side, so the
// grade is 0, not 4 m over 30 m.
void test_grade_with_an_altitude_step_in_a_pause()
{
    Storage  storage { };
    Activity road = flat_road(storage, 300);
    for (auto &r : road.records) {
        if (*r.distance > 150) {
            r.timestamp.timestamp += 600;
            *r.altitude += 4.0f;
        }
    }
    // Standing at 150 m, the first record after the pause is still there.
    road.records.insert(road.records.begin() + 31,
        record { .timestamp = DateTime { .timestamp = Start + 30 + 600 }, .altitude = 104.0f, .distance = 150.0f });
    OverlaySource const source { road };
    CHECK_EQ(grade_of(grade_at(source, 150, 30)), "0.0");
    CHECK_EQ(grade_of(grade_at(source, 140, 30)), "0.0");
    CHECK_EQ(grade_of(values_at_time(source, 300)[OverlayField::grade]), "0.0");
}

// A power meter that drops out just before a pause: 0 W in it, since the
// activity has power.
void test_pause_after_a_dropout()
{
    Storage  storage { };
    Activity activity { storage, ActivityID { } };
    activity.segment.start_time.timestamp = Start;
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start }, .cadence = 90, .speed = 8.0f, .power = 200 });
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 1 }, .speed = 8.0f });
    activity.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 60 }, .speed = 8.0f, .power = 210 });
    OverlaySource const source { activity };
    CHECK(source.has(OverlayField::power));
    CHECK(!source.has(OverlayField::heart_rate));
    auto const values = values_at_time(source, 30);
    CHECK_EQ(value_of(values, OverlayField::power), "0");
    CHECK_EQ(value_of(values, OverlayField::cadence), "0");
    CHECK_EQ(value_of(values, OverlayField::heart_rate), "none");
}

// The title is the one the source was made with.
void test_title_when_made()
{
    NewFieldsActivity   t;
    OverlaySource const source { t.activity };
    t.activity.title = "Renamed";
    CHECK_EQ(values_at_time(source, 3909).title, "Rückweg");
    CHECK_EQ(values_at_record(OverlaySource { t.activity }, 0).title, "Renamed");
}

// The date has no zone: specs that show one are refused.
void test_date_without_zone()
{
    CHECK(!is_valid_spec(OverlayField::date, "%d %b %Y %Z"));
    CHECK(!is_valid_spec(OverlayField::date, "%z"));
    CHECK(!is_valid_spec(OverlayField::date, "%Ez"));
    CHECK(is_valid_spec(OverlayField::date, "100%% %d"));
    CHECK(is_valid_spec(OverlayField::date, "%%Z"));
    CHECK(is_valid_spec(OverlayField::date, "%d %b"));
}

}

std::string range_of(OverlaySource const &source, OverlayField field)
{
    auto const range = source.range(field);
    return range ? std::format("{:.1f} {:.1f}", range->min, range->max) : std::string { "none" };
}

// The lowest and highest value over the records, for "auto": of the fields
// any record has, the grade included, and none for the others.
void test_ranges()
{
    TestActivity        t;
    OverlaySource const source { t.activity };
    CHECK_EQ(range_of(source, OverlayField::speed), "0.0 10.0");
    CHECK_EQ(range_of(source, OverlayField::power), "251.0 251.0");
    CHECK_EQ(range_of(source, OverlayField::distance), "42195.0 42205.0");
    CHECK_EQ(range_of(source, OverlayField::temperature), "none");
    CHECK(!source.has(OverlayField::temperature));
    CHECK(source.has(OverlayField::power));

    Climb               c;
    OverlaySource const climb { c.activity };
    CHECK_EQ(range_of(climb, OverlayField::altitude), "0.0 8.0");
    CHECK_EQ(range_of(climb, OverlayField::grade), "0.0 20.0");
    CHECK_EQ(range_of(climb, OverlayField::power), "none");
}

std::string average_of(OverlaySource const &source, double from, double to)
{
    auto const average = average_altitude(source, from, to);
    return average ? std::format("{:.2f}", *average) : std::string { "none" };
}

// The average altitude over a stretch, weighted by distance, for the
// elevation profile.
void test_average_altitude()
{
    Climb               c;
    OverlaySource const climb { c.activity };
    CHECK_EQ(average_of(climb, 0, 100), "0.00");
    CHECK_EQ(average_of(climb, 100, 140), "4.00");
    // Between records: 18 m of the ramp, at 6.2 m on average, and 18 m
    // flat at 8 m.
    CHECK_EQ(average_of(climb, 122, 158), "7.10");
    // Past the ends, clamped to them; and a stretch of nothing.
    CHECK_EQ(average_of(climb, -50, 50), "0.00");
    CHECK_EQ(average_of(climb, 250, 400), "8.00");
    CHECK_EQ(average_of(climb, 130, 130), "6.00");

    // Records 1 m apart at 0 m, then one 90 m later at 9 m: by record,
    // the average would be 0.75 m.
    Storage  storage { };
    Activity uneven { storage, ActivityID { } };
    uneven.segment.start_time.timestamp = Start;
    for (auto d = 0u; d <= 10; ++d) {
        uneven.records.push_back(record { .timestamp = DateTime { .timestamp = Start + d }, .altitude = 0.0f, .distance = static_cast<f32>(d) });
    }
    uneven.records.push_back(record { .timestamp = DateTime { .timestamp = Start + 14 }, .altitude = 9.0f, .distance = 100.0f });
    CHECK_EQ(average_of(OverlaySource { uneven }, 0, 100), "4.05");

    // Without altitudes, nothing.
    for (auto &r : c.activity.records) {
        r.altitude.reset();
    }
    CHECK_EQ(average_of(OverlaySource { c.activity }, 0, 100), "none");
}

// The altimeter's drift during a stop is no hill.
void test_average_altitude_across_a_step()
{
    Storage  storage { };
    Activity road = flat_road(storage, 300);
    for (auto &r : road.records) {
        if (*r.distance > 150) {
            r.timestamp.timestamp += 600;
            *r.altitude += 4.0f;
        }
    }
    road.records.insert(road.records.begin() + 31,
        record { .timestamp = DateTime { .timestamp = Start + 30 + 600 }, .altitude = 104.0f, .distance = 150.0f });
    OverlaySource const source { road };
    CHECK_EQ(average_of(source, 100, 200), "100.00");
}

// Records a second apart going east, but for one without a position, one
// at 0° (no fix), and a minute's gap after the fifth second.
struct Ride {
    Storage  storage { };
    Activity activity { storage, ActivityID { } };

    Ride()
    {
        activity.segment.start_time.timestamp = Start;
        for (auto i = 0u; i < 8; ++i) {
            auto const t = (i <= 5) ? i : i + 60;
            record     r { .timestamp = DateTime { .timestamp = Start + t } };
            if (i == 2) {
                r.position = Coordinates { .lat = 0.0f, .lon = 0.0f };
            } else if (i != 3) {
                r.position = Coordinates { .lat = 52.0f, .lon = 4.0f + 0.001f * static_cast<float>(i) };
            }
            activity.records.push_back(r);
        }
    }
};

std::string position_of(OverlaySource const &source, double seconds)
{
    auto const p = position_at(source, seconds);
    if (!p) {
        return "none";
    }
    // In thousandths of a degree east of 4°.
    auto const east = (p->x - world_pixel(Coordinates { .lat = 52.0f, .lon = 4.0f }, 0).x) / (world_pixel(Coordinates { .lat = 52.0f, .lon = 4.001f }, 0).x - world_pixel(Coordinates { .lat = 52.0f, .lon = 4.0f }, 0).x);
    return std::format("{:.2f}", east);
}

void test_track()
{
    Ride                r;
    OverlaySource const source { r.activity };
    // No position, and 0°, are left out.
    CHECK_EQ(source.track.size(), 6uz);
    CHECK_EQ(source.track[2].seconds, 4.0);
    CHECK_EQ(position_of(source, 0), "0.00");
    CHECK_EQ(position_of(source, 0.5), "0.50");
    // Across the record without a position: 1 s to 4 s, 3 s apart.
    CHECK_EQ(position_of(source, 2.5), "2.50");
    // Held across the gap, from 5 s to 66 s.
    CHECK_EQ(position_of(source, 30), "5.00");
    CHECK_EQ(position_of(source, 66.5), "6.50");
    // Before the start and after the end.
    CHECK_EQ(position_of(source, -10), "0.00");
    CHECK_EQ(position_of(source, 100), "7.00");

    TestActivity        indoors;
    OverlaySource const without { indoors.activity };
    CHECK(without.track.empty());
    CHECK_EQ(position_of(without, 0), "none");
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
    test_pause();
    test_grade();
    test_grade_through_a_pause();
    test_grade_without_altitude();
    test_pause_at_record_times();
    test_time_of_day_across_midnight();
    test_pace_between_records();
    test_lap_at_its_change();
    test_balance_out_of_range();
    test_date_specs_format();
    test_specs_per_kind();
    test_grade_with_duplicate_and_backward_distances();
    test_grade_with_gaps_in_the_records();
    test_grade_at_the_ends();
    test_grade_with_an_altitude_step_in_a_pause();
    test_pause_after_a_dropout();
    test_title_when_made();
    test_date_without_zone();
    test_ranges();
    test_average_altitude();
    test_average_altitude_across_a_step();
    test_track();
    return ST::Test::test_result();
}
