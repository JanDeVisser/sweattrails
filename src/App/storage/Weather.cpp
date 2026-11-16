/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <ranges>

#include <Logging.h>
#include <StringUtil.h>

#include <storage/Weather.h>

namespace ST {

namespace {

constexpr char const WeatherKey[] = "weather";

constexpr char const ArchiveUrl[] = "https://archive-api.open-meteo.com/v1/archive";
constexpr char const ForecastUrl[] = "https://api.open-meteo.com/v1/forecast";

// The sidecar's six decimals are about 10 cm; this is about 10 m.
constexpr float SamePlace = 1e-4f;

// Open-Meteo's names for what Weather has.
constexpr char const Temperature[] = "temperature_2m";
constexpr char const ApparentTemperature[] = "apparent_temperature";
constexpr char const WeatherCode[] = "weather_code";
constexpr char const Humidity[] = "relative_humidity_2m";
constexpr char const Precipitation[] = "precipitation";
constexpr char const CloudCover[] = "cloud_cover";
constexpr char const WindSpeed[] = "wind_speed_10m";
constexpr char const WindDirection[] = "wind_direction_10m";
constexpr char const WindGusts[] = "wind_gusts_10m";

bool is_indoor(FIT::sub_sport sub_sport)
{
    using enum FIT::sub_sport;
    switch (sub_sport) {
    case treadmill:
    case spin:
    case indoor_cycling:
    case indoor_rowing:
    case elliptical:
    case stair_climbing:
    case indoor_skiing:
    case indoor_walking:
    case indoor_running:
    case virtual_activity:
    case indoor_climbing:
    case indoor_wheelchair_walk:
    case indoor_wheelchair_run:
    case indoor_hand_cycling:
        return true;
    default:
        return false;
    }
}

std::optional<WeatherPoint> point_of(record const &rec)
{
    if (!rec.position || !rec.position->has_fix()) {
        return { };
    }
    return WeatherPoint { .time = static_cast<i64>(rec.timestamp.timestamp), .position = *rec.position };
}

// The UTC date of a Unix time, as Open-Meteo takes it: "2019-08-18".
std::string utc_date(i64 time)
{
    auto const day = std::chrono::floor<std::chrono::days>(std::chrono::sys_seconds { std::chrono::seconds { time } });
    return std::format("{:%F}", std::chrono::year_month_day { day });
}

std::string joined(std::span<WeatherPoint const> points, auto const &part)
{
    return join(points | std::views::transform([&part](WeatherPoint const &point) { return std::format("{:.6f}", part(point)); }) | std::ranges::to<std::vector<std::string>>(), ",");
}

std::optional<float> number(JSONValue const &value)
{
    float ret { 0 };
    if (value.is_null() || !value.convert(ret)) {
        return { };
    }
    return ret;
}

// The value of Open-Meteo's `name` at hour `ix`, or nothing for an empty
// one.
std::optional<float> hourly_value(JSONValue const &hourly, std::string_view name, size_t ix)
{
    auto const values = hourly.get(name).and_then([](JSONValue const &values) { return values.to_array(); });
    if (!values || ix >= values->size()) {
        return { };
    }
    return number((*values)[ix]);
}

std::optional<Weather> weather_at(JSONValue const &location, WeatherPoint const &point, WeatherSource source)
{
    auto const hourly = location.get("hourly");
    if (!hourly) {
        return { };
    }
    auto const times = hourly->try_get_array<i64>("time");
    if (!times) {
        return { };
    }
    auto const hour = nearest_hour(point.time);
    auto const found = std::ranges::find(*times, hour);
    if (found == times->end()) {
        return { };
    }
    auto const ix = static_cast<size_t>(found - times->begin());
    auto const temperature = hourly_value(*hourly, Temperature, ix);
    auto const code = hourly_value(*hourly, WeatherCode, ix);
    if (!temperature || !code) {
        return { };
    }
    return Weather {
        .at = point,
        .hour = hour,
        .source = source,
        .temperature = *temperature,
        .apparent_temperature = hourly_value(*hourly, ApparentTemperature, ix).value_or(*temperature),
        .code = static_cast<int>(*code),
        .humidity = hourly_value(*hourly, Humidity, ix),
        .precipitation = hourly_value(*hourly, Precipitation, ix),
        .cloud_cover = hourly_value(*hourly, CloudCover, ix),
        .wind_speed = hourly_value(*hourly, WindSpeed, ix),
        .wind_direction = hourly_value(*hourly, WindDirection, ix),
        .wind_gusts = hourly_value(*hourly, WindGusts, ix),
    };
}

// Asks `source` for the points, and why not as the box says it.
std::expected<std::vector<std::optional<Weather>>, LibCError> ask(HttpFetch const &fetch, WeatherSource source, std::span<WeatherPoint const> points)
{
    auto const response = fetch(open_meteo_request(source, points));
    if (!response) {
        return std::unexpected(LibCError { std::format("Couldn't reach Open-Meteo: {}", response.error().description) });
    }
    if (response->status == 429) {
        return std::unexpected(LibCError { std::string { "Open-Meteo's limit for today is reached" } });
    }
    if (response->status != 200) {
        auto const reason = JSONValue::deserialize(response->body)
                                .transform([](JSONValue const &json) { return json.try_get<std::string>("reason").value_or(""); })
                                .value_or("");
        return std::unexpected(LibCError { reason.empty()
                ? std::format("Open-Meteo answered {}", response->status)
                : std::format("Open-Meteo answered {}: {}", response->status, reason) });
    }
    return parse_open_meteo(response->body, points, source);
}

void set_optional(JSONValue &json, std::string_view key, std::optional<float> value)
{
    if (value) {
        json.set(key, JSONValue { static_cast<double>(*value) });
    }
}

std::optional<float> get_optional(JSONValue const &json, std::string_view key)
{
    return json.get(key).and_then(number);
}

JSONValue encode_point(WeatherPoint const &point)
{
    auto json = JSONValue::object();
    json.set("time", JSONValue { point.time });
    json.set("lat", JSONValue { static_cast<double>(point.position.lat) });
    json.set("lon", JSONValue { static_cast<double>(point.position.lon) });
    return json;
}

Decoded<WeatherPoint> decode_point(JSONValue const &json)
{
    return WeatherPoint {
        .time = TRY_EVAL(json.try_get<i64>("time")),
        .position = {
            .lat = TRY_EVAL(json.try_get<float>("lat")),
            .lon = TRY_EVAL(json.try_get<float>("lon")),
        },
    };
}

}

bool WeatherPoint::same_as(WeatherPoint const &other) const
{
    return time == other.time
        && std::abs(position.lat - other.position.lat) < SamePlace
        && std::abs(position.lon - other.position.lon) < SamePlace;
}

std::expected<WeatherPoints, NoWeather> weather_points(Activity const &activity)
{
    if (activity.manufacturer == FIT::manufacturer::zwift || (activity.sub_sport && is_indoor(*activity.sub_sport))) {
        return std::unexpected(NoWeather::Indoor);
    }
    std::optional<WeatherPoint> start;
    for (auto const &rec : activity.records) {
        if (start = point_of(rec); start) {
            break;
        }
    }
    if (!start) {
        return std::unexpected(NoWeather::NoPosition);
    }
    std::optional<WeatherPoint> end;
    for (auto const &rec : activity.records | std::views::reverse) {
        if (end = point_of(rec); end) {
            break;
        }
    }
    return WeatherPoints { .start = *start, .end = *end };
}

char const *WeatherSource_name(WeatherSource source)
{
    switch (source) {
#undef S
#define S(N)               \
    case WeatherSource::N: \
        return #N;
        WEATHERSOURCES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

std::optional<WeatherSource> WeatherSource_from(std::string_view name)
{
#undef S
#define S(N)                     \
    if (name == #N) {            \
        return WeatherSource::N; \
    }
    WEATHERSOURCES(S)
#undef S
    return { };
}

Decoded<Weather> Weather::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Weather"));
    }
    auto const source = WeatherSource_from(TRY_EVAL(json.try_get<std::string>("source")));
    if (!source) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, "Weather: unknown source" });
    }
    return Weather {
        .at = TRY_EVAL(decode_point(json.get("at").value_or(JSONValue { }))),
        .hour = TRY_EVAL(json.try_get<i64>("hour")),
        .source = *source,
        .temperature = TRY_EVAL(json.try_get<float>("temperature")),
        .apparent_temperature = TRY_EVAL(json.try_get<float>("apparent_temperature")),
        .code = TRY_EVAL(json.try_get<int>("code")),
        .humidity = get_optional(json, "humidity"),
        .precipitation = get_optional(json, "precipitation"),
        .cloud_cover = get_optional(json, "cloud_cover"),
        .wind_speed = get_optional(json, "wind_speed"),
        .wind_direction = get_optional(json, "wind_direction"),
        .wind_gusts = get_optional(json, "wind_gusts"),
    };
}

