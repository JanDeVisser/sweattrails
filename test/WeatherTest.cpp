/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the weather at an activity's start and end (specs/59-weather.md):
 * which records it's for, the request to Open-Meteo and its answer, with
 * canned responses, and the sidecar.
 */

#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <JSON.h>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <storage/Weather.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

// Muskoka 70.3 2019's T1, near Huntsville: 2019-08-18 12:23 UTC.
constexpr i64         Midnight = 1566086400; // 2019-08-18 00:00 UTC
constexpr i64         T1 = Midnight + 12 * 3600 + 23 * 60;
constexpr Coordinates Huntsville { .lat = 45.33f, .lon = -79.22f };
constexpr Coordinates Finish { .lat = 45.30f, .lon = -79.10f };

record at(i64 time, std::optional<Coordinates> position)
{
    record ret { };
    ret.timestamp = DateTime::from_timestamp(static_cast<i32>(time));
    ret.position = position;
    return ret;
}

struct Made {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
};

WeatherPoints muskoka()
{
    return WeatherPoints {
        .start = { .time = T1, .position = Huntsville },
        .end = { .time = T1 + 5 * 3600 + 40 * 60, .position = Finish },
    };
}

// Open-Meteo's answer for one place over `hours` from `first`, with
// `temperature` rising a degree an hour, and null from `empty_from` on,
// as the archive's are for the last days.
std::string location(i64 first, int hours, float temperature, std::optional<int> empty_from = { })
{
    std::vector<std::string> times, temperatures, codes, gusts;
    for (int ix = 0; ix < hours; ++ix) {
        auto const empty = empty_from && ix >= *empty_from;
        times.push_back(std::to_string(first + ix * 3600));
        temperatures.push_back(empty ? "null" : std::format("{:.1f}", temperature + static_cast<float>(ix)));
        codes.push_back(empty ? "null" : (ix % 2 == 0 ? "0" : "61"));
        gusts.push_back((ix == 1) ? "null" : "26.0");
    }
    return std::format(R"({{"latitude":45.3,"longitude":-79.2,"hourly":{{"time":[{}],"temperature_2m":[{}],"apparent_temperature":[{}],)"
                       R"("weather_code":[{}],"relative_humidity_2m":[{}],"wind_speed_10m":[{}],"wind_direction_10m":[{}],"wind_gusts_10m":[{}]}}}})",
        join(times, ","), join(temperatures, ","), join(temperatures, ","), join(codes, ","),
        join(std::vector<std::string>(hours, "64"), ","), join(std::vector<std::string>(hours, "11.0"), ","),
        join(std::vector<std::string>(hours, "49"), ","), join(gusts, ","));
}

// Gives the canned responses in order, and keeps the requests. Nothing is
// no response at all.
struct FakeOpenMeteo {
    std::vector<std::optional<HttpResponse>> answers { };
    std::vector<HttpRequest>                 requests { };

    HttpFetch fetch()
    {
        return [this](HttpRequest const &request) -> std::expected<HttpResponse, LibCError> {
            requests.push_back(request);
            if (requests.size() > answers.size() || !answers[requests.size() - 1]) {
                return std::unexpected(LibCError { std::string { "Could not resolve host" } });
            }
            return *answers[requests.size() - 1];
        };
    }
};

void test_points_are_the_first_and_last_with_a_position()
{
    Made t;
    t.activity.records = {
        at(T1 - 20, std::nullopt),                             // Before the GPS has a fix
        at(T1 - 10, Coordinates { .lat = 0.0f, .lon = 0.0f }), // A fix of nothing
        at(T1, Huntsville),
        at(T1 + 60, Coordinates { .lat = 45.31f, .lon = -79.15f }),
        at(T1 + 120, Finish),
        at(T1 + 180, std::nullopt), // The GPS dropped out before the end
    };
    auto const points = weather_points(t.activity);
    CHECK(points.has_value());
    if (points) {
        CHECK(points->start == (WeatherPoint { .time = T1, .position = Huntsville }));
        CHECK(points->end == (WeatherPoint { .time = T1 + 120, .position = Finish }));
    }
}

void test_no_position_no_weather()
{
    Made t;
    t.activity.records = { at(T1, std::nullopt), at(T1 + 60, std::nullopt) };
    CHECK(weather_points(t.activity) == std::unexpected(NoWeather::NoPosition));
    t.activity.records.clear();
    CHECK(weather_points(t.activity) == std::unexpected(NoWeather::NoPosition));
}

