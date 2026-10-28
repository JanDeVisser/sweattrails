/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Draws an overlay layout's elements for one frame's values: on the screen
 * for the preview, or into a render texture for the video.
 */

#pragma once

#include <map>
#include <optional>
#include <string>

#include <raylib.h>

#include <ClipShader.h>

#include <FontCache.h>

#include <overlay/Fields.h>
#include <overlay/LayoutFile.h>
#include <overlay/OverlayTiles.h>
#include <overlay/TrackMap.h>

namespace ST {

struct OverlayRenderer {
    explicit OverlayRenderer(FontFinder find);

    // The layout to draw, from the file called `file_name`, which starts the
    // errors. Finds its fonts: one that isn't there is the error.
    void set_layout(OverlayLayout const &layout, std::string_view file_name);
    void clear();

    [[nodiscard]] OverlayLayout const *layout() const { return m_layout ? &*m_layout : nullptr; }
    // What keeps the layout from being drawn: a font that isn't there, or
    // that raylib couldn't load.
    [[nodiscard]] std::optional<std::string> const &error() const { return m_error; }

    // Draws the elements into `area`, in screen or render texture
    // coordinates, scaled to fit it.
    void draw(FieldValues const &values, Rectangle const &area);

    // The maps' tiles, which the owner of `tiles` asks for before drawing,
    // with `detail` drawing pixels to a pixel of what's made: 2 for a
    // render. Without, as in tests, maps have no tiles.
    void set_tiles(OverlayTiles *tiles, float detail);
    // The tiles the maps show in `area` at the moment: what draw() draws.
    [[nodiscard]] std::vector<Tile> tiles_needed(FieldValues const &values, Rectangle const &area) const;

private:
    FontFinder                                   m_find;
    std::optional<OverlayLayout>                 m_layout { };
    std::string                                  m_file_name { };
    std::optional<std::string>                   m_error { };
    FontCache                                    m_fonts { };
    ClipShader                                   m_clip { };
    OverlayTiles                                *m_tiles { nullptr };
    float                                        m_detail { 1 };
    float                                        m_scale { 0 };
    std::map<std::string, std::filesystem::path> m_font_paths { }; // By the layout's name for them

    void                draw_element(OverlayElement const &element, FieldValues const &values, Rectangle const &area);
    void                draw(TextElement const &text, FieldValues const &values, Rectangle const &area);
    void                draw(ValueElement const &value, FieldValues const &values, Rectangle const &area);
    void                draw(GaugeElement const &gauge, FieldValues const &values, Rectangle const &area);
    void                draw(GraphElement const &graph, FieldValues const &values, Rectangle const &area);
    void                draw(ProfileElement const &profile, FieldValues const &values, Rectangle const &area);
    void                draw(MapElement const &map, FieldValues const &values, Rectangle const &area);
    Rectangle           screen_box(Placement const &at, Vector2 size, Rectangle const &area) const;
    Rectangle           draw_panel(Panel const &panel, Rectangle const &box) const;
    void                draw_background(Panel const &panel, ClipShape const &shape) const;
    void                draw_border(Panel const &panel, ClipShape const &shape) const;
    void                draw_tiles(MapElement const &map, MapTransform const &transform, ClipShape const &clip);
    void                draw_attribution(MapElement const &map, Rectangle const &panel, Rectangle const &area);
    void                draw_trace(std::vector<Vector2> const &points, Trace const &trace, float bottom) const;
    std::optional<Font> font(TextStyle const &style, size_t element);
    void                draw_text(Font const &font, std::string_view text, Vector2 at, TextStyle const &style) const;
};

}