JSONValue Weather::encode() const
{
    auto json = JSONValue::object();
    json.set("at", encode_point(at));
    json.set("hour", JSONValue { hour });
    json.set("source", JSONValue { std::string { WeatherSource_name(source) } });
    json.set("temperature", JSONValue { static_cast<double>(temperature) });
    json.set("apparent_temperature", JSONValue { static_cast<double>(apparent_temperature) });
    json.set("code", JSONValue { static_cast<i64>(code) });
    set_optional(json, "humidity", humidity);
    set_optional(json, "precipitation", precipitation);
    set_optional(json, "cloud_cover", cloud_cover);
    set_optional(json, "wind_speed", wind_speed);
    set_optional(json, "wind_direction", wind_direction);
    set_optional(json, "wind_gusts", wind_gusts);
    return json;
}

bool ActivityWeather::from_forecast() const
{
    return start.source == WeatherSource::forecast || end.source == WeatherSource::forecast;
}

i64 nearest_hour(i64 time)
{
    return (time + 1800) / 3600 * 3600;
}

HttpRequest open_meteo_request(WeatherSource source, std::span<WeatherPoint const> points)
{
    auto const hours = points | std::views::transform([](WeatherPoint const &point) { return nearest_hour(point.time); });
    auto const first = std::ranges::min(hours);
    auto const last = std::ranges::max(hours);
    auto const variables = std::vector<std::string> { Temperature, ApparentTemperature, WeatherCode, Humidity, Precipitation, CloudCover, WindSpeed, WindDirection, WindGusts };
    auto const query = form_encode({
        { "latitude", joined(points, [](WeatherPoint const &point) { return point.position.lat; }) },
        { "longitude", joined(points, [](WeatherPoint const &point) { return point.position.lon; }) },
        { "start_date", utc_date(first) },
        { "end_date", utc_date(last) },
        { "hourly", join(variables, ",") },
        { "timezone", "GMT" },
        { "timeformat", "unixtime" },
    });
    auto const url = (source == WeatherSource::archive) ? ArchiveUrl : ForecastUrl;
    return HttpRequest { .url = std::format("{}?{}", url, query) };
}