void test_indoors_no_weather()
{
    for (auto const sub_sport : { FIT::sub_sport::virtual_activity, FIT::sub_sport::indoor_cycling, FIT::sub_sport::treadmill }) {
        Made t;
        t.activity.records = { at(T1, Huntsville) };
        t.activity.sub_sport = sub_sport;
        CHECK(weather_points(t.activity) == std::unexpected(NoWeather::Indoor));
    }
    // Zwift's worlds are mapped onto real places.
    Made t;
    t.activity.records = { at(T1, Coordinates { .lat = -11.64f, .lon = 166.95f }) };
    t.activity.manufacturer = FIT::manufacturer::zwift;
    CHECK(weather_points(t.activity) == std::unexpected(NoWeather::Indoor));
    t.activity.manufacturer = FIT::manufacturer::garmin;
    CHECK(weather_points(t.activity).has_value());
}

void test_nearest_hour()
{
    CHECK_EQ(nearest_hour(Midnight + 12 * 3600 + 29 * 60 + 59), Midnight + 12 * 3600);
    CHECK_EQ(nearest_hour(Midnight + 12 * 3600 + 30 * 60), Midnight + 13 * 3600);
    CHECK_EQ(nearest_hour(Midnight + 12 * 3600), Midnight + 12 * 3600);
}

void test_request()
{
    auto const        points = muskoka();
    std::vector const both { points.start, points.end };
    auto const        archive = open_meteo_request(WeatherSource::archive, both);
    CHECK_EQ(archive.method, "GET");
    CHECK(archive.url.starts_with("https://archive-api.open-meteo.com/v1/archive?"));
    CHECK(archive.url.contains("latitude=45.330002%2C45.299999"));
    CHECK(archive.url.contains("longitude=-79.220001%2C-79.099998"));
    CHECK(archive.url.contains("start_date=2019-08-18&end_date=2019-08-18"));
    CHECK(archive.url.contains("timeformat=unixtime"));
    CHECK(archive.url.contains("wind_gusts_10m"));
    auto const forecast = open_meteo_request(WeatherSource::forecast, both);
    CHECK(forecast.url.starts_with("https://api.open-meteo.com/v1/forecast?"));
}

// A ride that ends after midnight UTC, or whose end's nearest hour is
// the next day's, asks for both days.
void test_request_over_midnight()
{
    std::vector<WeatherPoint> const points {
        { .time = Midnight + 22 * 3600, .position = Huntsville },
        { .time = Midnight + 23 * 3600 + 45 * 60, .position = Huntsville },
    };
    auto const request = open_meteo_request(WeatherSource::archive, points);
    CHECK(request.url.contains("start_date=2019-08-18&end_date=2019-08-19"));
}

void test_parse_two_places()
{
    auto const                      points = muskoka();
    std::vector<WeatherPoint> const both { points.start, points.end };
    auto const                      body = std::format("[{},{}]", location(Midnight, 24, 10.0f), location(Midnight, 24, 20.0f));
    auto const                      parsed = parse_open_meteo(body, both, WeatherSource::archive);
    CHECK(parsed.has_value());
    if (!parsed || parsed->size() != 2) {
        return;
    }
    auto const &start = (*parsed)[0];
    CHECK(start.has_value());
    if (start) {
        // 12:23 is nearest 12:00, the twelfth hour.
        CHECK_EQ(start->hour, Midnight + 12 * 3600);
        CHECK_EQ(start->temperature, 22.0f);
        CHECK_EQ(start->code, 0);
        CHECK(start->humidity == std::optional { 64.0f });
        CHECK(start->wind_speed == std::optional { 11.0f });
        CHECK(start->wind_direction == std::optional { 49.0f });
        CHECK(start->wind_gusts == std::optional { 26.0f });
        CHECK(!start->precipitation.has_value()); // Not in the answer
        CHECK(start->source == WeatherSource::archive);
        CHECK(start->at == points.start);
    }
    auto const &end = (*parsed)[1];
    CHECK(end.has_value());
    if (end) {
        // 18:03 is nearest 18:00.
        CHECK_EQ(end->hour, Midnight + 18 * 3600);
        CHECK_EQ(end->temperature, 38.0f);
    }
}

