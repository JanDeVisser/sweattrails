/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Clips what's drawn to a rounded rectangle, or a circle, which is one
 * with half its side for a radius. raylib can only clip to a rectangle,
 * with scissor mode; drawing into a render texture of its own and that
 * through a mask doesn't work while drawing into one already, since
 * texture modes don't nest. So a shader fades out what's outside the
 * shape, over its last pixel, which keeps the edge smooth.
 */

#pragma once

#include <optional>

#include <raylib.h>

namespace ST {

struct ClipShader {
    ClipShader() = default;
    ~ClipShader();
    ClipShader(ClipShader const &) = delete;
    ClipShader &operator=(ClipShader const &) = delete;

    // What's drawn from begin() to end() is clipped to `box`, with corners
    // rounded by `radius`, in the coordinates it's drawn in. The shader is
    // loaded the first time, which needs the window.
    void begin(Rectangle box, float radius);
    void end();

private:
    struct Loaded {
        Shader shader { };
        int    box { -1 };
        int    radius { -1 };
    };
    std::optional<Loaded> m_loaded { };
};

}
