/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <format>

#include <overlay/Renderer.h>

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

// The text, or the value with its unit after it, measured, placed, and
// drawn.
void OverlayRenderer::draw_element(OverlayElement const &element, FieldValues const &values, Rectangle const &area)
{
    auto const &layout = *m_layout;
    auto const  scale = m_scale;
    auto const  canvas_size = Vector2 { .x = area.width, .y = area.height };
    auto const  offset = [&area](Rectangle const &box) { return Vector2 { .x = area.x + box.x, .y = area.y + box.y }; };

    if (auto const *text = std::get_if<TextElement>(&element)) {
        auto const f = font(text->style, text->number);
        if (!f) {
            return;
        }
        auto const shown = fill_template(text->text, values, layout.units, "--");
        auto const measured = MeasureTextEx(*f, shown.c_str(), static_cast<float>(f->baseSize), 0);
        auto const box_size = text->at.size ? Vector2 { .x = text->at.size->x * scale, .y = text->at.size->y * scale } : measured;
        auto const box = place(text->at, box_size, canvas_size, scale);
        auto const in_box = place_in_box(box_size, measured, text->at.anchor, text->align);
        auto const at = offset(box);
        draw_text(*f, shown, Vector2 { .x = at.x + in_box.x, .y = at.y + in_box.y }, text->style);
        return;
    }

    auto const &value = std::get<ValueElement>(element);
    auto const  f = font(value.style, value.number);
    auto const  unit_font = font(value.unit_style, value.number);
    if (!f || !unit_font) {
        return;
    }
    auto const number = values[value.field];
    if (!number && value.hide_when_missing) {
        return;
    }
    auto const unit = value.unit.value_or(default_unit(value.field, layout.units, values.sport));
    auto const spec = value.spec.empty() ? default_spec(value.field) : std::string_view { value.spec };
    auto const shown = format_field(value.field, number, unit, spec, value.missing);
    auto const unit_text = (number && !unit.name.empty()) ? std::string { unit.name } : std::string { };
    auto const measured = MeasureTextEx(*f, shown.c_str(), static_cast<float>(f->baseSize), 0);
    auto const unit_measured = unit_text.empty() ? Vector2 { } : MeasureTextEx(*unit_font, unit_text.c_str(), static_cast<float>(unit_font->baseSize), 0);
    auto const gap = unit_text.empty() ? 0.0f : value.unit_style.font_size * scale * UnitGap;
    auto const size = Vector2 { .x = measured.x + gap + unit_measured.x, .y = std::max(measured.y, unit_measured.y) };
    auto const box_size = value.at.size ? Vector2 { .x = value.at.size->x * scale, .y = value.at.size->y * scale } : size;
    auto const box = place(value.at, box_size, canvas_size, scale);
    auto const in_box = place_in_box(box_size, size, value.at.anchor);
    auto const at = Vector2 { .x = offset(box).x + in_box.x, .y = offset(box).y + in_box.y };
    // The number and its unit stand on the same line, at the bottom.
    draw_text(*f, shown, Vector2 { .x = at.x, .y = at.y + size.y - measured.y }, value.style);
    if (!unit_text.empty()) {
        draw_text(*unit_font, unit_text, Vector2 { .x = at.x + measured.x + gap, .y = at.y + size.y - unit_measured.y }, value.unit_style);
    }
}

}