void test_parse_one_place_and_a_missing_gust()
{
    std::vector<WeatherPoint> const one { { .time = Midnight + 3600, .position = Huntsville } };
    auto const                      parsed = parse_open_meteo(location(Midnight, 24, 10.0f), one, WeatherSource::forecast);
    CHECK(parsed.has_value() && parsed->size() == 1 && (*parsed)[0].has_value());
    if (parsed && parsed->size() == 1 && (*parsed)[0]) {
        CHECK(!(*parsed)[0]->wind_gusts.has_value());
        CHECK_EQ((*parsed)[0]->code, 61);
        CHECK((*parsed)[0]->source == WeatherSource::forecast);
    }
}

void test_parse_empty_hours()
{
    auto const                      points = muskoka();
    std::vector<WeatherPoint> const both { points.start, points.end };
    auto const                      body = std::format("[{},{}]", location(Midnight, 24, 10.0f, 15), location(Midnight, 24, 10.0f, 15));
    auto const                      parsed = parse_open_meteo(body, both, WeatherSource::archive);
    CHECK(parsed.has_value());
    if (parsed && parsed->size() == 2) {
        CHECK((*parsed)[0].has_value());
        CHECK(!(*parsed)[1].has_value());
    }
}

void test_parse_errors()
{
    std::vector<WeatherPoint> const one { { .time = T1, .position = Huntsville } };
    auto const                      error = parse_open_meteo(R"({"error":true,"reason":"Invalid value"})", one, WeatherSource::archive);
    CHECK(!error.has_value());
    if (!error) {
        CHECK_EQ(error.error().description, "Open-Meteo said: Invalid value");
    }
    CHECK(!parse_open_meteo("<html>", one, WeatherSource::archive).has_value());
    // An answer for another number of places than asked.
    CHECK(!parse_open_meteo(std::format("[{},{}]", location(Midnight, 24, 1), location(Midnight, 24, 1)), one, WeatherSource::archive).has_value());
}

void test_fetch_from_the_archive()
{
    FakeOpenMeteo fake;
    fake.answers.push_back(HttpResponse { .status = 200, .body = std::format("[{},{}]", location(Midnight, 24, 10.0f), location(Midnight, 24, 20.0f)) });
    auto const weather = fetch_weather(fake.fetch(), muskoka());
    CHECK(weather.has_value());
    CHECK_EQ(fake.requests.size(), 1uz);
    if (weather) {
        CHECK(!weather->from_forecast());
        CHECK_EQ(weather->start.temperature, 22.0f);
        CHECK_EQ(weather->end.temperature, 38.0f);
        CHECK(weather->points() == muskoka());
    }
}

// The archive doesn't have the end's hour yet: the forecast is asked for
// it, alone.
void test_fetch_from_the_forecast()
{
    FakeOpenMeteo fake;
    fake.answers.push_back(HttpResponse { .status = 200, .body = std::format("[{},{}]", location(Midnight, 24, 10.0f), location(Midnight, 24, 20.0f, 15)) });
    fake.answers.push_back(HttpResponse { .status = 200, .body = location(Midnight, 24, 30.0f) });
    auto const weather = fetch_weather(fake.fetch(), muskoka());
    CHECK(weather.has_value());
    CHECK_EQ(fake.requests.size(), 2uz);
    if (fake.requests.size() == 2) {
        CHECK(fake.requests[1].url.starts_with("https://api.open-meteo.com/v1/forecast?"));
        CHECK(fake.requests[1].url.contains("latitude=45.299999&"));
    }
    if (weather) {
        CHECK(weather->from_forecast());
        CHECK(weather->start.source == WeatherSource::archive);
        CHECK(weather->end.source == WeatherSource::forecast);
        CHECK_EQ(weather->end.temperature, 48.0f);
    }
}

