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

}

OverlayPreview::OverlayPreview(pWidget const &parent)
    : Widget(parent)
    , renderer([](std::string_view name) { return SweatTrails::the()->find_font(name); })
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

void OverlayPreview::layout_loaded()
{
    renderer.clear();
    if (file && file->layout) {
        renderer.set_layout(*file->layout, file->path.filename().string());
    }
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
    if (renderer.error()) {
        draw_message(area, *renderer.error());
        return;
    }
    auto const &layout = *file->layout;
    if (activity) {
        BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width), static_cast<int>(area.height));
        renderer.draw(values_at_record(activity->activity, activity->current_record()), area);
        EndScissorMode();
    }
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
    auto const size = static_cast<float>(App::the()->font_sizes[static_cast<size_t>(FontSize::Medium)]);
    auto const measure = [&font, size](std::string_view text) {
        return MeasureTextEx(*font, std::string { text }.c_str(), size, 2).x;
    };
    BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width), static_cast<int>(area.height));
    auto y = area.y + 10;
    for (auto const &line : wrap_words(message, area.width - 20, measure)) {
        DrawTextEx(*font, line.c_str(), Vector2 { .x = area.x + 10, .y = y }, size, 2, RAYWHITE);
        y += App::the()->cell(FontSize::Medium).y;
    }
    EndScissorMode();
}

}
