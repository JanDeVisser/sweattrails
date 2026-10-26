/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <format>

#include <App.h>

#include <SweatTrails.h>
#include <widget/OverlayPreview.h>

namespace ST {

namespace {

constexpr float CheckerSize = 16;
constexpr Color CheckerDark { .r = 0x20, .g = 0x20, .b = 0x20, .a = 0xff };
constexpr Color CheckerLight { .r = 0x2c, .g = 0x2c, .b = 0x2c, .a = 0xff };

// A value's unit is this much of its font size to the right of it.
constexpr float UnitGap = 0.2f;

}

OverlayPreview::OverlayPreview(pWidget const &parent)
    : Widget(parent)
{
    policy = SizePolicy::Hide;
    padding = { 10, 10, 10, 10 };
}

void OverlayPreview::set_activity(std::shared_ptr<DisplayedActivity> const &activity)
{
    this->activity = activity;
}

void OverlayPreview::set_layout(std::filesystem::path const &path)
{
    if (file && file->path == path) {
        return;
    }
    file.emplace(path);
    file->reload_if_changed();
    layout_loaded();
}

// Saving the file in an editor shows up within a second.
void OverlayPreview::process_input()
{
    auto const now = App::the()->time;
    if (!file || policy == SizePolicy::Hide || now - checked_at < 1.0) {
        return;
    }
    checked_at = now;
    if (file->reload_if_changed()) {
        layout_loaded();
    }
}

// Finds the layout's fonts once, rather than on every frame. A font that
// isn't in any of the font directories is an error, as a mistake in the
// file is.
void OverlayPreview::layout_loaded()
{
    font_paths.clear();
    font_error.reset();
    fonts.clear();
    if (!file || !file->layout) {
        return;
    }
    auto paths = layout_fonts(*file->layout, [](std::string_view name) { return SweatTrails::the()->find_font(name); });
    if (!paths) {
        font_error = std::format("{}: {}", file->path.filename().string(), paths.error().to_string());
        return;
    }
    font_paths = std::move(*paths);
    fonts.set_codepoints(layout_codepoints(*file->layout));
}

// As large as fits, with the layout's aspect ratio, centered.
Rectangle OverlayPreview::canvas() const
{
    auto const design = (file && file->layout) ? file->layout->design_size : Vector2 { .x = 1920, .y = 1080 };
    auto const room = Vector2 { .x = viewport.width, .y = viewport.height };
    auto const scale = std::max(0.0f, layout_scale(design, room));
    auto const width = std::floor(design.x * scale);
    auto const height = std::floor(design.y * scale);
    return Rectangle {
        .x = viewport.x + (room.x - width) / 2,
        .y = viewport.y + (room.y - height) / 2,
        .width = width,
        .height = height,
    };
}

void OverlayPreview::draw()
{
    auto const area = canvas();
    if (area.width < 1 || area.height < 1) {
        return;
    }
    draw_checkerboard(area);
    if (!file) {
        draw_message(area, "No overlay layout");
        return;
    }
    if (!file->layout) {
        draw_message(area, file->layout.error());
        return;
    }
    if (font_error) {
        draw_message(area, *font_error);
        return;
    }
    auto const &layout = *file->layout;
    auto const  scale = layout_scale(layout.design_size, Vector2 { .x = area.width, .y = area.height });
    if (scale != fonts_scale) {
        fonts.clear();
        fonts_scale = scale;
    }
    BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width), static_cast<int>(area.height));
    for (auto const &element : layout.elements) {
        draw_element(element, layout, area, scale);
    }
    EndScissorMode();
    if (layout.skipped > 0) {
        auto const note = std::format("{} {} can't be shown yet", layout.skipped, (layout.skipped == 1) ? "element" : "elements");
        DrawTextEx(*App::the()->font(FontSize::Small), note.c_str(),
            Vector2 { .x = area.x + 6, .y = area.y + area.height - App::the()->cell(FontSize::Small).y - 4 },
            static_cast<float>(App::the()->font_sizes[static_cast<size_t>(FontSize::Small)]), 2, GRAY);
    }
}

void OverlayPreview::draw_checkerboard(Rectangle const &area) const
{
    DrawRectangleRec(area, CheckerDark);
    for (auto y = 0.0f; y < area.height; y += CheckerSize) {
        for (auto x = 0.0f; x < area.width; x += CheckerSize) {
            if ((static_cast<int>(x / CheckerSize) + static_cast<int>(y / CheckerSize)) % 2 == 1) {
                DrawRectangleRec(Rectangle {
                                     .x = area.x + x,
                                     .y = area.y + y,
                                     .width = std::min(CheckerSize, area.width - x),
                                     .height = std::min(CheckerSize, area.height - y),
                                 },
                    CheckerLight);
            }
        }
    }
}

