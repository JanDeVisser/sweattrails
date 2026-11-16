/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <cmath>
#include <ctime>
#include <format>
#include <utility>

#include <StringUtil.h>

#include <widget/Weather.h>

namespace ST {

namespace {

constexpr std::array<char const *, 16> CompassPoints {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
};

bool is_snow(int code)
{
    return (code >= 71 && code <= 77) || code == 85 || code == 86;
}

// A whole number, without "-0".
int whole(float value)
{
    return static_cast<int>(std::lround(value));
}

// "8:02", in the activity's local time, or the computer's if the file
// doesn't say.
std::string local_time(i64 time, std::optional<i32> utc_offset)
{
    auto const tm = utc_offset ? utc_tm(time + *utc_offset) : local_tm(time);
    return std::format("{}:{:02}", tm.tm_hour, tm.tm_min);
}

std::string temperatures(Weather const &weather)
{
    return std::format("{}°C, feels like {}°C", whole(weather.temperature), whole(weather.apparent_temperature));
}

std::string wind(Weather const &weather)
{
    if (!weather.wind_speed) {
        return { };
    }
    auto const speed = whole(*weather.wind_speed);
    if (speed == 0) {
        return "No wind";
    }
    auto ret = std::format("{} km/h", speed);
    if (weather.wind_direction) {
        ret += std::format(" from the {}", compass_point(*weather.wind_direction));
    }
    if (weather.wind_gusts) {
        ret += std::format(", gusts {} km/h", whole(*weather.wind_gusts));
    }
    return ret;
}

// "64% humidity · no rain · 0% cloud", leaving out what's missing.
std::string details(Weather const &weather)
{
    std::vector<std::string> parts;
    if (weather.humidity) {
        parts.push_back(std::format("{}% humidity", whole(*weather.humidity)));
    }
    if (weather.precipitation) {
        auto const what = is_snow(weather.code) ? "snow" : "rain";
        parts.push_back((*weather.precipitation < 0.05f) ? std::format("no {}", what) : std::format("{:.1f} mm {}", *weather.precipitation, what));
    }
    if (weather.cloud_cover) {
        parts.push_back(std::format("{}% cloud", whole(*weather.cloud_cover)));
    }
    return join(parts, " · ");
}

void add_weather(std::vector<InfoRow> &rows, std::string_view name, Weather const &weather, std::optional<i32> utc_offset)
{
    using enum InfoRow::Kind;
    auto heading = std::format("{}, {}", name, local_time(weather.at.time, utc_offset));
    if (weather.source == WeatherSource::forecast) {
        heading += " · from the forecast";
    }
    rows.push_back({ .kind = Heading, .line = { .text = heading } });
    rows.push_back({ .kind = Entry, .line = { .text = weather_condition(weather.code) } });
    rows.push_back({ .kind = Entry, .line = { .text = temperatures(weather) } });
    if (auto const blowing = wind(weather); !blowing.empty()) {
        rows.push_back({ .kind = Entry, .line = { .text = blowing } });
    }
    if (auto const small = details(weather); !small.empty()) {
        rows.push_back({ .kind = Details, .line = { .small = small } });
    }
}

}

std::string weather_condition(int code)
{
    switch (code) {
    case 0:
        return "Clear";
    case 1:
        return "Mainly clear";
    case 2:
        return "Partly cloudy";
    case 3:
        return "Overcast";
    case 45:
        return "Fog";
    case 48:
        return "Freezing fog";
    case 51:
        return "Light drizzle";
    case 53:
        return "Drizzle";
    case 55:
        return "Heavy drizzle";
    case 56:
        return "Light freezing drizzle";
    case 57:
        return "Freezing drizzle";
    case 61:
        return "Light rain";
    case 63:
        return "Rain";
    case 65:
        return "Heavy rain";
    case 66:
        return "Light freezing rain";
    case 67:
        return "Freezing rain";
    case 71:
        return "Light snow";
    case 73:
        return "Snow";
    case 75:
        return "Heavy snow";
    case 77:
        return "Snow grains";
    case 80:
        return "Light showers";
    case 81:
        return "Showers";
    case 82:
        return "Heavy showers";
    case 85:
        return "Light snow showers";
    case 86:
        return "Heavy snow showers";
    case 95:
        return "Thunderstorm";
    case 96:
        return "Thunderstorm with hail";
    case 99:
        return "Thunderstorm with heavy hail";
    default:
        return "Unknown weather";
    }
}

std::string compass_point(float degrees)
{
    auto const turned = std::fmod(std::fmod(degrees, 360.0f) + 360.0f, 360.0f);
    auto const ix = static_cast<size_t>(std::floor((turned + 11.25f) / 22.5f)) % CompassPoints.size();
    return CompassPoints[ix];
}

std::vector<InfoRow> weather_rows(WeatherState const &state)
{
    using enum InfoRow::Kind;
    std::vector<InfoRow> ret;
    if (state.none) {
        auto const why = (*state.none == NoWeather::Indoor) ? "No weather: this is an indoor activity." : "No weather: this activity has no position.";
        ret.push_back({ .kind = Entry, .line = { .text = why } });
        return ret;
    }
    if (!state.weather) {
        auto const text = state.getting ? std::string { "Getting the weather from Open-Meteo..." } : std::format("Couldn't get the weather: {}", state.error);
        ret.push_back({ .kind = Entry, .line = { .text = text } });
        return ret;
    }
    add_weather(ret, "Start", state.weather->start, state.utc_offset);
    add_weather(ret, "End", state.weather->end, state.utc_offset);
    // The forecast's, while the archive is asked again, or after it failed.
    if (state.getting) {
        ret.push_back({ .kind = Details, .line = { .small = "Asking Open-Meteo's archive again..." } });
    } else if (!state.error.empty()) {
        ret.push_back({ .kind = Details, .line = { .small = std::format("Couldn't ask the archive again: {}", state.error) } });
    }
    return ret;
}

WeatherBox::WeatherBox(Describe describe, Fetch fetch)
    : InfoBox("Weather")
    , describe_fnc(std::move(describe))
    , fetch_fnc(std::move(fetch))
{
}

void WeatherBox::initialize()
{
    refresh();
    resize();
}

void WeatherBox::refresh()
{
    auto described = describe_fnc();
    if (!described) {
        return;
    }
    shown = std::move(*described);
    state = WeatherState { .utc_offset = shown.utc_offset };
    if (!shown.points) {
        state.none = shown.points.error();
        return;
    }
    auto const &points = *shown.points;
    if (shown.saved && shown.saved->start.at.same_as(points.start) && shown.saved->end.at.same_as(points.end)) {
        state.weather = shown.saved;
        if (!shown.saved->from_forecast()) {
            return;
        }
    }
    state.getting = true;
    fetch_fnc(points);
}

void WeatherBox::show()
{
    make_visible();
}

bool WeatherBox::weather_arrived(WeatherPoints const &points, std::expected<ActivityWeather, std::string> const &result)
{
    if (!shown.points || !shown.points->start.same_as(points.start) || !shown.points->end.same_as(points.end)) {
        return false;
    }
    state.getting = false;
    if (result) {
        state.weather = *result;
        state.error.clear();
    } else {
        state.error = result.error();
    }
    if (status == ModalStatus::Active) {
        resize();
    }
    return true;
}

std::string WeatherBox::box_title() const
{
    return shown.leg.empty() ? "Weather" : std::format("Weather · {}", shown.leg);
}

std::vector<InfoRow> WeatherBox::box_rows() const
{
    return weather_rows(state);
}

std::string WeatherBox::hint_text(double) const
{
    return state.weather ? std::format("{} · Esc closes", WeatherAttribution) : "Esc closes";
}

}
