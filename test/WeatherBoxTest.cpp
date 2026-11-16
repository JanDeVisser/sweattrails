/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the weather box (specs/59-weather.md) without a window or the
 * network: its words for the weather, its rows, and when it asks for the
 * weather.
 */

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <widget/Weather.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

constexpr i64 Midnight = 1566086400; // 2019-08-18 00:00 UTC
constexpr i32 Muskoka = -4 * 3600;   // EDT

WeatherPoints muskoka()
{
    return WeatherPoints {
        .start = { .time = Midnight + 12 * 3600 + 2 * 60, .position = { .lat = 45.33f, .lon = -79.22f } },
        .end = { .time = Midnight + 17 * 3600 + 41 * 60, .position = { .lat = 45.30f, .lon = -79.10f } },
    };
}

ActivityWeather muskoka_weather(WeatherSource end_source = WeatherSource::archive)
{
    auto const points = muskoka();
    return ActivityWeather {
        .start = {
            .at = points.start,
            .hour = Midnight + 12 * 3600,
            .temperature = 15.3f,
            .apparent_temperature = 13.4f,
            .code = 0,
            .humidity = 64.0f,
            .precipitation = 0.0f,
            .cloud_cover = 0.0f,
            .wind_speed = 11.0f,
            .wind_direction = 49.0f,
            .wind_gusts = 26.0f,
        },
        .end = {
            .at = points.end,
            .hour = Midnight + 18 * 3600,
            .source = end_source,
            .temperature = 21.0f,
            .apparent_temperature = 21.4f,
            .code = 61,
            .precipitation = 1.24f,
            .wind_speed = 0.3f,
        },
    };
}

void test_conditions()
{
    CHECK_EQ(weather_condition(0), "Clear");
    CHECK_EQ(weather_condition(2), "Partly cloudy");
    CHECK_EQ(weather_condition(45), "Fog");
    CHECK_EQ(weather_condition(53), "Drizzle");
    CHECK_EQ(weather_condition(61), "Light rain");
    CHECK_EQ(weather_condition(75), "Heavy snow");
    CHECK_EQ(weather_condition(82), "Heavy showers");
    CHECK_EQ(weather_condition(95), "Thunderstorm");
    CHECK_EQ(weather_condition(99), "Thunderstorm with heavy hail");
    CHECK_EQ(weather_condition(4), "Unknown weather");
}

void test_compass_points()
{
    CHECK_EQ(compass_point(0), "N");
    CHECK_EQ(compass_point(11.24f), "N");
    CHECK_EQ(compass_point(11.25f), "NNE");
    CHECK_EQ(compass_point(49), "NE");
    CHECK_EQ(compass_point(90), "E");
    CHECK_EQ(compass_point(200), "SSW");
    CHECK_EQ(compass_point(348.74f), "NNW");
    CHECK_EQ(compass_point(359), "N");
    CHECK_EQ(compass_point(360), "N");
    CHECK_EQ(compass_point(-90), "W");
}

void test_rows()
{
    using enum InfoRow::Kind;
    WeatherState const         state { .weather = muskoka_weather(WeatherSource::forecast), .utc_offset = Muskoka };
    std::vector<InfoRow> const expected {
        { .kind = Heading, .line = { .text = "Start, 8:02" } },
        { .kind = Entry, .line = { .text = "Clear" } },
        { .kind = Entry, .line = { .text = "15°C, feels like 13°C" } },
        { .kind = Entry, .line = { .text = "11 km/h from the NE, gusts 26 km/h" } },
        { .kind = Details, .line = { .small = "64% humidity · no rain · 0% cloud" } },
        { .kind = Heading, .line = { .text = "End, 13:41 · from the forecast" } },
        { .kind = Entry, .line = { .text = "Light rain" } },
        { .kind = Entry, .line = { .text = "21°C, feels like 21°C" } },
        { .kind = Entry, .line = { .text = "No wind" } },
        { .kind = Details, .line = { .small = "1.2 mm rain" } },
    };
    auto const rows = weather_rows(state);
    CHECK_EQ(rows.size(), expected.size());
    for (size_t ix = 0; ix < std::min(rows.size(), expected.size()); ++ix) {
        CHECK_EQ(rows[ix].line.text, expected[ix].line.text);
        CHECK_EQ(rows[ix].line.small, expected[ix].line.small);
        CHECK(rows[ix].kind == expected[ix].kind);
    }
}

void test_rows_without_weather()
{
    auto const only = [](WeatherState const &state) {
        auto const rows = weather_rows(state);
        return rows.size() == 1 ? rows[0].line.text : std::string { "more than one row" };
    };
    CHECK_EQ(only({ .none = NoWeather::NoPosition }), "No weather: this activity has no position.");
    CHECK_EQ(only({ .none = NoWeather::Indoor }), "No weather: this is an indoor activity.");
    CHECK_EQ(only({ .getting = true }), "Getting the weather from Open-Meteo...");
    CHECK_EQ(only({ .error = "Open-Meteo's limit for today is reached" }), "Couldn't get the weather: Open-Meteo's limit for today is reached");
}

void test_snow_and_a_missing_value()
{
    auto weather = muskoka_weather();
    weather.start.code = 73;
    weather.start.precipitation = 0.0f;
    weather.start.humidity.reset();
    weather.start.wind_direction.reset();
    weather.start.wind_gusts.reset();
    auto const rows = weather_rows({ .weather = weather, .utc_offset = Muskoka });
    CHECK_EQ(rows[1].line.text, "Snow");
    CHECK_EQ(rows[3].line.text, "11 km/h");
    CHECK_EQ(rows[4].line.small, "no snow · 0% cloud");
}