void OverlayPreview::draw_message(Rectangle const &area, std::string_view message) const
{
    auto const font = App::the()->font(FontSize::Medium);
    if (!font) {
        return;
    }
    BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width), static_cast<int>(area.height));
    auto const text = std::string { message };
    DrawTextEx(*font, text.c_str(), Vector2 { .x = area.x + 10, .y = area.y + 10 },
        static_cast<float>(App::the()->font_sizes[static_cast<size_t>(FontSize::Medium)]), 2, RAYWHITE);
    EndScissorMode();
}

std::optional<Font> OverlayPreview::font(TextStyle const &style, float scale)
{
    auto const path = font_paths.find(style.font);
    if (path == font_paths.end()) {
        return { };
    }
    return fonts.get(path->second, std::max(1, static_cast<int>(std::lround(style.font_size * scale))));
}

void OverlayPreview::draw_text(Font const &font, std::string_view text, Vector2 at, TextStyle const &style, float scale) const
{
    auto const s = std::string { text };
    auto const size = static_cast<float>(font.baseSize);
    if (style.shadow) {
        auto const offset = Vector2 { .x = at.x + style.shadow->offset.x * scale, .y = at.y + style.shadow->offset.y * scale };
        DrawTextEx(font, s.c_str(), offset, size, 0, style.shadow->color);
    }
    DrawTextEx(font, s.c_str(), at, size, 0, style.color);
}

// The text, or the value with its unit after it, measured, placed, and
// drawn.
void OverlayPreview::draw_element(OverlayElement const &element, OverlayLayout const &layout, Rectangle const &area, float scale)
{
    auto const canvas_size = Vector2 { .x = area.width, .y = area.height };
    auto const record = activity ? activity->current_record() : 0uz;
    auto const offset = [&area](Rectangle const &box) { return Vector2 { .x = area.x + box.x, .y = area.y + box.y }; };

    if (auto const *text = std::get_if<TextElement>(&element)) {
        auto const f = font(text->style, scale);
        if (!f || !activity) {
            return;
        }
        auto const shown = fill_template(text->text, activity->activity, record, layout.units, "--");
        auto const measured = MeasureTextEx(*f, shown.c_str(), static_cast<float>(f->baseSize), 0);
        auto const box_size = text->at.size ? Vector2 { .x = text->at.size->x * scale, .y = text->at.size->y * scale } : measured;
        auto const box = place(text->at, box_size, canvas_size, scale);
        auto       at = offset(box);
        if (text->align == Align::Center) {
            at.x += (box.width - measured.x) / 2;
        } else if (text->align == Align::Right) {
            at.x += box.width - measured.x;
        }
        draw_text(*f, shown, at, text->style, scale);
        return;
    }

    auto const &value = std::get<ValueElement>(element);
    auto const  f = font(value.style, scale);
    auto const  unit_font = font(value.unit_style, scale);
    if (!f || !unit_font || !activity) {
        return;
    }
    auto const number = field_value(activity->activity, record, value.field);
    if (!number && value.hide_when_missing) {
        return;
    }
    auto const unit = value.unit.value_or(default_unit(value.field, layout.units, activity->activity.sport));
    auto const spec = value.spec.empty() ? default_spec(value.field) : std::string_view { value.spec };
    auto const shown = format_field(value.field, number, unit, spec, value.missing);
    auto const unit_text = (number && !unit.name.empty()) ? std::string { unit.name } : std::string { };
    auto const measured = MeasureTextEx(*f, shown.c_str(), static_cast<float>(f->baseSize), 0);
    auto const unit_measured = unit_text.empty() ? Vector2 { } : MeasureTextEx(*unit_font, unit_text.c_str(), static_cast<float>(unit_font->baseSize), 0);
    auto const gap = unit_text.empty() ? 0.0f : value.unit_style.font_size * scale * UnitGap;
    auto const size = Vector2 { .x = measured.x + gap + unit_measured.x, .y = std::max(measured.y, unit_measured.y) };
    auto const box = place(value.at, value.at.size ? Vector2 { .x = value.at.size->x * scale, .y = value.at.size->y * scale } : size, canvas_size, scale);
    auto const at = offset(box);
    // The number and its unit stand on the same line, at the bottom.
    draw_text(*f, shown, Vector2 { .x = at.x, .y = at.y + size.y - measured.y }, value.style, scale);
    if (!unit_text.empty()) {
        draw_text(*unit_font, unit_text, Vector2 { .x = at.x + measured.x + gap, .y = at.y + size.y - unit_measured.y }, value.unit_style, scale);
    }
}

}
