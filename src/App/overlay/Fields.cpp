/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>

#include <overlay/Fields.h>

namespace ST {

namespace {

constexpr double MetersPerMile = 1609.344;
constexpr double FeetPerMeter = 1.0 / 0.3048;
constexpr double SecondsPerDay = 24 * 3600;
// Slower than this, pace is absurd: 60:00 min/km and up.
constexpr double SlowestPace = 1.0 / 3.6;

constexpr FieldUnit KilometersPerHour { .name = "km/h", .factor = 3.6 };
constexpr FieldUnit MilesPerHour { .name = "mph", .factor = 3600.0 / MetersPerMile };
constexpr FieldUnit MetersPerSecond { .name = "m/s", .factor = 1.0 };
constexpr FieldUnit Kilometers { .name = "km", .factor = 0.001 };
constexpr FieldUnit Miles { .name = "mi", .factor = 1.0 / MetersPerMile };
constexpr FieldUnit Meters { .name = "m", .factor = 1.0 };
constexpr FieldUnit Feet { .name = "ft", .factor = FeetPerMeter };
constexpr FieldUnit Watts { .name = "W" };
constexpr FieldUnit BeatsPerMinute { .name = "bpm" };
constexpr FieldUnit RevolutionsPerMinute { .name = "rpm" };
// FIT records the strides of one foot, which runners count as steps.
constexpr FieldUnit StepsPerMinute { .name = "spm", .factor = 2.0 };
// Pace is in s/m: the seconds a kilometer or a mile takes.
constexpr FieldUnit MinutesPerKilometer { .name = "min/km", .factor = 1000.0 };
constexpr FieldUnit MinutesPerMile { .name = "min/mi", .factor = MetersPerMile };
constexpr FieldUnit Percent { .name = "%" };
constexpr FieldUnit Celsius { .name = "°C" };
constexpr FieldUnit Fahrenheit { .name = "°F", .factor = 1.8, .offset = 32.0 };
constexpr FieldUnit NoUnit { };

// The specs of the kinds that don't take std::format's.
constexpr std::array DurationSpecs { std::string_view { "hms" }, std::string_view { "ms" } };
constexpr std::array PaceSpecs { std::string_view { "ms" } };
constexpr std::array ClockSpecs { std::string_view { "hm" }, std::string_view { "hms" } };

long whole_seconds(double seconds)
{
    return static_cast<long>(std::max(0.0, std::round(seconds)));
}

// `hms`, 1:05:09, or `ms`, 65:09.
std::string duration(double seconds, std::string_view spec)
{
    auto const total = whole_seconds(seconds);
    if (spec == "ms") {
        return std::format("{}:{:02}", total / 60, total % 60);
    }
    return std::format("{}:{:02}:{:02}", total / 3600, (total / 60) % 60, total % 60);
}

// `hm`, 09:44, or `hms`, 09:44:10. A clock doesn't round up: at 09:44:59.6
// it still says 09:44.
std::string clock(double seconds_since_midnight, std::string_view spec)
{
    auto const total = static_cast<long>(std::floor(std::max(0.0, seconds_since_midnight)));
    if (spec == "hms") {
        return std::format("{:02}:{:02}:{:02}", total / 3600, (total / 60) % 60, total % 60);
    }
    return std::format("{:02}:{:02}", total / 3600, (total / 60) % 60);
}

std::chrono::sys_seconds date_of(double local_seconds)
{
    return std::chrono::sys_seconds { std::chrono::seconds { std::llround(local_seconds) } };
}

std::string number(double value, std::string_view spec)
{
    return std::vformat(std::format("{{:{}}}", spec), std::make_format_args(value));
}

// FIT's left/right balance: bit 0x80 says the percentage in bits 0x7f is
// the right leg's. Without it, the side isn't known; 0xff is no value.
std::optional<double> right_share(u8 raw)
{
    if (raw == 0xff || (raw & 0x80) == 0 || (raw & 0x7f) > 100) {
        return { };
    }
    return static_cast<double>(raw & 0x7f);
}

std::optional<double> pace_of(std::optional<double> speed)
{
    if (!speed || *speed < SlowestPace) {
        return { };
    }
    return 1.0 / *speed;
}

// The lap the record is in, from 1. Without laps, the activity is one.
double lap_of(Activity const &activity, size_t record)
{
    for (auto ix = 0uz; ix < activity.laps.size(); ++ix) {
        auto const &lap = activity.laps[ix];
        if (record >= lap.first_record && record < lap.end_record) {
            return static_cast<double>(ix + 1);
        }
    }
    return 1.0;
}

double start_of(Activity const &activity)
{
    return static_cast<double>(activity.segment.start_time.timestamp);
}

double time_of_day(OverlaySource const &source, double timestamp)
{
    auto const local = std::fmod(timestamp + source.utc_offset, SecondsPerDay);
    return (local < 0) ? local + SecondsPerDay : local;
}

// The values during a pause, or with the signal lost, from the last
// record's: not moving, and not measuring. Distance, altitude, grade and
// lap stay as they were, and the clocks are worked out from the time.
void paused(FieldValues &values, OverlaySource const &source)
{
    auto const set = [&values](OverlayField field, std::optional<double> value) {
        values.values[static_cast<size_t>(field)] = value;
    };
    // No power meter, no 0 W: but one that dropped out just before the
    // pause is 0 W in it.
    for (auto const field : { OverlayField::speed, OverlayField::power, OverlayField::cadence }) {
        set(field, source.has(field) ? std::optional { 0.0 } : std::nullopt);
    }
    for (auto const field : { OverlayField::heart_rate, OverlayField::temperature, OverlayField::lr_balance }) {
        set(field, std::nullopt);
    }
}

}

std::string_view OverlayField_name(OverlayField field)
{
    switch (field) {
#undef S
#define S(F)              \
    case OverlayField::F: \
        return #F;
        OVERLAYFIELDS(S)
#undef S
    }
    return "";
}

std::optional<OverlayField> overlay_field(std::string_view name)
{
#undef S
#define S(F)        \
    if (name == #F) \
        return OverlayField::F;
    OVERLAYFIELDS(S)
#undef S
    return { };
}

FieldKind field_kind(OverlayField field)
{
    switch (field) {
    case OverlayField::elapsed:
    case OverlayField::pace:
        return FieldKind::Duration;
    case OverlayField::time_of_day:
        return FieldKind::Clock;
    case OverlayField::date:
        return FieldKind::Date;
    case OverlayField::lr_balance:
        return FieldKind::Balance;
    case OverlayField::title:
        return FieldKind::Text;
    default:
        return FieldKind::Number;
    }
}

FieldUnit default_unit(OverlayField field, Units units, sport sport)
{
    auto const metric = units == Units::Metric;
    switch (field) {
    case OverlayField::speed:
        return metric ? KilometersPerHour : MilesPerHour;
    case OverlayField::distance:
        return metric ? Kilometers : Miles;
    case OverlayField::power:
        return Watts;
    case OverlayField::heart_rate:
        return BeatsPerMinute;
    case OverlayField::cadence:
        return (sport == sport::Running) ? StepsPerMinute : RevolutionsPerMinute;
    case OverlayField::pace:
        return metric ? MinutesPerKilometer : MinutesPerMile;
    case OverlayField::altitude:
        return metric ? Meters : Feet;
    case OverlayField::grade:
        return Percent;
    case OverlayField::temperature:
        return metric ? Celsius : Fahrenheit;
    default:
        return NoUnit;
    }
}

std::optional<FieldUnit> field_unit(OverlayField field, std::string_view name)
{
    auto const find = [name](std::initializer_list<FieldUnit> units) -> std::optional<FieldUnit> {
        for (auto const &unit : units) {
            if (unit.name == name) {
                return unit;
            }
        }
        return { };
    };
    switch (field) {
    case OverlayField::speed:
        return find({ KilometersPerHour, MilesPerHour, MetersPerSecond });
    case OverlayField::distance:
        return find({ Kilometers, Miles, Meters });
    case OverlayField::power:
        return find({ Watts });
    case OverlayField::heart_rate:
        return find({ BeatsPerMinute });
    case OverlayField::cadence:
        return find({ RevolutionsPerMinute, StepsPerMinute });
    case OverlayField::pace:
        return find({ MinutesPerKilometer, MinutesPerMile });
    case OverlayField::altitude:
        return find({ Meters, Feet });
    case OverlayField::grade:
        return find({ Percent });
    case OverlayField::temperature:
        return find({ Celsius, Fahrenheit });
    default:
        return find({ NoUnit });
    }
}

std::string_view default_spec(OverlayField field)
{
    switch (field) {
    case OverlayField::speed:
    case OverlayField::distance:
        return ".1f";
    case OverlayField::elapsed:
        return "hms";
    case OverlayField::pace:
        return "ms";
    case OverlayField::time_of_day:
        return "hm";
    case OverlayField::date:
        return "%d %b %Y";
    case OverlayField::title:
        return "";
    default:
        return ".0f";
    }
}

// std::vformat says a spec is wrong only by throwing.
bool is_valid_spec(std::string_view spec)
{
    try {
        auto const value = 1.0;
        (void) std::vformat(std::format("{{:{}}}", spec), std::make_format_args(value));
        return true;
    } catch (std::format_error const &) {
        return false;
    }
}

bool is_valid_spec(OverlayField field, std::string_view spec)
{
    switch (field_kind(field)) {
    case FieldKind::Number:
    case FieldKind::Balance:
        return is_valid_spec(spec);
    case FieldKind::Duration:
        return (field == OverlayField::pace) ? std::ranges::contains(PaceSpecs, spec) : std::ranges::contains(DurationSpecs, spec);
    case FieldKind::Clock:
        return std::ranges::contains(ClockSpecs, spec);
    case FieldKind::Date:
        try {
            auto const day = date_of(0);
            (void) std::vformat(std::format("{{:{}}}", spec), std::make_format_args(day));
            return true;
        } catch (std::format_error const &) {
            return false;
        }
    case FieldKind::Text:
        return spec.empty();
    }
    return false;
}

i32 local_utc_offset(Activity const &activity)
{
    if (activity.utc_offset) {
        return *activity.utc_offset;
    }
    auto const start = static_cast<std::time_t>(activity.segment.start_time.timestamp);
    std::tm    local { };
    localtime_r(&start, &local);
    return static_cast<i32>(local.tm_gmtoff);
}

OverlaySource::OverlaySource(Activity const &activity)
    : activity(&activity)
    , utc_offset(local_utc_offset(activity))
{
    // An altitude that changed while standing still, such as a barometer
    // drifting during a café stop, is no climb: it comes off everything
    // after the pause, so that the grade across it is the riding's.
    double        step { 0 };
    record const *previous { nullptr };
    for (auto const &r : activity.records) {
        // A device that goes back in distance is ignored there: the
        // profile has to be in order to be searched.
        if (!r.distance || !r.altitude || (!profile.empty() && *r.distance < profile.back().distance)) {
            continue;
        }
        if (previous != nullptr) {
            auto const &p = *previous;
            auto const  time = static_cast<double>(r.timestamp.timestamp) - static_cast<double>(p.timestamp.timestamp);
            auto const  moved = static_cast<double>(*r.distance - *p.distance);
            // Standing: less than 5 s at 1 km/h, however long the pause.
            if (time > MaxGap && moved < MaxGap * SlowestPace) {
                step += static_cast<double>(*r.altitude - *p.altitude);
            }
        }
        profile.push_back(ProfilePoint { .distance = *r.distance, .altitude = *r.altitude - step });
        previous = &r;
    }
    for (auto ix = 0uz; ix < activity.records.size(); ++ix) {
        for (auto f = 0uz; f < OverlayFieldCount; ++f) {
            recorded[f] = recorded[f] || field_value(*this, ix, static_cast<OverlayField>(f)).has_value();
        }
    }
}

namespace {

// The altitude at `distance`, between the profile's points around it. In a
// pause, the points have the same distance: the first of them.
double altitude_at(std::vector<OverlaySource::ProfilePoint> const &profile, double distance)
{
    auto const after = std::ranges::lower_bound(profile, distance, { }, &OverlaySource::ProfilePoint::distance);
    if (after == profile.begin()) {
        return after->altitude;
    }
    auto const &later = *after;
    auto const &earlier = *(after - 1);
    auto const  span = later.distance - earlier.distance;
    return (span > 0) ? earlier.altitude + (distance - earlier.distance) / span * (later.altitude - earlier.altitude) : later.altitude;
}

}

std::optional<double> grade_at(OverlaySource const &source, double distance, double window)
{
    auto const &profile = source.profile;
    auto const  from = distance - window / 2;
    auto const  to = distance + window / 2;
    if (profile.empty() || window <= 0 || from < profile.front().distance || to > profile.back().distance) {
        return { };
    }
    return (altitude_at(profile, to) - altitude_at(profile, from)) / window * 100.0;
}

std::optional<double> field_value(OverlaySource const &source, size_t record, OverlayField field)
{
    auto const &activity = *source.activity;
    if (record >= activity.records.size()) {
        return { };
    }
    auto const &r = activity.records[record];
    auto const  as_double = [](auto const &value) -> std::optional<double> {
        if (!value) {
            return { };
        }
        return static_cast<double>(*value);
    };
    auto const timestamp = static_cast<double>(r.timestamp.timestamp);
    switch (field) {
    case OverlayField::speed:
        return as_double(r.speed);
    case OverlayField::power:
        return as_double(r.power);
    case OverlayField::heart_rate:
        return as_double(r.heart_rate);
    case OverlayField::cadence:
        return as_double(r.cadence);
    case OverlayField::distance:
        return as_double(r.distance);
    case OverlayField::elapsed:
        return timestamp - start_of(activity);
    case OverlayField::pace:
        return pace_of(as_double(r.speed));
    case OverlayField::altitude:
        return as_double(r.altitude);
    case OverlayField::grade:
        return { };
    case OverlayField::temperature:
        return as_double(r.temperature);
    case OverlayField::lr_balance:
        return r.left_right_balance ? right_share(*r.left_right_balance) : std::nullopt;
    case OverlayField::time_of_day:
        return time_of_day(source, timestamp);
    case OverlayField::lap:
        return lap_of(activity, record);
    case OverlayField::title:
        return { };
    case OverlayField::date:
        return start_of(activity) + source.utc_offset;
    }
    return { };
}

FieldValues values_at_record(OverlaySource const &source, size_t record)
{
    FieldValues ret { .sport = source.activity->sport, .title = source.activity->title, .source = &source };
    for (auto ix = 0uz; ix < OverlayFieldCount; ++ix) {
        ret.values[ix] = field_value(source, record, static_cast<OverlayField>(ix));
    }
    ret.values[static_cast<size_t>(OverlayField::grade)] = ret.grade(DefaultGradeWindow);
    return ret;
}

FieldValues values_at_time(OverlaySource const &source, double seconds)
{
    auto const &activity = *source.activity;
    auto const &records = activity.records;
    auto const  at = start_of(activity) + seconds;
    auto const  time_of = [](record const &r) { return static_cast<double>(r.timestamp.timestamp); };
    // The first record after the time; the one before it is the earlier.
    auto const after = std::ranges::upper_bound(records, at, { }, time_of);
    auto const later = static_cast<size_t>(after - records.begin());

    FieldValues ret { .sport = activity.sport, .title = activity.title, .source = &source };
    if (!records.empty()) {
        auto const earlier = (later == 0) ? 0 : later - 1;
        ret = values_at_record(source, earlier);
        if (later > 0 && later < records.size()) {
            auto const from = time_of(records[earlier]);
            auto const span = time_of(records[later]) - from;
            if (span > MaxGap && at > from) {
                paused(ret, source);
            } else if (span > 0 && span <= MaxGap) {
                auto const next = values_at_record(source, later);
                auto const fraction = (at - from) / span;
                for (auto ix = 0uz; ix < OverlayFieldCount; ++ix) {
                    // The lap changes at a record, rather than gliding there.
                    if (static_cast<OverlayField>(ix) != OverlayField::lap && ret.values[ix] && next.values[ix]) {
                        ret.values[ix] = *ret.values[ix] + fraction * (*next.values[ix] - *ret.values[ix]);
                    }
                }
            }
        }
    }
    // At the distance it is now: a pause holds it, and so the grade.
    ret.values[static_cast<size_t>(OverlayField::grade)] = ret.grade(DefaultGradeWindow);
    // From the speed it is now, not interpolated itself: pace is its inverse.
    ret.values[static_cast<size_t>(OverlayField::pace)] = pace_of(ret[OverlayField::speed]);
    // The clocks run on, also without records.
    ret.values[static_cast<size_t>(OverlayField::elapsed)] = seconds;
    ret.values[static_cast<size_t>(OverlayField::time_of_day)] = time_of_day(source, at);
    ret.values[static_cast<size_t>(OverlayField::date)] = start_of(activity) + source.utc_offset;
    return ret;
}

std::string format_field(OverlayField field, std::optional<double> value, FieldUnit const &unit, std::string_view spec, std::string_view missing)
{
    if (!value) {
        return std::string { missing };
    }
    switch (field_kind(field)) {
    case FieldKind::Number:
        return number(unit.shown(*value), spec);
    case FieldKind::Duration:
        return duration(unit.shown(*value), spec);
    case FieldKind::Clock:
        return clock(*value, spec);
    case FieldKind::Date: {
        auto const day = date_of(*value);
        return std::vformat(std::format("{{:{}}}", spec), std::make_format_args(day));
    }
    case FieldKind::Balance:
        return std::format("{}/{}", number(100.0 - *value, spec), number(*value, spec));
    case FieldKind::Text:
        return std::string { missing };
    }
    return std::string { missing };
}

std::string format_value(FieldValues const &values, OverlayField field, FieldUnit const &unit, std::string_view spec, std::string_view missing)
{
    if (field == OverlayField::title) {
        return values.title.empty() ? std::string { missing } : values.title;
    }
    return format_field(field, values[field], unit, spec, missing);
}

}
