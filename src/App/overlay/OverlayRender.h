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
#include <overlay/Render.h>
#include <overlay/Renderer.h>
#include <widget/DisplayedActivity.h>

namespace ST {

struct RenderJob {
    std::shared_ptr<DisplayedActivity> activity;
    OverlayLayout                      layout;
    std::string                        layout_file; // For the errors
    RenderSettings                     settings;
    RenderRange                        range;
    fs::path                           output;
    fs::path                           ffmpeg;
};

class OverlayRender {
public:
    // Starts ffmpeg; an error if it, or the layout's fonts, can't be had.
    static std::expected<std::unique_ptr<OverlayRender>, std::string> start(RenderJob job, FontFinder find);

    OverlayRender(OverlayRender const &) = delete;
    OverlayRender &operator=(OverlayRender const &) = delete;
    ~OverlayRender(); // Stops, unless it's over

    // Renders frames for about `budget` seconds, or until ffmpeg has as
    // many as it can take. After the last frame, waits for ffmpeg.
    void step(double budget);
    // Stops ffmpeg and removes the file.
    void stop();

    [[nodiscard]] bool     over() const { return m_result.has_value(); }
    [[nodiscard]] double   progress() const; // 0 to 1
    [[nodiscard]] fs::path output() const { return m_job.output; }
    // Once it's over: whether the file is there, or what went wrong.
    [[nodiscard]] std::optional<std::expected<void, std::string>> const &result() const { return m_result; }

private:
    explicit OverlayRender(RenderJob job, FontFinder find);
    void render_frame();

    RenderJob                                       m_job;
    OverlayRenderer                                 m_renderer;
    std::unique_ptr<Encoder>                        m_encoder;
    RenderTexture2D                                 m_target { };
    size_t                                          m_frame { 0 };
    size_t                                          m_frames { 0 };
    std::optional<std::expected<void, std::string>> m_result { };
};

}
