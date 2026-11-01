#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include <Coordinates.h>

namespace ST {

bool Coordinates::in_box(Box const &box) const
{
    return box.has(*this);
}

std::string Coordinates::to_string() const
{
    return std::format("{:.2f}º{},{:.2f}º{}", std::abs(lat), (lat < 0) ? 'S' : 'N', std::abs(lon), (lon < 0) ? 'W' : 'E');
}

float64 distance_between(Coordinates const &from, Coordinates const &to)
{
    constexpr float64 EarthRadius = 6'371'008.8; // m, the mean
    auto const        radians = [](float32 degrees) { return static_cast<float64>(degrees) * std::numbers::pi / 180; };
    auto const        dlat = radians(to.lat - from.lat);
    auto const        dlon = radians(to.lon - from.lon);
    auto const        a = std::pow(std::sin(dlat / 2), 2) + std::cos(radians(from.lat)) * std::cos(radians(to.lat)) * std::pow(std::sin(dlon / 2), 2);
    return 2 * EarthRadius * std::asin(std::min(1.0, std::sqrt(a)));
}

Box Box::with_margins(float32 margin) const
{
    auto const mid = center();
    auto const f = 1.0f + margin;
    return Box {
        .sw = { .lat = mid.lat - (height() * f) / 2.0f, .lon = mid.lon - (width() * f) / 2.0f },
        .ne = { .lat = mid.lat + (height() * f) / 2.0f, .lon = mid.lon + (width() * f) / 2.0f },
    };
}

Coordinates Box::center() const
{
    return Coordinates {
        .lat = (sw.lat + ne.lat) / 2.0f,
        .lon = (sw.lon + ne.lon) / 2.0f,
    };
}

float32 Box::width() const
{
    return ne.lon - sw.lon;
}

float32 Box::height() const
{
    return ne.lat - sw.lat;
}

bool Box::contains(Box const &other) const
{
    return has(other.sw) && has(other.ne);
}

bool Box::has(Coordinates const &point) const
{
    return point.lat >= sw.lat && point.lon >= sw.lon && point.lat <= ne.lat && point.lon <= ne.lon;
}

void Box::extend(Coordinates const &point)
{
    if (!has(point)) {
        sw.lat = std::min(sw.lat, point.lat);
        sw.lon = std::min(sw.lon, point.lon);
        ne.lat = std::max(ne.lat, point.lat);
        ne.lon = std::max(ne.lon, point.lon);
    }
}

std::string Box::to_string() const
{
    return std::format("NE: {} SW: {}", ne.to_string(), sw.to_string());
}

}
