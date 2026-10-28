/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ClipShader.h>
#include <Logging.h>

namespace ST {

namespace {

// As raylib's own vertex shader, but it also passes on where each vertex
// is drawn, which the fragment shader measures the shape from.
constexpr char const *VertexShader = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec2 fragPosition;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPosition = vertexPosition.xy;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// How far outside the rounded rectangle the pixel is, negative inside: the
// signed distance, which fades the pixel out over the edge's last pixel.
constexpr char const *FragmentShader = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec2 fragPosition;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 clipBox;
uniform float clipRadius;
out vec4 finalColor;

void main()
{
    vec2 halfSize = clipBox.zw * 0.5;
    vec2 corner = abs(fragPosition - (clipBox.xy + halfSize)) - halfSize + vec2(clipRadius);
    float outside = length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - clipRadius;
    vec4 color = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    finalColor = vec4(color.rgb, color.a * clamp(0.5 - outside, 0.0, 1.0));
}
)";

}

ClipShader::~ClipShader()
{
    if (m_loaded) {
        UnloadShader(m_loaded->shader);
    }
}

void ClipShader::begin(Rectangle box, float radius)
{
    if (!m_loaded) {
        auto const shader = LoadShaderFromMemory(VertexShader, FragmentShader);
        m_loaded = Loaded {
            .shader = shader,
            .box = GetShaderLocation(shader, "clipBox"),
            .radius = GetShaderLocation(shader, "clipRadius"),
            .valid = IsShaderValid(shader),
        };
        if (!m_loaded->valid) {
            warning(Widget, "The clip shader didn't compile; clipping to rectangles instead");
        }
    }
    if (!m_loaded->valid) {
        BeginScissorMode(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.width), static_cast<int>(box.height));
        return;
    }
    float const clip_box[] { box.x, box.y, box.width, box.height };
    SetShaderValue(m_loaded->shader, m_loaded->box, clip_box, SHADER_UNIFORM_VEC4);
    SetShaderValue(m_loaded->shader, m_loaded->radius, &radius, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(m_loaded->shader);
}

void ClipShader::end()
{
    if (!m_loaded || !m_loaded->valid) {
        EndScissorMode();
        return;
    }
    EndShaderMode();
}

}