void test_fetch_errors()
{
    {
        FakeOpenMeteo fake;
        fake.answers.push_back(HttpResponse { .status = 429, .body = R"({"error":true,"reason":"Daily API request limit exceeded"})" });
        auto const weather = fetch_weather(fake.fetch(), muskoka());
        CHECK(!weather.has_value());
        if (!weather) {
            CHECK_EQ(weather.error().description, "Open-Meteo's limit for today is reached");
        }
    }
    {
        FakeOpenMeteo fake;
        fake.answers.push_back(HttpResponse { .status = 400, .body = R"({"error":true,"reason":"Invalid value"})" });
        auto const weather = fetch_weather(fake.fetch(), muskoka());
        CHECK(!weather.has_value());
        if (!weather) {
            CHECK_EQ(weather.error().description, "Open-Meteo answered 400: Invalid value");
        }
    }
    {
        FakeOpenMeteo fake;
        fake.answers.emplace_back();
        auto const weather = fetch_weather(fake.fetch(), muskoka());
        CHECK(!weather.has_value());
        if (!weather) {
            CHECK_EQ(weather.error().description, "Couldn't reach Open-Meteo: Could not resolve host");
        }
    }
    {
        // Neither has it.
        FakeOpenMeteo fake;
        fake.answers.push_back(HttpResponse { .status = 200, .body = std::format("[{},{}]", location(Midnight, 24, 10.0f, 0), location(Midnight, 24, 10.0f, 0)) });
        fake.answers.push_back(HttpResponse { .status = 200, .body = std::format("[{},{}]", location(Midnight, 24, 10.0f, 0), location(Midnight, 24, 10.0f, 0)) });
        CHECK(!fetch_weather(fake.fetch(), muskoka()).has_value());
    }
}

ActivityWeather some_weather()
{
    FakeOpenMeteo fake;
    fake.answers.push_back(HttpResponse { .status = 200, .body = std::format("[{},{}]", location(Midnight, 24, 10.0f), location(Midnight, 24, 20.0f)) });
    return MUST_EVAL(fetch_weather(fake.fetch(), muskoka()));
}

void test_sidecar()
{
    TempDir    dir { "weather" };
    auto const sidecar = dir.path / "1566130980.json";
    CHECK(!sidecar_weather(sidecar).has_value());
    MUST(save_sidecar_title(sidecar, "Muskoka 70.3"));
    auto const weather = some_weather();
    MUST(save_sidecar_weather(sidecar, weather));
    auto const read = sidecar_weather(sidecar);
    CHECK(read.has_value());
    if (read) {
        CHECK(read->points().start.same_as(weather.start.at));
        CHECK(read->points().end.same_as(weather.end.at));
        CHECK_EQ(read->start.temperature, weather.start.temperature);
        CHECK(read->start.wind_gusts == weather.start.wind_gusts);
        CHECK(!read->start.precipitation.has_value());
        CHECK(read->start.source == WeatherSource::archive);
    }
    CHECK(sidecar_own_title(sidecar) == std::optional<std::string> { "Muskoka 70.3" });
    CHECK(!sidecar_weather(sidecar, 2).has_value());
}

void test_sidecar_of_a_leg()
{
    TempDir    dir { "weather-leg" };
    auto const sidecar = dir.path / "1566130980.json";
    MUST(save_sidecar_title(sidecar, "Bike", 2));
    auto weather = some_weather();
    weather.end.source = WeatherSource::forecast;
    MUST(save_sidecar_weather(sidecar, weather, 2));
    CHECK(!sidecar_weather(sidecar).has_value());
    auto const read = sidecar_weather(sidecar, 2);
    CHECK(read.has_value());
    if (read) {
        CHECK(read->from_forecast());
    }
    CHECK(sidecar_titles(sidecar).legs.size() == 3 && sidecar_titles(sidecar).legs[2].own == std::optional<std::string> { "Bike" });
}

void test_same_point()
{
    WeatherPoint const point { .time = T1, .position = Huntsville };
    CHECK(point.same_as(WeatherPoint { .time = T1, .position = { .lat = 45.330001f, .lon = -79.220001f } }));
    CHECK(!point.same_as(WeatherPoint { .time = T1 + 1, .position = Huntsville }));
    CHECK(!point.same_as(WeatherPoint { .time = T1, .position = { .lat = 45.331f, .lon = -79.22f } }));
}

}

int main()
{
    test_points_are_the_first_and_last_with_a_position();
    test_no_position_no_weather();
    test_indoors_no_weather();
    test_nearest_hour();
    test_request();
    test_request_over_midnight();
    test_parse_two_places();
    test_parse_one_place_and_a_missing_gust();
    test_parse_empty_hours();
    test_parse_errors();
    test_fetch_from_the_archive();
    test_fetch_from_the_forecast();
    test_fetch_errors();
    test_sidecar();
    test_sidecar_of_a_leg();
    test_same_point();
    return test_result();
}
