/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The pictures an overlay's images show, decoded from their files. No
 * window is needed: only making a texture of one does. See
 * specs/19-overlay-image.md.
 */

#pragma once

#include <filesystem>
#include <optional>

#include <raylib.h>

namespace ST {

// The picture in the file, as RGBA, with its colors bled (see below).
// None if raylib can't read it. The caller unloads it.
std::optional<Image> decode_picture(std::filesystem::path const &path);

// Gives each transparent pixel the color of the shown pixels nearest it,
// leaving it transparent. Mipmaps average the colors without their alpha,
// so the black of a PNG's transparent pixels would darken its edges where
// it's drawn much smaller than its file, as an icon is.
void bleed_colors(Image &image);

}