// The small text of the last row, and the text of the first.
std::string last_small(WeatherState const &state)
{
    auto const rows = weather_rows(state);
    return rows.empty() ? std::string { } : rows.back().line.small;
}

std::string first_text(WeatherState const &state)
{
    auto const rows = weather_rows(state);
    return rows.empty() ? std::string { } : rows.front().line.text;
}

// A box that keeps what it was asked to fetch. resize() measures text,
// which needs fonts.
struct TestBox : public WeatherBox {
    WeatherBox::Shown          on_display { };
    std::vector<WeatherPoints> fetched { };

    TestBox()
        : WeatherBox(nullptr, nullptr)
    {
        describe_fnc = [this]() -> std::optional<Shown> { return on_display; };
        fetch_fnc = [this](WeatherPoints const &points) { fetched.push_back(points); };
        on_display = Shown { .points = muskoka(), .utc_offset = Muskoka };
    }

    void resize() override
    {
    }

    void open()
    {
        refresh();
        make_visible();
    }
};

void test_no_position_asks_for_nothing()
{
    auto box = std::make_shared<TestBox>();
    box->on_display.points = std::unexpected(NoWeather::Indoor);
    box->open();
    CHECK(box->fetched.empty());
    CHECK(box->state.none == std::optional { NoWeather::Indoor });
    CHECK_EQ(box->hint_text(0), "Esc closes");
    box->hide();
}

void test_saved_weather_is_shown_without_asking()
{
    auto box = std::make_shared<TestBox>();
    box->on_display.saved = muskoka_weather();
    box->open();
    CHECK(box->fetched.empty());
    CHECK(box->state.weather == std::optional { muskoka_weather() });
    CHECK(!box->state.getting);
    CHECK_EQ(box->hint_text(0), "Weather data by Open-Meteo.com · Esc closes");
    box->hide();
}

void test_none_saved_asks()
{
    auto box = std::make_shared<TestBox>();
    box->open();
    CHECK_EQ(box->fetched.size(), 1uz);
    CHECK(box->state.getting);
    CHECK(box->weather_arrived(muskoka(), muskoka_weather()));
    CHECK(!box->state.getting);
    CHECK(box->state.weather == std::optional { muskoka_weather() });
    box->hide();
}

void test_the_forecasts_is_shown_and_asked_again()
{
    auto box = std::make_shared<TestBox>();
    box->on_display.saved = muskoka_weather(WeatherSource::forecast);
    box->open();
    CHECK_EQ(box->fetched.size(), 1uz);
    CHECK(box->state.getting);
    CHECK(box->state.weather == std::optional { muskoka_weather(WeatherSource::forecast) });
    CHECK_EQ(last_small(box->state), "Asking Open-Meteo's archive again...");
    // It fails: the forecast's stays.
    CHECK(box->weather_arrived(muskoka(), std::unexpected(std::string { "Couldn't reach Open-Meteo" })));
    CHECK(box->state.weather == std::optional { muskoka_weather(WeatherSource::forecast) });
    CHECK_EQ(last_small(box->state), "Couldn't ask the archive again: Couldn't reach Open-Meteo");
    box->hide();
}

// The saved weather is for another start: the gaps were filled, or
// another recording is the main one now.
void test_saved_for_other_points_asks()
{
    auto box = std::make_shared<TestBox>();
    auto saved = muskoka_weather();
    saved.start.at.time -= 60;
    box->on_display.saved = saved;
    box->open();
    CHECK_EQ(box->fetched.size(), 1uz);
    CHECK(!box->state.weather.has_value());
    box->hide();
}

void test_an_answer_for_other_points_is_ignored()
{
    auto box = std::make_shared<TestBox>();
    box->open();
    auto other = muskoka();
    other.end.time += 3600;
    CHECK(!box->weather_arrived(other, muskoka_weather()));
    CHECK(box->state.getting);
    CHECK(!box->state.weather.has_value());
    box->hide();
}

void test_an_error_is_shown()
{
    auto box = std::make_shared<TestBox>();
    box->open();
    CHECK(box->weather_arrived(muskoka(), std::unexpected(std::string { "Open-Meteo's limit for today is reached" })));
    CHECK(!box->state.getting);
    CHECK_EQ(first_text(box->state), "Couldn't get the weather: Open-Meteo's limit for today is reached");
    box->hide();
}

void test_title_and_esc()
{
    auto box = std::make_shared<TestBox>();
    box->on_display.leg = "Bike";
    box->open();
    CHECK_EQ(box->box_title(), "Weather · Bike");
    CHECK(!box->process_key(KModNone, KEY_TAB));
    CHECK(box->process_key(KModNone, KEY_ESCAPE));
    CHECK(App::the()->modals.empty());
}

}

int main()
{
    ScopedTestApp app;
    test_conditions();
    test_compass_points();
    test_rows();
    test_rows_without_weather();
    test_snow_and_a_missing_value();
    test_no_position_asks_for_nothing();
    test_saved_weather_is_shown_without_asking();
    test_none_saved_asks();
    test_the_forecasts_is_shown_and_asked_again();
    test_saved_for_other_points_asks();
    test_an_answer_for_other_points_is_ignored();
    test_an_error_is_shown();
    test_title_and_esc();
    return test_result();
}
