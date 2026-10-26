/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <charconv>

#include <Colors.h>

namespace ST {

std::optional<Color> parse_color(std::string_view text)
{
    if (!text.starts_with('#') || (text.length() != 7 && text.length() != 9)) {
        return { };
    }
    auto const component = [text](size_t ix) -> std::optional<unsigned char> {
        auto const    digits = text.substr(1 + 2 * ix, 2);
        unsigned char value { 0 };
        auto const [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
        if (error != std::errc { } || end != digits.data() + digits.size()) {
            return { };
        }
        return value;
    };
    auto const r = component(0);
    auto const g = component(1);
    auto const b = component(2);
    auto const a = (text.length() == 9) ? component(3) : std::optional<unsigned char> { 255 };
    if (!r || !g || !b || !a) {
        return { };
    }
    return Color { .r = *r, .g = *g, .b = *b, .a = *a };
}

}
