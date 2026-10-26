/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The overlay drawn over a checkerboard, at the record marked in the graph,
 * to see a layout while writing it. See specs/overlay-preview.md.
 */

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <raylib.h>

#include <FontCache.h>
#include <Widget.h>

#include <overlay/LayoutFile.h>
#include <widget/DisplayedActivity.h>

namespace ST {

struct OverlayPreview : public Widget {
    std::shared_ptr<DisplayedActivity> activity { };
    std::optional<LayoutFile>          file { };

    explicit OverlayPreview(pWidget const &parent);
    void set_activity(std::shared_ptr<DisplayedActivity> const &activity);
    void set_layout(std::filesystem::path const &path);
    void process_input() override;
    void draw() override;

private:
    FontCache                                    fonts { };
    float                                        fonts_scale { 0 };
    double                                       checked_at { 0 };
    std::map<std::string, std::filesystem::path> font_paths { }; // By the layout's name for them
    std::optional<std::string>                   font_error { };

    void                layout_loaded();
    Rectangle           canvas() const; // In screen coordinates
    void                draw_checkerboard(Rectangle const &area) const;
    void                draw_message(Rectangle const &area, std::string_view message) const;
    void                draw_element(OverlayElement const &element, OverlayLayout const &layout, Rectangle const &area, float scale);
    std::optional<Font> font(TextStyle const &style, float scale);
    void                draw_text(Font const &font, std::string_view text, Vector2 at, TextStyle const &style, float scale) const;
};

}
