/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <overlay/Shapes.h>

namespace ST {

Rectangle content_box(Rectangle box, float padding)
{
    auto const x = std::min(padding, box.width / 2);
    auto const y = std::min(padding, box.height / 2);
    return Rectangle { .x = box.x + x, .y = box.y + y, .width = box.width - 2 * x, .height = box.height - 2 * y };
}

Vector2 box_size(Placement const &at, Vector2 content, Panel const &panel, float scale)
{
    if (at.size) {
        return Vector2 { .x = at.size->x * scale, .y = at.size->y * scale };
    }
    auto const padding = 2 * panel.padding * scale;
    return Vector2 { .x = content.x + padding, .y = content.y + padding };
}

Vector2 image_size(Vector2 picture, std::optional<float> width, std::optional<float> height)
{
    if (width && height) {
        return Vector2 { .x = *width, .y = *height };
    }
    if (width) {
        return Vector2 { .x = *width, .y = (picture.x > 0) ? *width * picture.y / picture.x : 0 };
    }
    if (height) {
        return Vector2 { .x = (picture.y > 0) ? *height * picture.x / picture.y : 0, .y = *height };
    }
    return picture;
}

Rectangle fit_picture(Rectangle content, Vector2 picture, Anchor anchor)
{
    if (picture.x <= 0 || picture.y <= 0) {
        return Rectangle { .x = content.x, .y = content.y, .width = 0, .height = 0 };
    }
    auto const scale = std::min(content.width / picture.x, content.height / picture.y);
    auto const size = Vector2 { .x = picture.x * scale, .y = picture.y * scale };
    auto const at = place_in_box(Vector2 { .x = content.width, .y = content.height }, size, anchor);
    return Rectangle { .x = content.x + at.x, .y = content.y + at.y, .width = size.x, .height = size.y };
}

ImageBox image_box(ImageElement const &image, Vector2 picture, Vector2 canvas, float scale)
{
    auto const size = image_size(picture, image.width, image.height);
    auto       at = image.at;
    if (image.width && image.height) {
        at.size = size;
    }
    auto const panel = place(at, box_size(at, Vector2 { .x = size.x * scale, .y = size.y * scale }, image.panel, scale), canvas, scale);
    return ImageBox {
        .panel = panel,
        .picture = fit_picture(content_box(panel, image.panel.padding * scale), picture, image.at.anchor),
    };
}

std::optional<Bounds> shown_bounds(OverlaySource const &source, OverlayField field, FieldUnit const &unit,
    std::optional<double> min, std::optional<double> max)
{
    auto const range = source.range(field);
    if ((!min || !max) && !range) {
        return { };
    }
    Bounds ret {
        .min = min.value_or(range ? unit.shown(range->min) : 0),
        .max = max.value_or(range ? unit.shown(range->max) : 0),
    };
    if (ret.max <= ret.min) {
        ret.max = ret.min + 1;
    }
    return ret;
}

std::optional<float> gauge_fraction(std::optional<double> shown, Bounds const &bounds)
{
    if (!shown) {
        return { };
    }
    return static_cast<float>(std::clamp((*shown - bounds.min) / (bounds.max - bounds.min), 0.0, 1.0));
}

BarRects bar_rects(Rectangle content, GaugeShape shape, std::optional<float> thickness, float fraction)
{
    if (shape == GaugeShape::VBar) {
        auto const width = std::min(thickness.value_or(content.width), content.width);
        auto const track = Rectangle { .x = content.x + (content.width - width) / 2, .y = content.y, .width = width, .height = content.height };
        auto const filled = track.height * fraction;
        return BarRects { .track = track, .fill = Rectangle { .x = track.x, .y = track.y + track.height - filled, .width = width, .height = filled } };
    }
    auto const height = std::min(thickness.value_or(content.height), content.height);
    auto const track = Rectangle { .x = content.x, .y = content.y + (content.height - height) / 2, .width = content.width, .height = height };
    return BarRects { .track = track, .fill = Rectangle { .x = track.x, .y = track.y, .width = track.width * fraction, .height = height } };
}

Ring arc_ring(Rectangle content, std::optional<float> thickness)
{
    auto const outer = std::min(content.width, content.height) / 2;
    auto const thick = std::min(thickness.value_or(outer * DefaultArcThickness), outer);
    return Ring {
        .center = Vector2 { .x = content.x + content.width / 2, .y = content.y + content.height / 2 },
        .inner = outer - thick,
        .outer = outer,
    };
}

ArcAngles arc_angles(float fraction)
{
    return ArcAngles { .start = ArcStart, .end = ArcStart + ArcSweep * std::clamp(fraction, 0.0f, 1.0f) };
}

std::optional<double> graph_now(GraphElement const &graph, FieldValues const &values)
{
    return values[graph.axis == GraphAxis::Time ? OverlayField::elapsed : OverlayField::distance];
}

namespace {

// The records in a column's stretch: whether there are any, and the
// average of those that have the field.
struct Stretch {
    bool                  has_records { false };
    std::optional<double> average { };
};

// The records along the graph's axis: each one's place on it, from the
// first to the last, and its value.
struct Axis {
    Axis(OverlaySource const &source, GraphElement const &graph)
        : m_source(source)
        , m_graph(graph)
    {
    }

