/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

// raylib.h before rlgl.h, which defines Matrix only without it.
#include <raylib.h>
#include <rlgl.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <format>

#include <overlay/OverlayRender.h>

namespace ST {

OverlayRender::OverlayRender(RenderJob job, FontFinder find)
    : m_job(std::move(job))
    , m_renderer(std::move(find))
{
    m_frames = frame_count(m_job.range, m_job.settings.fps);
}

std::expected<std::unique_ptr<OverlayRender>, std::string> OverlayRender::start(RenderJob job, FontFinder find)
{
    std::unique_ptr<OverlayRender> render { new OverlayRender(std::move(job), std::move(find)) };
    auto const                    &job_ = render->m_job;
    render->m_renderer.set_layout(job_.layout, job_.layout_file);
    if (render->m_renderer.error()) {
        return std::unexpected(*render->m_renderer.error());
    }
    auto const [width, height] = job_.settings.resolution;
    auto const clock = activity_clock(job_.activity->activity);
    auto       encoder = Encoder::start(job_.ffmpeg, ffmpeg_arguments(job_.settings, clock, job_.range, job_.output), job_.output,
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    if (!encoder) {
        return std::unexpected(encoder.error());
    }
    render->m_encoder = std::move(*encoder);
    render->m_target = LoadRenderTexture(width, height);
    if (render->m_target.id == 0) {
        render->stop();
        return std::unexpected(std::format("Can't draw a frame of {}x{}", width, height));
    }
    return render;
}

OverlayRender::~OverlayRender()
{
    if (!over()) {
        stop();
    }
    if (m_target.id != 0) {
        UnloadRenderTexture(m_target);
    }
}

double OverlayRender::progress() const
{
    return (m_frames == 0) ? 1.0 : static_cast<double>(m_frame) / static_cast<double>(m_frames);
}

void OverlayRender::step(double budget)
{
    if (over()) {
        return;
    }
    auto const until = GetTime() + budget;
    while (m_frame < m_frames && m_encoder->can_write() && GetTime() < until) {
        render_frame();
    }
    // A frame ffmpeg didn't take, or the last: finish() says how it went.
    if (m_frame >= m_frames || m_encoder->failed()) {
        m_result = m_encoder->finish();
    }
}

void OverlayRender::render_frame()
{
    auto const [width, height] = m_job.settings.resolution;
    auto const values = values_at_time(m_job.activity->activity, frame_time(m_job.range, m_job.settings.fps, m_frame));
    BeginTextureMode(m_target);
    ClearBackground(BLANK);
    // The colors are blended as usual, but the alpha adds up, rather than
    // being blended too: the background is transparent, not black. This
    // leaves the colors premultiplied, which the encoder undoes.
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    m_renderer.draw(values, Rectangle { .x = 0, .y = 0, .width = static_cast<float>(width), .height = static_cast<float>(height) });
    EndBlendMode();
    rlDrawRenderBatchActive();
    auto frame = m_encoder->buffer();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frame.data());
    EndTextureMode();
    m_encoder->write(std::move(frame));
    ++m_frame;
}

void OverlayRender::stop()
{
    if (over()) {
        return;
    }
    m_encoder->stop();
    m_result = std::unexpected("Stopped");
}

}
