/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <map>

#include <Utf8.h>

#include <Colors.h>

#include <overlay/Layout.h>

namespace ST {

namespace {

using Names = std::initializer_list<std::string_view>;

// Every element has these, also those of later slices.
constexpr Names ElementProperties { "type", "style", "at", "size", "visible", "fade", "when", "smooth", "interpolate" };

// The style properties. The box ones are for later slices' elements, but
// defaults and styles may have them, so every element accepts them.
constexpr Names TextStyleProperties { "font", "font_size", "color", "shadow" };
constexpr Names BoxStyleProperties { "background", "radius", "padding", "border" };

constexpr Names TextProperties { "text", "align" };
constexpr Names ValueProperties { "field", "format", "unit", "unit_style", "missing", "color_by" };

constexpr Names LaterTypes { "gauge", "graph", "profile", "map", "image", "group" };

// The properties of later slices' elements, from the design. Defaults and
// styles may have them, for those elements.
constexpr Names LaterProperties {
    "shape",
    "min",
    "max",
    "track",
    "fill",
    "thickness", // gauge
    "x_axis",
    "window",
    "line",
    "cursor", // graph
    "extent",
    "done",
    "remaining",
    "marker",
    "marker_label",
    "scale",
    "axis_labels",
    "laps", // profile
    "clip",
    "view",
    "zoom",
    "rotate",
    "tiles",
    "attribution", // map
    "src",
    "tint",     // image
    "children", // group
};

constexpr Names LayoutProperties { "version", "design_size", "units", "defaults", "styles", "elements" };

using Error = std::unexpected<LayoutError>;

Error error(std::string message)
{
    return Error { LayoutError { .message = std::move(message) } };
}

bool contains(Names names, std::string_view name)
{
    return std::ranges::contains(names, name);
}

std::optional<std::string> string_of(JSONValue const &json)
{
    if (!json.is_string()) {
        return { };
    }
    std::string ret;
    (void) json.convert(ret);
    return ret;
}

std::optional<double> number_of(JSONValue const &json)
{
    if (!json.is_integer() && !json.is_double()) {
        return { };
    }
    double ret { 0 };
    (void) json.convert(ret);
    return ret;
}

// Checks that every property of `json` is in one of `allowed`.
std::expected<void, LayoutError> check_properties(JSONValue const &json, std::string_view what, std::initializer_list<Names> allowed)
{
    for (auto it = json.obj_begin(); it != json.obj_end(); ++it) {
        auto const &name = it->first;
        if (std::ranges::none_of(allowed, [&name](Names names) { return contains(names, name); })) {
            return error(std::format("\"{}\" isn't a property of {}", name, what));
        }
    }
    return { };
}

std::expected<double, LayoutError> number(JSONValue const &json, std::string_view property)
{
    if (auto const n = number_of(json); n) {
        return *n;
    }
    return error(std::format("\"{}\" isn't a number", property));
}

std::expected<std::string, LayoutError> string(JSONValue const &json, std::string_view property)
{
    if (auto s = string_of(json); s) {
        return *s;
    }
    return error(std::format("\"{}\" isn't a string", property));
}

std::expected<Color, LayoutError> color(JSONValue const &json, std::string_view property)
{
    auto const text = string_of(json);
    if (!text) {
        return error(std::format("\"{}\" isn't a color: give one as \"#rrggbb\" or \"#rrggbbaa\"", property));
    }
    if (auto const c = parse_color(*text); c) {
        return *c;
    }
    return error(std::format("\"{}\": `{}` isn't a color: give one as \"#rrggbb\" or \"#rrggbbaa\"", property, *text));
}

std::expected<Vector2, LayoutError> pair(JSONValue const &json, std::string_view property)
{
    if (!json.is_array() || json.size() != 2) {
        return error(std::format("\"{}\" isn't a pair of numbers, such as [2, 2]", property));
    }
    std::array<float, 2> xy { };
    auto                 ix = 0uz;
    for (auto const &n : json) {
        auto const value = number_of(n);
        if (!value) {
            return error(std::format("\"{}\" isn't a pair of numbers, such as [2, 2]", property));
        }
        xy[ix++] = static_cast<float>(*value);
    }
    return Vector2 { .x = xy[0], .y = xy[1] };
}

std::expected<Vector2, LayoutError> width_height(JSONValue const &json, std::string_view property)
{
    if (!json.is_object()) {
        return error(std::format("\"{}\" isn't an object with a width and a height", property));
    }
    TRY(check_properties(json, std::format("\"{}\"", property), { { "width", "height" } }));
    auto const width = json.get("width");
    auto const height = json.get("height");
    if (!width || !height) {
        return error(std::format("\"{}\" needs a width and a height", property));
    }
    return Vector2 {
        .x = static_cast<float>(TRY_EVAL(number(*width, "width"))),
        .y = static_cast<float>(TRY_EVAL(number(*height, "height"))),
    };
}

std::expected<Coordinate, LayoutError> coordinate(JSONValue const &json, std::string_view property)
{
    if (auto const n = number_of(json); n) {
        return Coordinate { .value = *n };
    }
    auto const text = string_of(json);
    if (text && text->ends_with('%')) {
        double     value { 0 };
        auto const digits = std::string_view { *text }.substr(0, text->length() - 1);
        auto const [end, err] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (err == std::errc { } && end == digits.data() + digits.size() && !digits.empty()) {
            return Coordinate { .value = value, .percent = true };
        }
    }
    return error(std::format("\"{}\" isn't a number or a percentage, such as \"50%\"", property));
}

std::expected<Anchor, LayoutError> anchor(JSONValue const &json)
{
    static std::map<std::string, Anchor, std::less<>> const anchors {
        { "top-left", Anchor::TopLeft },
        { "top", Anchor::Top },
        { "top-right", Anchor::TopRight },
        { "left", Anchor::Left },
        { "center", Anchor::Center },
        { "right", Anchor::Right },
        { "bottom-left", Anchor::BottomLeft },
        { "bottom", Anchor::Bottom },
        { "bottom-right", Anchor::BottomRight },
    };
    if (auto const text = string_of(json); text) {
        if (auto const it = anchors.find(*text); it != anchors.end()) {
            return it->second;
        }
    }
    return error("\"anchor\" isn't one of top-left, top, top-right, left, center, right, bottom-left, bottom, bottom-right");
}

std::expected<Placement, LayoutError> placement(JSONValue const &element)
{
    Placement ret { };
    if (auto const at = element.get("at"); at) {
        if (!at->is_object()) {
            return error("\"at\" isn't an object with x and y");
        }
        TRY(check_properties(*at, "\"at\"", { { "x", "y", "anchor" } }));
        if (auto const x = at->get("x"); x) {
            ret.x = TRY_EVAL(coordinate(*x, "x"));
        }
        if (auto const y = at->get("y"); y) {
            ret.y = TRY_EVAL(coordinate(*y, "y"));
        }
        if (auto const a = at->get("anchor"); a) {
            ret.anchor = TRY_EVAL(anchor(*a));
        }
    }
    if (auto const size = element.get("size"); size) {
        ret.size = TRY_EVAL(width_height(*size, "size"));
    }
    return ret;
}

// The text style properties of `json`, over `base`.
std::expected<TextStyle, LayoutError> text_style(JSONValue const &json, TextStyle base = { })
{
    auto ret = std::move(base);
    if (auto const font = json.get("font"); font) {
        ret.font = TRY_EVAL(string(*font, "font"));
    }
    if (auto const size = json.get("font_size"); size) {
        ret.font_size = static_cast<float>(TRY_EVAL(number(*size, "font_size")));
        if (ret.font_size <= 0) {
            return error("\"font_size\" isn't more than 0");
        }
    }
    if (auto const c = json.get("color"); c) {
        ret.color = TRY_EVAL(color(*c, "color"));
    }
    if (auto const shadow = json.get("shadow"); shadow) {
        if (shadow->is_null()) {
            ret.shadow.reset();
        } else if (!shadow->is_object()) {
            return error("\"shadow\" isn't an object with a color and an offset");
        } else {
            TRY(check_properties(*shadow, "\"shadow\"", { { "color", "offset" } }));
            // Over the base's shadow, as the cascade merges objects: a
            // unit_style can change only the offset.
            auto s = ret.shadow.value_or(Shadow { });
            if (auto const c = shadow->get("color"); c) {
                s.color = TRY_EVAL(color(*c, "shadow.color"));
            }
            if (auto const offset = shadow->get("offset"); offset) {
                s.offset = TRY_EVAL(pair(*offset, "shadow.offset"));
            }
            ret.shadow = s;
        }
    }
    return ret;
}

std::expected<TextElement, LayoutError> text_element(JSONValue const &json)
{
    TextElement ret { .at = TRY_EVAL(placement(json)), .style = TRY_EVAL(text_style(json)) };
    auto const  text = json.get("text");
    if (!text) {
        return error("a text needs a \"text\"");
    }
    auto parsed = parse_template(TRY_EVAL(string(*text, "text")));
    if (!parsed) {
        return error(std::format("\"text\": {}", parsed.error()));
    }
    ret.text = std::move(*parsed);
    if (auto const align = json.get("align"); align) {
        auto const a = string_of(*align).value_or("");
        if (a == "left") {
            ret.align = Align::Left;
        } else if (a == "center") {
            ret.align = Align::Center;
        } else if (a == "right") {
            ret.align = Align::Right;
        } else {
            return error("\"align\" isn't left, center or right");
        }
    }
    return ret;
}

// `"{:.1f}"`: a format with one placeholder and nothing else, for its spec.
std::expected<std::string, LayoutError> value_spec(JSONValue const &json, OverlayField field)
{
    auto const format = TRY_EVAL(string(json, "format"));
    auto       spec = std::string_view { format };
    if (!spec.starts_with('{') || !spec.ends_with('}')) {
        return error(std::format("\"format\": `{}` isn't a format such as \"{{:.1f}}\"", format));
    }
    spec = spec.substr(1, spec.length() - 2);
    if (spec.starts_with(':')) {
        spec.remove_prefix(1);
    } else if (!spec.empty()) {
        return error(std::format("\"format\": `{}` isn't a format such as \"{{:.1f}}\"", format));
    }
    // The duration specs, such as `hms`, come with the other fields.
    if (field != OverlayField::elapsed && !is_valid_spec(spec)) {
        return error(std::format("\"format\": `{}` isn't a format for {}", format, OverlayField_name(field)));
    }
    return std::string { spec };
}

std::expected<ValueElement, LayoutError> value_element(JSONValue const &json)
{
    ValueElement ret { .at = TRY_EVAL(placement(json)), .style = TRY_EVAL(text_style(json)) };
    auto const   field = json.get("field");
    if (!field) {
        return error("a value needs a \"field\"");
    }
    auto const name = TRY_EVAL(string(*field, "field"));
    if (is_later_field(name)) {
        return error(std::format("\"field\": `{}` can't be shown as a value yet", name));
    }
    auto const f = overlay_field(name);
    if (!f) {
        return error(std::format("\"field\": `{}` isn't a field", name));
    }
    ret.field = *f;
    if (auto const format = json.get("format"); format) {
        ret.spec = TRY_EVAL(value_spec(*format, ret.field));
    }
    if (auto const unit = json.get("unit"); unit) {
        auto const u = TRY_EVAL(string(*unit, "unit"));
        if (u != "auto") {
            ret.unit = field_unit(ret.field, u);
            if (!ret.unit) {
                return error(std::format("\"unit\": {} can't be shown in `{}`", name, u));
            }
        }
    }
    ret.unit_style = ret.style;
    if (auto const unit_style = json.get("unit_style"); unit_style) {
        if (!unit_style->is_object()) {
            return error("\"unit_style\" isn't an object");
        }
        TRY(check_properties(*unit_style, "\"unit_style\"", { TextStyleProperties }));
        ret.unit_style = TRY_EVAL(text_style(*unit_style, ret.style));
    }
    if (auto const missing = json.get("missing"); missing) {
        ret.missing = TRY_EVAL(string(*missing, "missing"));
        ret.hide_when_missing = ret.missing == "hide";
    }
    return ret;
}

// The element's properties after the cascade: the defaults, its styles in
// order, and its own.
std::expected<JSONValue, LayoutError> cascaded(JSONValue const &element, JSONValue const &defaults, JSONValue const &styles)
{
    auto ret = defaults;
    if (auto const style = element.get("style"); style) {
        std::vector<std::string> names;
        if (auto const name = string_of(*style); name) {
            names.push_back(*name);
        } else if (style->is_array()) {
            for (auto const &n : *style) {
                names.push_back(TRY_EVAL(string(n, "style")));
            }
        } else {
            return error("\"style\" isn't a name or a list of names");
        }
        for (auto const &name : names) {
            auto const s = styles.get(name);
            if (!s || !s->is_object()) {
                return error(std::format("\"style\": there's no style `{}`", name));
            }
            ret = cascade(std::move(ret), *s);
        }
    }
    return cascade(std::move(ret), element);
}

// Where the anchor is across an element, from 0 at its left to 1 at its
// right, and down it.
float fraction_x(Anchor a)
{
    switch (a) {
    case Anchor::Top:
    case Anchor::Center:
    case Anchor::Bottom:
        return 0.5f;
    case Anchor::TopRight:
    case Anchor::Right:
    case Anchor::BottomRight:
        return 1.0f;
    default:
        return 0.0f;
    }
}

float fraction_y(Anchor a)
{
    switch (a) {
    case Anchor::Left:
    case Anchor::Center:
    case Anchor::Right:
        return 0.5f;
    case Anchor::BottomLeft:
    case Anchor::Bottom:
    case Anchor::BottomRight:
        return 1.0f;
    default:
        return 0.0f;
    }
}

std::expected<std::optional<OverlayElement>, LayoutError> element(JSONValue const &json, JSONValue const &defaults, JSONValue const &styles)
{
    if (!json.is_object()) {
        return error("it isn't an object");
    }
    auto const type = json.get("type");
    if (!type) {
        return error("it has no \"type\"");
    }
    auto const name = TRY_EVAL(string(*type, "type"));
    if (contains(LaterTypes, name)) {
        return std::optional<OverlayElement> { };
    }
    // A typo is in the element's own properties: those it takes from the
    // defaults and its styles were checked with them, and may be for
    // elements of another type, such as a text's align.
    if (name == "text") {
        TRY(check_properties(json, "a text", { ElementProperties, TextStyleProperties, BoxStyleProperties, TextProperties }));
        return std::optional<OverlayElement> { TRY_EVAL(text_element(TRY_EVAL(cascaded(json, defaults, styles)))) };
    }
    if (name == "value") {
        TRY(check_properties(json, "a value", { ElementProperties, TextStyleProperties, BoxStyleProperties, ValueProperties }));
        return std::optional<OverlayElement> { TRY_EVAL(value_element(TRY_EVAL(cascaded(json, defaults, styles)))) };
    }
    return error(std::format("\"type\": `{}` isn't an element type", name));
}

}

std::string LayoutError::to_string() const
{
    if (element) {
        return std::format("element {}: {}", *element, message);
    }
    return message;
}

JSONValue cascade(JSONValue base, JSONValue const &over)
{
    if (!base.is_object() || !over.is_object()) {
        return over;
    }
    for (auto it = over.obj_begin(); it != over.obj_end(); ++it) {
        auto const &[name, value] = *it;
        auto mine = base.get(name);
        base.set(name, (mine && mine->is_object() && value.is_object()) ? cascade(std::move(*mine), value) : value);
    }
    return base;
}

std::expected<OverlayLayout, LayoutError> decode_layout(JSONValue const &json)
{
    if (!json.is_object()) {
        return error("the layout isn't a JSON object");
    }
    TRY(check_properties(json, "a layout", { LayoutProperties }));
    if (auto const version = json.get("version"); version && number_of(*version) != 1.0) {
        return error("\"version\": only version 1 is known");
    }
    OverlayLayout ret { };
    if (auto const size = json.get("design_size"); size) {
        ret.design_size = TRY_EVAL(width_height(*size, "design_size"));
        if (ret.design_size.x <= 0 || ret.design_size.y <= 0) {
            return error("\"design_size\" isn't more than 0 wide and high");
        }
    }
    if (auto const units = json.get("units"); units) {
        auto const u = string_of(*units).value_or("");
        if (u == "metric") {
            ret.units = Units::Metric;
        } else if (u == "imperial") {
            ret.units = Units::Imperial;
        } else {
            return error("\"units\" isn't metric or imperial");
        }
    }
    auto const defaults = json.get_with_default("defaults");
    auto const styles = json.get_with_default("styles");
    if (!defaults.is_object()) {
        return error("\"defaults\" isn't an object");
    }
    if (!styles.is_object()) {
        return error("\"styles\" isn't an object");
    }
    // What defaults and styles may hold: any element's properties but its
    // type and style.
    auto const style_properties = { TextStyleProperties, BoxStyleProperties, TextProperties, ValueProperties, LaterProperties,
        Names { "at", "size", "visible", "fade", "when", "smooth", "interpolate" } };
    TRY(check_properties(defaults, "the defaults", style_properties));
    for (auto it = styles.obj_begin(); it != styles.obj_end(); ++it) {
        if (!it->second.is_object()) {
            return error(std::format("the style `{}` isn't an object", it->first));
        }
        TRY(check_properties(it->second, std::format("the style `{}`", it->first), style_properties));
    }
    auto const elements = json.get_with_default("elements", JSONType::Array);
    if (!elements.is_array()) {
        return error("\"elements\" isn't a list");
    }
    auto ix = 0uz;
    for (auto const &e : elements) {
        ++ix;
        auto decoded = element(e, defaults, styles);
        if (!decoded) {
            auto err = decoded.error();
            err.element = ix;
            return std::unexpected(err);
        }
        if (*decoded) {
            std::visit([ix](auto &el) { el.number = ix; }, **decoded);
            ret.elements.push_back(std::move(**decoded));
        } else {
            ++ret.skipped;
        }
    }
    return ret;
}

float layout_scale(Vector2 design_size, Vector2 canvas)
{
    return std::min(canvas.x / design_size.x, canvas.y / design_size.y);
}

Rectangle place(Placement const &at, Vector2 size, Vector2 canvas, float scale)
{
    auto const resolve = [scale](Coordinate const &c, float extent) {
        auto const pixels = static_cast<float>(c.percent ? c.value / 100.0 * extent : c.value * scale);
        return (c.value < 0) ? extent + pixels : pixels;
    };
    return Rectangle {
        .x = resolve(at.x, canvas.x) - fraction_x(at.anchor) * size.x,
        .y = resolve(at.y, canvas.y) - fraction_y(at.anchor) * size.y,
        .width = size.x,
        .height = size.y,
    };
}

size_t element_number(OverlayElement const &element)
{
    return std::visit([](auto const &e) { return e.number; }, element);
}

Vector2 place_in_box(Vector2 box, Vector2 size, Anchor anchor, std::optional<Align> align)
{
    auto x = fraction_x(anchor);
    if (align) {
        x = (*align == Align::Left) ? 0.0f : (*align == Align::Center) ? 0.5f
                                                                       : 1.0f;
    }
    return Vector2 { .x = x * (box.x - size.x), .y = fraction_y(anchor) * (box.y - size.y) };
}

std::set<int> layout_codepoints(OverlayLayout const &layout)
{
    std::set<int> ret;
    auto const    add = [&ret](std::string_view text) {
        for (auto const ch : to_wstring(text).value_or(std::wstring { })) {
            if (ch > 126) {
                ret.insert(static_cast<int>(ch));
            }
        }
    };
    for (auto const &element : layout.elements) {
        if (auto const *text = std::get_if<TextElement>(&element)) {
            for (auto const &part : text->text.parts) {
                if (auto const *literal = std::get_if<std::string>(&part)) {
                    add(*literal);
                }
            }
            // Fields of later slices show as missing.
            add("--");
        } else if (auto const *value = std::get_if<ValueElement>(&element)) {
            add(value->missing);
            if (value->unit) {
                add(value->unit->name);
            }
        }
    }
    return ret;
}

}
