/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string_view>

#include <raylib.h>

namespace ST {

// `#rrggbb`, or `#rrggbbaa` with an alpha, in upper or lower case. Nothing
// if it's neither.
std::optional<Color> parse_color(std::string_view text);

}
