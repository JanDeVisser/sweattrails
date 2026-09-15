#include <Coordinates.h>

namespace ST {

bool Coordinates::in_box(Box const &box) const
{
    return box.has(*this);
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

}
