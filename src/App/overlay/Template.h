/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A text element's text, with placeholders filled in on every frame:
 * "Lap {lap} · {distance:.1f} {distance.unit}".
 */

#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <overlay/Fields.h>

namespace ST {

struct Placeholder {
    OverlayField field { OverlayField::speed };
    std::string  spec { }; // The default spec if empty
    bool         unit { false };
};

using TemplatePart = std::variant<std::string, Placeholder>;

struct Template {
    std::vector<TemplatePart> parts { };
};

// `{field}`, `{field:spec}` and `{field.unit}`, with `{{` and `}}` for
// braces. An error, as a message, for an unknown field, a bad spec, or a
// brace that isn't closed or opened.
std::expected<Template, std::string> parse_template(std::string_view text);

// The text for the frame's values, with the fields in their default units.
std::string fill_template(Template const &text, FieldValues const &values, Units units, std::string_view missing);

}
