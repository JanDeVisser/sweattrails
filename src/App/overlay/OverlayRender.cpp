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

namespace {

// A frame is drawn this many times its size, and scaled down, which
// smooths the edges of shapes and text: raylib draws them without.
constexpr int Supersampling = 2;

}

OverlayRender::OverlayRender(RenderJob job, FontFinder find, ImageFinder find_image, TileFetcher cached, TileScheduler schedule)
    : m_job(std::move(job))
    , m_source(m_job.activity->activity)
    , m_tiles(std::move(cached), std::move(schedule))
    , m_renderer(std::move(find), std::move(find_image))
{
    m_frames = frame_count(m_job.range, m_job.settings.fps);
    m_renderer.set_tiles(&m_tiles, Supersampling);
}

std::expected<std::unique_ptr<OverlayRender>, std::string> OverlayRender::start(RenderJob job, FontFinder find, ImageFinder find_image, TileFetcher cached, TileScheduler schedule)
{
    std::unique_ptr<OverlayRender> render { new OverlayRender(std::move(job), std::move(find), std::move(find_image), std::move(cached), std::move(schedule)) };
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
    render->m_large = LoadRenderTexture(width * Supersampling, height * Supersampling);
    render->m_target = LoadRenderTexture(width, height);
    if (render->m_large.id == 0 || render->m_target.id == 0) {
        render->stop();
        return std::unexpected(std::format("Can't draw a frame of {}x{}", width, height));
    }
    SetTextureFilter(render->m_large.texture, TEXTURE_FILTER_BILINEAR);
    return render;
}

OverlayRender::~OverlayRender()
{
    if (!over()) {
        stop();
    }
    if (m_large.id != 0) {
        UnloadRenderTexture(m_large);
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
        // A frame waits for its tiles, which are only fetched once a frame
        // shows them: OSM doesn't allow fetching them ahead.
        auto const seconds = frame_time(m_job.range, m_job.settings.fps, m_frame);
        auto const values = values_at_time(m_source, seconds);
        auto const time = VideoTime { .at = seconds - m_job.range.from, .length = m_job.range.to - m_job.range.from };
        m_waiting = !m_tiles.request(m_renderer.tiles_needed(values, large_area(), time));
        if (m_waiting) {
            return;
        }
        render_frame(values, time);
        // A font raylib couldn't load: the frames would be blank.
        if (auto const &error = m_renderer.error(); error) {
            m_encoder->stop();
            m_result = std::unexpected(*error);
            return;
        }
    }
    // A frame ffmpeg didn't take, or the last: the encoder waits for
    // ffmpeg to finish the file, and then says how it went.
    if (m_frame >= m_frames || m_encoder->failed()) {
        m_encoder->close();
        if (m_encoder->result()) {
            m_result = m_encoder->finish();
        }
    }
}

Rectangle OverlayRender::large_area() const
{
    auto const [width, height] = m_job.settings.resolution;
    return Rectangle { .x = 0, .y = 0, .width = static_cast<float>(width * Supersampling), .height = static_cast<float>(height * Supersampling) };
}

void OverlayRender::render_frame(FieldValues const &values, VideoTime time)
{
    auto const [width, height] = m_job.settings.resolution;
    auto const large = large_area();
    BeginTextureMode(m_large);
    ClearBackground(BLANK);
    // The colors are blended as usual, but the alpha adds up, rather than
    // being blended too: the background is transparent, not black. This
    // leaves the colors premultiplied, which the encoder undoes.
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    m_renderer.draw(values, large, time);
    EndBlendMode();
    EndTextureMode();

    // Bilinear at half the size averages each 2x2 block. Premultiplied
    // colors average as they should, and onto the cleared frame the
    // premultiplied blend copies them. A source upside down keeps the
    // frame as it was before scaling: upside down, as the encoder expects.
    BeginTextureMode(m_target);
    ClearBackground(BLANK);
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    DrawTexturePro(m_large.texture, Rectangle { .x = 0, .y = 0, .width = large.width, .height = -large.height },
        Rectangle { .x = 0, .y = 0, .width = static_cast<float>(width), .height = static_cast<float>(height) }, Vector2 { }, 0, WHITE);
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
    // A render that couldn't start has no encoder.
    if (m_encoder) {
        m_encoder->stop();
    }
    m_result = std::unexpected("Stopped");
}

}
