/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The weather box (specs/59-weather.md): the weather at the start and the
 * end of the activity on display, getting it from Open-Meteo the first
 * time. Its text is plain functions, so that tests check it without fonts.
 */

#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <InfoBox.h>

#include <storage/Weather.h>

namespace ST {

constexpr char const WeatherAttribution[] = "Weather data by Open-Meteo.com";

// The WMO weather code in words: "Clear", "Light rain", "Thunderstorm",
// ... or "Unknown weather" for a code that isn't WMO's.
std::string weather_condition(int code);

// Where the wind comes from, as one of 16 compass points: "N", "NNE", ...
std::string compass_point(float degrees);

// What the box knows. It can show the forecast's weather while it asks
// the archive again, and keep it when that fails.
struct WeatherState {
    std::optional<NoWeather>       none { };
    std::optional<ActivityWeather> weather { };
    bool                           getting { false };
    std::string                    error { };      // Why the last request failed
    std::optional<i32>             utc_offset { }; // For the times shown
};

std::vector<InfoRow> weather_rows(WeatherState const &state);

struct WeatherBox : public InfoBox {
    // The activity on display, as the box needs it.
    struct Shown {
        std::expected<WeatherPoints, NoWeather> points { std::unexpected(NoWeather::NoPosition) };
        std::optional<ActivityWeather>          saved { }; // In its sidecar
        std::optional<i32>                      utc_offset { };
        std::string                             leg { }; // The leg's name, on a leg's tab
    };

    // The activity on display, or nothing without one.
    using Describe = std::function<std::optional<Shown>()>;
    // Starts getting the weather for the points: weather_arrived() gets it.
    using Fetch = std::function<void(WeatherPoints const &)>;

    Describe     describe_fnc;
    Fetch        fetch_fnc;
    Shown        shown { };
    WeatherState state { };

    WeatherBox(Describe describe, Fetch fetch);

    void initialize() override;
    // Shows the saved weather of the activity on display, and asks for it
    // if there's none, or it's the forecast's, or it's another start's.
    void refresh();
    void show();
    // The answer to a request for `points`, or why there's none. Ignored,
    // returning false, if the box shows other points by now.
    bool weather_arrived(WeatherPoints const &points, std::expected<ActivityWeather, std::string> const &result);

    [[nodiscard]] std::string          box_title() const override;
    [[nodiscard]] std::vector<InfoRow> box_rows() const override;
    [[nodiscard]] std::string          hint_text(double now) const override;
};

}
