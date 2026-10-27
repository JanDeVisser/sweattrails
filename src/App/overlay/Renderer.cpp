/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <format>

#include <overlay/Renderer.h>
#include <overlay/Shapes.h>

namespace ST {

namespace {

// A value's unit is this much of its font size to the right of it.
constexpr float UnitGap = 0.2f;

}

OverlayRenderer::OverlayRenderer(FontFinder find)
    : m_find(std::move(find))
{
}

// Finds the layout's fonts once, rather than on every frame. A font that
// isn't in any of the font directories is an error, as a mistake in the
// file is.
void OverlayRenderer::set_layout(OverlayLayout const &layout, std::string_view file_name)
{
    clear();
    m_layout = layout;
    m_file_name = file_name;
    auto paths = layout_fonts(layout, m_find);
    if (!paths) {
        m_error = std::format("{}: {}", m_file_name, paths.error().to_string());
        return;
    }
    m_font_paths = std::move(*paths);
    m_fonts.set_codepoints(layout_codepoints(layout));
}

void OverlayRenderer::clear()
{
    m_layout.reset();
    m_error.reset();
    m_font_paths.clear();
    m_fonts.clear();
    m_scale = 0;
}

void OverlayRenderer::draw(FieldValues const &values, Rectangle const &area)
{
    if (!m_layout || m_error) {
        return;
    }
    auto const scale = layout_scale(m_layout->design_size, Vector2 { .x = area.width, .y = area.height });
    if (scale != m_scale) {
        m_fonts.clear();
        m_scale = scale;
    }
    // The title can have characters the layout doesn't, such as `ü`: the
    // fonts are loaded again with them, once.
    m_fonts.add_codepoints(values.title);
    for (auto const &element : m_layout->elements) {
        draw_element(element, values, area);
    }
}

// A font that's there but can't be loaded, such as a broken file, is an
// error from the next frame on, as one that isn't there is.
std::optional<Font> OverlayRenderer::font(TextStyle const &style, size_t element)
{
    auto const path = m_font_paths.find(style.font);
    if (path == m_font_paths.end()) {
        return { };
    }
    auto const ret = m_fonts.get(path->second, std::max(1, static_cast<int>(std::lround(style.font_size * m_scale))));
    if (!ret && !m_error) {
        m_error = std::format("{}: element {}: the font `{}` can't be loaded from `{}`",
            m_file_name, element, style.font, path->second.string());
    }
    return ret;
}

void OverlayRenderer::draw_text(Font const &font, std::string_view text, Vector2 at, TextStyle const &style) const
{
    auto const s = std::string { text };
    auto const size = static_cast<float>(font.baseSize);
    if (style.shadow) {
        auto const offset = Vector2 { .x = at.x + style.shadow->offset.x * m_scale, .y = at.y + style.shadow->offset.y * m_scale };
        DrawTextEx(font, s.c_str(), offset, size, 0, style.shadow->color);
    }
    DrawTextEx(font, s.c_str(), at, size, 0, style.color);
}

void OverlayRenderer::draw_element(OverlayElement const &element, FieldValues const &values, Rectangle const &area)
{
    std::visit([this, &values, &area](auto const &e) { draw(e, values, area); }, element);
}

// The element's box on the screen, of `size` in pixels: its own size, or
// what it measured with its padding around it.
Rectangle OverlayRenderer::screen_box(Placement const &at, Vector2 size, Rectangle const &area) const
{
    auto const box = place(at, size, Vector2 { .x = area.width, .y = area.height }, m_scale);
    return Rectangle { .x = area.x + box.x, .y = area.y + box.y, .width = box.width, .height = box.height };
}

// The panel behind the box, and the box inside its padding.
Rectangle OverlayRenderer::draw_panel(Panel const &panel, Rectangle const &box) const
{
    // raylib's roundness is the share of the shorter side that's rounded.
    auto const shorter = std::min(box.width, box.height);
    auto const roundness = (shorter > 0) ? std::min(1.0f, 2 * panel.radius * m_scale / shorter) : 0.0f;
    if (panel.background) {
        DrawRectangleRounded(box, roundness, 0, *panel.background);
    }
    if (panel.border) {
        DrawRectangleRoundedLinesEx(box, roundness, 0, panel.border->width * m_scale, panel.border->color);
    }
    return content_box(box, panel.padding * m_scale);
}

void OverlayRenderer::draw(TextElement const &text, FieldValues const &values, Rectangle const &area)
{
    auto const f = font(text.style, text.number);
    if (!f) {
        return;
    }
    auto const shown = fill_template(text.text, values, m_layout->units, "--");
    auto const measured = MeasureTextEx(*f, shown.c_str(), static_cast<float>(f->baseSize), 0);
    auto const content = draw_panel(text.panel, screen_box(text.at, box_size(text.at, measured, text.panel, m_scale), area));
    auto const in_box = place_in_box(Vector2 { .x = content.width, .y = content.height }, measured, text.at.anchor, text.align);
    draw_text(*f, shown, Vector2 { .x = content.x + in_box.x, .y = content.y + in_box.y }, text.style);
}

// The value with its unit after it, measured, placed, and drawn.
void OverlayRenderer::draw(ValueElement const &value, FieldValues const &values, Rectangle const &area)
{
    auto const f = font(value.style, value.number);
    auto const unit_font = font(value.unit_style, value.number);
    if (!f || !unit_font) {
        return;
    }
    auto const shown_as = shown_value(value, values, m_layout->units);
    if (!shown_as) {
        return;
    }
    auto const &shown = shown_as->number;
    auto const &unit_text = shown_as->unit;
    auto const  measured = MeasureTextEx(*f, shown.c_str(), static_cast<float>(f->baseSize), 0);
    auto const  unit_measured = unit_text.empty() ? Vector2 { } : MeasureTextEx(*unit_font, unit_text.c_str(), static_cast<float>(unit_font->baseSize), 0);
    auto const  gap = unit_text.empty() ? 0.0f : value.unit_style.font_size * m_scale * UnitGap;
    auto const  size = Vector2 { .x = measured.x + gap + unit_measured.x, .y = std::max(measured.y, unit_measured.y) };
    auto const  content = draw_panel(value.panel, screen_box(value.at, box_size(value.at, size, value.panel, m_scale), area));
    auto const  in_box = place_in_box(Vector2 { .x = content.width, .y = content.height }, size, value.at.anchor);
    auto const  at = Vector2 { .x = content.x + in_box.x, .y = content.y + in_box.y };
    // The number and its unit stand on the same line, at the bottom.
    // Only the number takes the color of its threshold, not the unit.
    auto style = value.style;
    style.color = shown_as->color;
    draw_text(*f, shown, Vector2 { .x = at.x, .y = at.y + size.y - measured.y }, style);
    if (!unit_text.empty()) {
        draw_text(*unit_font, unit_text, Vector2 { .x = at.x + measured.x + gap, .y = at.y + size.y - unit_measured.y }, value.unit_style);
    }
}

// The track, and over it the fill up to the value. A missing value, or
// "auto" for a field the activity hasn't got, shows the track only.
void OverlayRenderer::draw(GaugeElement const &gauge, FieldValues const &values, Rectangle const &area)
{
    auto const content = draw_panel(gauge.panel, screen_box(gauge.at, box_size(gauge.at, { }, gauge.panel, m_scale), area));
    auto const unit = gauge.unit.value_or(default_unit(gauge.field, m_layout->units, values.sport));
    auto const value = values[gauge.field];
    auto const shown = value ? std::optional { unit.shown(*value) } : std::nullopt;
    auto const bounds = values.source ? shown_bounds(*values.source, gauge.field, unit, gauge.min, gauge.max) : std::nullopt;
    auto const fraction = bounds ? gauge_fraction(shown, *bounds) : std::nullopt;
    auto const fill = threshold_color(gauge.thresholds, shown, gauge.fill);
    auto const thickness = gauge.thickness ? std::optional { *gauge.thickness * m_scale } : std::nullopt;

    if (gauge.shape == GaugeShape::Arc) {
        auto const ring = arc_ring(content, thickness);
        DrawRing(ring.center, ring.inner, ring.outer, ArcStart, ArcStart + ArcSweep, 0, gauge.track);
        if (fraction && *fraction > 0) {
            auto const angles = arc_angles(*fraction);
            DrawRing(ring.center, ring.inner, ring.outer, angles.start, angles.end, 0, fill);
        }
        return;
    }
    auto const rects = bar_rects(content, gauge.shape, thickness, fraction.value_or(0));
    DrawRectangleRec(rects.track, gauge.track);
    if (fraction && *fraction > 0) {
        DrawRectangleRec(rects.fill, fill);
    }
}

// The line through a value per pixel column, broken where values are
// missing, the area under it, and the cursor at the moment.
void OverlayRenderer::draw(GraphElement const &graph, FieldValues const &values, Rectangle const &area)
{
    auto const content = draw_panel(graph.panel, screen_box(graph.at, box_size(graph.at, { }, graph.panel, m_scale), area));
    auto const now = graph_now(graph, values);
    if (!values.source || !now || content.width < 1) {
        return;
    }
    auto const &source = *values.source;
    auto const  unit = graph.unit.value_or(default_unit(graph.field, m_layout->units, values.sport));
    auto const  bounds = shown_bounds(source, graph.field, unit, graph.min, graph.max);
    if (!bounds) {
        return;
    }
    auto const columns = graph_columns(source, graph, unit, *now, static_cast<size_t>(content.width));
    auto const bottom = content.y + content.height;

    std::vector<Vector2> run;
    auto const           draw_run = [&]() {
        if (graph.fill) {
            for (auto ix = 1uz; ix < run.size(); ++ix) {
                auto const &l = run[ix - 1];
                auto const &r = run[ix];
                // Counter-clockwise on the screen, as raylib draws them.
                DrawTriangle(l, Vector2 { .x = l.x, .y = bottom }, Vector2 { .x = r.x, .y = bottom }, *graph.fill);
                DrawTriangle(l, Vector2 { .x = r.x, .y = bottom }, r, *graph.fill);
            }
        }
        auto const width = graph.line.width * m_scale;
        if (run.size() == 1) {
            DrawCircleV(run.front(), width / 2, graph.line.color);
        } else if (run.size() > 1) {
            DrawSplineLinear(run.data(), static_cast<int>(run.size()), width, graph.line.color);
        }
        run.clear();
    };
    for (auto ix = 0uz; ix < columns.size(); ++ix) {
        if (!columns[ix]) {
            draw_run();
            continue;
        }
        run.push_back(Vector2 { .x = content.x + static_cast<float>(ix) + 0.5f, .y = graph_y(*columns[ix], *bounds, content) });
    }
    draw_run();

    auto const position = graph_position(source, graph, *now);
    auto const value = values[graph.field];
    if (graph.cursor && position && value) {
        auto const at = Vector2 { .x = content.x + *position * content.width, .y = graph_y(unit.shown(*value), *bounds, content) };
        DrawCircleV(at, graph.cursor->radius * m_scale, graph.cursor->color);
    }
}

void OverlayRenderer::draw(ProfileElement const &, FieldValues const &, Rectangle const &)
{
}

}
