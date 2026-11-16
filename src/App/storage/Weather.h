/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The weather an activity was done in, at its start and at its end, from
 * Open-Meteo's historical weather API, and kept in its sidecar. See
 * specs/59-weather.md.
 */

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Error.h>
#include <JSON.h>

#include <Coordinates.h>
#include <Http.h>
#include <storage/Activity.h>

namespace ST {

namespace fs = std::filesystem;

// Where and when the weather is for: the first or the last record with a
// position.
struct WeatherPoint {
    i64         time { 0 }; // Unix time
    Coordinates position { };

    // The sidecar keeps the position to six decimals, so a saved point is
    // the same as this one to within that.
    [[nodiscard]] bool same_as(WeatherPoint const &other) const;
    bool               operator==(WeatherPoint const &) const = default;
};

struct WeatherPoints {
    WeatherPoint start { };
    WeatherPoint end { };

    bool operator==(WeatherPoints const &) const = default;
};

enum class NoWeather {
    NoPosition, // On the trainer or the treadmill, or without GPS
    Indoor,     // Zwift, or a FIT file that says it's indoors or virtual
};

// The activity's start and end, or why it has no weather.
std::expected<WeatherPoints, NoWeather> weather_points(Activity const &activity);

#define WEATHERSOURCES(S) \
    S(archive)            \
    S(forecast)

enum class WeatherSource {
#undef S
#define S(N) N,
    WEATHERSOURCES(S)
#undef S
};

char const                  *WeatherSource_name(WeatherSource source);
std::optional<WeatherSource> WeatherSource_from(std::string_view name);

// Open-Meteo's hour nearest a point, in its units: °C, km/h, degrees, %,
// mm and a WMO weather code. Open-Meteo can leave any of the optional ones
// empty.
struct Weather {
    WeatherPoint         at { };
    i64                  hour { 0 }; // Unix time
    WeatherSource        source { WeatherSource::archive };
    float                temperature { 0 };
    float                apparent_temperature { 0 };
    int                  code { 0 };
    std::optional<float> humidity { };
    std::optional<float> precipitation { };
    std::optional<float> cloud_cover { };
    std::optional<float> wind_speed { };
    std::optional<float> wind_direction { }; // Where it comes from
    std::optional<float> wind_gusts { };

    static Decoded<Weather> decode(JSONValue const &json);
    [[nodiscard]] JSONValue encode() const;
    bool                    operator==(Weather const &) const = default;
};

struct ActivityWeather {
    Weather start { };
    Weather end { };

    [[nodiscard]] WeatherPoints points() const { return { .start = start.at, .end = end.at }; }
    // The forecast's at the start or the end, which the archive is asked
    // for again.
    [[nodiscard]] bool from_forecast() const;
    bool               operator==(ActivityWeather const &) const = default;
};

// The hour nearest `time`, a Unix time; half past goes to the next one.
i64 nearest_hour(i64 time);

// The request for the hours nearest `points`, from the archive or the
// forecast, all of them at once.
HttpRequest open_meteo_request(WeatherSource source, std::span<WeatherPoint const> points);

// Open-Meteo's answer to open_meteo_request(): the nearest hour's weather
// for each point, or nothing where its temperature or code is empty, as
// the archive's are for the last few days.
std::expected<std::vector<std::optional<Weather>>, LibCError> parse_open_meteo(std::string_view body, std::span<WeatherPoint const> points, WeatherSource source);

// The weather at the start and the end, from the archive, or the forecast
// where the archive doesn't have it yet. The error is what the box says
// after "Couldn't get the weather: ".
std::expected<ActivityWeather, LibCError> fetch_weather(HttpFetch const &fetch, WeatherPoints const &points);

// The weather box's request to Open-Meteo: the points, and where the
// answer is saved. An empty sidecar is an activity not from the store.
struct WeatherRequest {
    WeatherPoints         points { };
    fs::path              sidecar { };
    std::optional<size_t> leg { };

    bool operator==(WeatherRequest const &) const = default;
};

// The request to start once `answered` is done: the one asked for while it
// ran, unless that's the same request, whose answer is saved now. The box
// closed and opened again on the activity before the answer came.
std::optional<WeatherRequest> next_weather_request(std::optional<WeatherRequest> waiting, WeatherRequest const &answered);

// The weather in the sidecar, under `"weather"`, or leg `leg`'s.
std::optional<ActivityWeather> sidecar_weather(fs::path const &sidecar, std::optional<size_t> leg = { });

// Saves the weather in the sidecar, with update_sidecar(), or leg `leg`'s.
std::expected<void, LibCError> save_sidecar_weather(fs::path const &sidecar, ActivityWeather const &weather, std::optional<size_t> leg = { });

}
