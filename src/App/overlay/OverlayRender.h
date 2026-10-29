/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A render of an overlay to video, while it runs. It draws on the UI
 * thread, where the OpenGL context is, a few frames at a time, so that the
 * app stays usable. See specs/video-overlay.md.
 */

#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>

#include <raylib.h>

#include <overlay/Encoder.h>
#include <overlay/OverlayTiles.h>
#include <overlay/Render.h>
#include <overlay/Renderer.h>
#include <widget/DisplayedActivity.h>

namespace ST {

struct RenderJob {
    std::shared_ptr<DisplayedActivity> activity;
    OverlayLayout                      layout;
    fs::path                           layout_file; // For the errors, and the pictures next to it
    RenderSettings                     settings;
    RenderRange                        range;
    fs::path                           output;
    fs::path                           ffmpeg;
};

struct OverlayRender {
    // Starts ffmpeg; an error if it, or the layout's fonts, can't be had.
    // The maps' tiles come from the cache, or are downloaded on the job
    // thread with `schedule`.
    static std::expected<std::unique_ptr<OverlayRender>, std::string> start(RenderJob job, FontFinder find, ImageFinder find_image, TileFetcher cached, TileScheduler schedule);

    OverlayRender(OverlayRender const &) = delete;
    OverlayRender &operator=(OverlayRender const &) = delete;
    ~OverlayRender(); // Stops, unless it's over

    // Renders frames for about `budget` seconds, or until ffmpeg has as
    // many as it can take, or a frame's tiles aren't there yet. After the
    // last frame, waits for ffmpeg.
    void step(double budget);
    // Stops ffmpeg and removes the file.
    void stop();

    [[nodiscard]] bool                over() const { return m_result.has_value(); }
    [[nodiscard]] double              progress() const;                     // 0 to 1
    [[nodiscard]] bool                waiting() const { return m_waiting; } // For the next frame's tiles
    [[nodiscard]] OverlayTiles       &tiles() { return m_tiles; }
    [[nodiscard]] OverlayTiles const &tiles() const { return m_tiles; }
    [[nodiscard]] fs::path            output() const { return m_job.output; }
    // Once it's over: whether the file is there, or what went wrong.
    [[nodiscard]] std::optional<std::expected<void, std::string>> const &result() const { return m_result; }

private:
    OverlayRender(RenderJob job, FontFinder find, ImageFinder find_image, TileFetcher cached, TileScheduler schedule);
    [[nodiscard]] Rectangle large_area() const;
    void                    render_frame(FieldValues const &values, VideoTime time);

    RenderJob                                       m_job;
    OverlaySource                                   m_source;
    OverlayTiles                                    m_tiles;
    OverlayRenderer                                 m_renderer;
    std::unique_ptr<Encoder>                        m_encoder;
    RenderTexture2D                                 m_large { };  // Twice the size, for smooth edges
    RenderTexture2D                                 m_target { }; // The frame, read back
    size_t                                          m_frame { 0 };
    size_t                                          m_frames { 0 };
    bool                                            m_waiting { false };
    std::optional<std::expected<void, std::string>> m_result { };
};

}