std::expected<std::vector<std::optional<Weather>>, LibCError> parse_open_meteo(std::string_view body, std::span<WeatherPoint const> points, WeatherSource source)
{
    auto const json = JSONValue::deserialize(body);
    if (!json) {
        return std::unexpected(LibCError { std::string { "Open-Meteo's answer isn't JSON" } });
    }
    if (json->is_object() && json->try_get<bool>("error").value_or(false)) {
        return std::unexpected(LibCError { std::format("Open-Meteo said: {}", json->try_get<std::string>("reason").value_or("an error")) });
    }
    // One place is an object, several are a list of them, in order.
    auto const locations = json->is_object() ? JSONValue::Array { *json } : json->to_array().value_or(JSONValue::Array { });
    if (locations.size() != points.size()) {
        return std::unexpected(LibCError { std::format("Open-Meteo answered for {} places, not {}", locations.size(), points.size()) });
    }
    std::vector<std::optional<Weather>> ret;
    for (auto const &[location, point] : std::views::zip(locations, points)) {
        ret.push_back(weather_at(location, point, source));
    }
    return ret;
}

std::expected<ActivityWeather, LibCError> fetch_weather(HttpFetch const &fetch, WeatherPoints const &points)
{
    std::vector<WeatherPoint> const both { points.start, points.end };
    auto                            weather = TRY_EVAL(ask(fetch, WeatherSource::archive, both));
    std::vector<WeatherPoint>       missing;
    for (auto const &[found, point] : std::views::zip(weather, both)) {
        if (!found) {
            missing.push_back(point);
        }
    }
    if (!missing.empty()) {
        auto const forecast = TRY_EVAL(ask(fetch, WeatherSource::forecast, missing));
        auto       next = forecast.begin();
        for (auto &found : weather) {
            if (!found) {
                found = *next++;
            }
        }
    }
    if (!weather[0] || !weather[1]) {
        return std::unexpected(LibCError { std::string { "Open-Meteo has no weather for then" } });
    }
    return ActivityWeather { .start = std::move(*weather[0]), .end = std::move(*weather[1]) };
}

std::optional<ActivityWeather> sidecar_weather(fs::path const &sidecar, std::optional<size_t> leg)
{
    auto const json = read_sidecar(sidecar).and_then([leg](JSONValue const &json) { return sidecar_part(json, leg); });
    auto const weather = json.and_then([](JSONValue const &part) { return part.get(WeatherKey); });
    if (!weather) {
        return { };
    }
    auto const start = weather->get("start").transform(Weather::decode);
    auto const end = weather->get("end").transform(Weather::decode);
    if (!start || !*start || !end || !*end) {
        warning(Storage, "Ignoring `{}`'s weather, which isn't what SweatTrails saves", sidecar.string());
        return { };
    }
    return ActivityWeather { .start = **start, .end = **end };
}

std::expected<void, LibCError> save_sidecar_weather(fs::path const &sidecar, ActivityWeather const &weather, std::optional<size_t> leg)
{
    auto json = JSONValue::object();
    json.set("start", weather.start.encode());
    json.set("end", weather.end.encode());
    return update_sidecar(sidecar, [&json, leg](JSONValue &sidecar_json) {
        change_sidecar_part(sidecar_json, leg, [&json](JSONValue &part) { part.set(WeatherKey, json); });
    });
}

}
