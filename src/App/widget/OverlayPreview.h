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

#include <Widget.h>

#include <overlay/LayoutFile.h>
#include <overlay/Renderer.h>
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
    OverlayRenderer renderer;
    double          checked_at { 0 };

    void      layout_loaded();
    Rectangle canvas() const; // In screen coordinates
    void      draw_checkerboard(Rectangle const &area) const;
    void      draw_message(Rectangle const &area, std::string_view message) const;
};

}
