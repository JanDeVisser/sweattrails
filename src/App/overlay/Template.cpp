/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>

#include <overlay/Template.h>

namespace ST {

namespace {

std::expected<Placeholder, std::string> parse_placeholder(std::string_view text)
{
    std::string_view name = text;
    std::string_view spec { };
    if (auto const colon = text.find(':'); colon != std::string_view::npos) {
        name = text.substr(0, colon);
        spec = text.substr(colon + 1);
    }
    Placeholder ret { .spec = std::string { spec } };
    if (name.ends_with(".unit")) {
        name.remove_suffix(5);
        ret.unit = true;
        if (!spec.empty()) {
            return std::unexpected(std::format("`{{{}}}`: a unit has no format", text));
        }
    }
    if (is_later_field(name)) {
        return ret;
    }
    ret.field = overlay_field(name);
    if (!ret.field) {
        return std::unexpected(std::format("`{}` isn't a field", name));
    }
    // The duration specs, such as `hms`, come with the other fields.
    if (!spec.empty() && ret.field != OverlayField::elapsed && !is_valid_spec(spec)) {
        return std::unexpected(std::format("`{}` isn't a format for `{}`", spec, name));
    }
    return ret;
}

}

std::expected<Template, std::string> parse_template(std::string_view text)
{
    Template    ret;
    std::string literal;
    auto const  flush = [&ret, &literal]() {
        if (!literal.empty()) {
            ret.parts.emplace_back(std::move(literal));
            literal.clear();
        }
    };
    for (size_t ix = 0; ix < text.length(); ++ix) {
        auto const ch = text[ix];
        auto const next = (ix + 1 < text.length()) ? text[ix + 1] : '\0';
        if ((ch == '{' && next == '{') || (ch == '}' && next == '}')) {
            literal += ch;
            ++ix;
            continue;
        }
        if (ch == '}') {
            return std::unexpected(std::string { "a `}` without a `{`" });
        }
        if (ch != '{') {
            literal += ch;
            continue;
        }
        auto const close = text.find('}', ix);
        if (close == std::string_view::npos) {
            return std::unexpected(std::string { "a `{` without a `}`" });
        }
        auto placeholder = parse_placeholder(text.substr(ix + 1, close - ix - 1));
        if (!placeholder) {
            return std::unexpected(placeholder.error());
        }
        flush();
        ret.parts.emplace_back(std::move(*placeholder));
        ix = close;
    }
    flush();
    return ret;
}

std::string fill_template(Template const &text, Activity const &activity, size_t record, Units units, std::string_view missing)
{
    std::string ret;
    for (auto const &part : text.parts) {
        if (auto const *literal = std::get_if<std::string>(&part)) {
            ret += *literal;
            continue;
        }
        auto const &placeholder = std::get<Placeholder>(part);
        if (!placeholder.field) {
            ret += placeholder.unit ? "" : missing;
            continue;
        }
        auto const field = *placeholder.field;
        auto const unit = default_unit(field, units, activity.sport);
        if (placeholder.unit) {
            ret += unit.name;
            continue;
        }
        auto const spec = placeholder.spec.empty() ? default_spec(field) : std::string_view { placeholder.spec };
        ret += format_field(field, field_value(activity, record, field), unit, spec, missing);
    }
    return ret;
}

}