    // From the start to the end, in seconds since the start or in meters.
    [[nodiscard]] std::optional<std::pair<double, double>> extent() const
    {
        auto const &records = m_source.activity->records;
        if (m_graph.axis == GraphAxis::Distance) {
            auto const &mileage = m_source.mileage;
            if (mileage.empty()) {
                return { };
            }
            return std::pair { mileage.front().distance, mileage.back().distance };
        }
        if (records.empty()) {
            return { };
        }
        return std::pair { 0.0, time_of(records.size() - 1) };
    }

    // The records from `from`, up to `to`, and at `to` for the last
    // column, so that the activity's last record is in it.
    [[nodiscard]] Stretch stretch(double from, double to, bool last) const
    {
        auto const before_end = [to, last](double x) { return x < to || (last && x == to); };
        Stretch    ret { };
        double     sum { 0 };
        size_t     count { 0 };
        auto const add = [&](size_t record) {
            ret.has_records = true;
            if (auto const value = field_value(m_source, record, m_graph.field); value) {
                sum += *value;
                ++count;
            }
        };
        if (m_graph.axis == GraphAxis::Distance) {
            auto const &mileage = m_source.mileage;
            auto const  first = std::ranges::lower_bound(mileage, from, { }, &OverlaySource::Mileage::distance);
            for (auto it = first; it != mileage.end() && before_end(it->distance); ++it) {
                add(it->record);
            }
        } else {
            auto const &records = m_source.activity->records;
            auto const  start = static_cast<double>(m_source.activity->segment.start_time.timestamp);
            auto const  first = std::ranges::lower_bound(records, start + from, { },
                [](record const &r) { return static_cast<double>(r.timestamp.timestamp); });
            for (auto ix = static_cast<size_t>(first - records.begin()); ix < records.size() && before_end(time_of(ix)); ++ix) {
                add(ix);
            }
        }
        if (count > 0) {
            ret.average = sum / static_cast<double>(count);
        }
        return ret;
    }

    // The value at a place between records: in a pause, as the fields
    // are in one.
    [[nodiscard]] std::optional<double> at(double x) const
    {
        auto const seconds = (m_graph.axis == GraphAxis::Distance) ? elapsed_at(x) : x;
        return values_at_time(m_source, seconds)[m_graph.field];
    }

private:
    [[nodiscard]] double time_of(size_t record) const
    {
        auto const &activity = *m_source.activity;
        return static_cast<double>(activity.records[record].timestamp.timestamp) - static_cast<double>(activity.segment.start_time.timestamp);
    }

    // When the activity was at the distance, between the records around it.
    [[nodiscard]] double elapsed_at(double distance) const
    {
        auto const &mileage = m_source.mileage;
        auto const  after = std::ranges::lower_bound(mileage, distance, { }, &OverlaySource::Mileage::distance);
        if (after == mileage.begin()) {
            return time_of(after->record);
        }
        if (after == mileage.end()) {
            return time_of(mileage.back().record);
        }
        auto const &earlier = *(after - 1);
        auto const  span = after->distance - earlier.distance;
        auto const  from = time_of(earlier.record);
        return (span > 0) ? from + (distance - earlier.distance) / span * (time_of(after->record) - from) : from;
    }

    OverlaySource const &m_source;
    GraphElement const  &m_graph;
};

}

std::vector<std::optional<double>> graph_columns(OverlaySource const &source, GraphElement const &graph,
    FieldUnit const &unit, double now, size_t columns)
{
    std::vector<std::optional<double>> ret(columns);
    Axis const                         axis { source, graph };
    auto const                         extent = axis.extent();
    if (!source.has(graph.field) || !extent || columns == 0) {
        return ret;
    }
    auto const [start, end] = *extent;
    auto const from = graph.window ? now - *graph.window : start;
    auto const to = graph.window ? now : end;
    auto const width = (to - from) / static_cast<double>(columns);
    for (auto ix = 0uz; ix < columns; ++ix) {
        auto const left = from + static_cast<double>(ix) * width;
        auto const middle = left + width / 2;
        if (middle < start || middle > end) {
            continue;
        }
        // The last column ends at the end, not where adding up the widths
        // gets to, which can be a hair short of it.
        auto const last = ix == columns - 1;
        auto const stretch = axis.stretch(left, last ? to : left + width, last);
        auto const value = stretch.has_records ? stretch.average : axis.at(middle);
        if (value) {
            ret[ix] = unit.shown(*value);
        }
    }
    return ret;
}

std::optional<float> graph_position(OverlaySource const &source, GraphElement const &graph, double now)
{
    auto const extent = Axis { source, graph }.extent();
    if (!extent) {
        return { };
    }
    if (graph.window) {
        return 1.0f;
    }
    auto const [start, end] = *extent;
    return (end > start) ? static_cast<float>(std::clamp((now - start) / (end - start), 0.0, 1.0)) : 1.0f;
}

float graph_y(double shown, Bounds const &bounds, Rectangle content)
{
    auto const fraction = std::clamp((shown - bounds.min) / (bounds.max - bounds.min), 0.0, 1.0);
    return content.y + content.height * static_cast<float>(1.0 - fraction);
}

}
