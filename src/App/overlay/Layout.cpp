/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <format>
#include <map>

#include <StringUtil.h>
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
constexpr Names ValueProperties { "field", "format", "unit", "unit_style", "missing", "color_by", "window" };

// Grade's window, in m: shorter is noise, longer smooths the ramps away.
constexpr double ShortestWindow = 10;
constexpr double LongestWindow = 200;

constexpr Names GaugeProperties { "field", "unit", "shape", "min", "max", "track", "fill", "thickness", "color_by" };
constexpr Names GraphProperties { "field", "unit", "x_axis", "window", "min", "max", "line", "fill", "cursor" };

// `color_by_grade` is deferred: taken, and not drawn yet.
constexpr Names ProfileProperties { "extent", "smooth", "min", "max", "min_span", "done", "remaining", "marker",
    "marker_label", "axis_labels", "laps", "color_by_grade" };

// `rotate` is deferred: taken, and not done yet.
constexpr Names MapProperties { "view", "zoom", "clip", "track", "marker", "tiles", "attribution", "rotate" };

constexpr Names ImageProperties { "src", "tint" };

constexpr Names LaterTypes { "group" };

// The properties of later slices' elements, from the design. Defaults and
// styles may have them, for those elements.
constexpr Names LaterProperties {
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

std::expected<bool, LayoutError> boolean(JSONValue const &json, std::string_view property)
{
    bool ret { false };
    if (!json.is_boolean() || !json.convert(ret)) {
        return error(std::format("\"{}\" isn't true or false", property));
    }
    return ret;
}

std::expected<double, LayoutError> not_negative(JSONValue const &json, std::string_view property)
{
    auto const ret = TRY_EVAL(number(json, property));
    if (ret < 0) {
        return error(std::format("\"{}\" is less than 0", property));
    }
    return ret;
}

// `{ "color": "#rrggbbaa", "width": 3 }`, over `base`.
std::expected<Stroke, LayoutError> stroke(JSONValue const &json, std::string_view property, Stroke base = { })
{
    if (!json.is_object()) {
        return error(std::format("\"{}\" isn't a line, such as {{ \"color\": \"#ffffff\", \"width\": 2 }}", property));
    }
    TRY(check_properties(json, std::format("\"{}\"", property), { { "color", "width" } }));
    auto ret = base;
    if (auto const c = json.get("color"); c) {
        ret.color = TRY_EVAL(color(*c, std::format("{}.color", property)));
    }
    if (auto const width = json.get("width"); width) {
        ret.width = static_cast<float>(TRY_EVAL(number(*width, std::format("{}.width", property))));
        if (ret.width <= 0) {
            return error(std::format("\"{}.width\" isn't more than 0", property));
        }
    }
    return ret;
}

std::expected<Panel, LayoutError> panel(JSONValue const &json)
{
    Panel ret { };
    if (auto const background = json.get("background"); background) {
        ret.background = TRY_EVAL(color(*background, "background"));
    }
    if (auto const radius = json.get("radius"); radius) {
        ret.radius = static_cast<float>(TRY_EVAL(not_negative(*radius, "radius")));
    }
    if (auto const padding = json.get("padding"); padding) {
        ret.padding = static_cast<float>(TRY_EVAL(not_negative(*padding, "padding")));
    }
    if (auto const border = json.get("border"); border) {
        ret.border = TRY_EVAL(stroke(*border, "border"));
    }
    return ret;
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
    TextElement ret { .at = TRY_EVAL(placement(json)), .panel = TRY_EVAL(panel(json)), .style = TRY_EVAL(text_style(json)) };
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
    if (!is_valid_spec(field, spec)) {
        return error(std::format("\"format\": `{}` isn't a format for {}", format, OverlayField_name(field)));
    }
    return std::string { spec };
}

std::expected<std::vector<Threshold>, LayoutError> color_by(JSONValue const &json, OverlayField field)
{
    auto const usage = std::string { "\"color_by\" needs \"thresholds\", such as [[0, \"#8ecae6\"], [150, \"#f94144\"]]" };
    if (!json.is_object()) {
        return error(usage);
    }
    TRY(check_properties(json, "\"color_by\"", { { "thresholds", "zones", "colors" } }));
    if (json.get("zones")) {
        return error("\"color_by\": zones need an athlete profile, which there isn't yet: give \"thresholds\"");
    }
    if (field_kind(field) != FieldKind::Number) {
        return error(std::format("\"color_by\": {} isn't a number", OverlayField_name(field)));
    }
    auto const thresholds = json.get("thresholds");
    if (!thresholds || !thresholds->is_array() || thresholds->size() == 0) {
        return error(usage);
    }
    std::vector<Threshold> ret;
    for (auto const &t : *thresholds) {
        if (!t.is_array() || t.size() != 2) {
            return error(usage);
        }
        auto const from = number_of(*t.get(0));
        if (!from) {
            return error(usage);
        }
        auto const c = TRY_EVAL(color(*t.get(1), "color_by"));
        if (!ret.empty() && *from <= ret.back().from) {
            return error("\"color_by\": the thresholds have to go up");
        }
        ret.push_back(Threshold { .from = *from, .color = c });
    }
    return ret;
}

// `json` is the element after the cascade, `own` as the file has it.
std::expected<ValueElement, LayoutError> value_element(JSONValue const &json, JSONValue const &own)
{
    ValueElement ret { .at = TRY_EVAL(placement(json)), .panel = TRY_EVAL(panel(json)), .style = TRY_EVAL(text_style(json)) };
    auto const   field = json.get("field");
    if (!field) {
        return error("a value needs a \"field\"");
    }
    auto const name = TRY_EVAL(string(*field, "field"));
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
    // A window in the defaults or a style can be for the graphs: only one
    // the value gives itself has to be for grade.
    if (own.get("window") && ret.field != OverlayField::grade) {
        return error("\"window\" is only for grade");
    }
    if (auto const window = json.get("window"); window && ret.field == OverlayField::grade) {
        ret.window = TRY_EVAL(number(*window, "window"));
        if (*ret.window < ShortestWindow || *ret.window > LongestWindow) {
            return error(std::format("\"window\" is from {} to {} m", ShortestWindow, LongestWindow));
        }
    }
    if (auto const c = json.get("color_by"); c) {
        ret.thresholds = TRY_EVAL(color_by(*c, ret.field));
    }
    return ret;
}

// A gauge's or a graph's field: a number, and in which unit.
struct ShownField {
    OverlayField             field { OverlayField::speed };
    std::optional<FieldUnit> unit { };
};

std::expected<ShownField, LayoutError> shown_field(JSONValue const &json, std::string_view what)
{
    auto const field = json.get("field");
    if (!field) {
        return error(std::format("a {} needs a \"field\"", what));
    }
    auto const name = TRY_EVAL(string(*field, "field"));
    auto const f = overlay_field(name);
    if (!f) {
        return error(std::format("\"field\": `{}` isn't a field", name));
    }
    if (field_kind(*f) != FieldKind::Number) {
        return error(std::format("\"field\": a {} shows a number, which {} isn't", what, name));
    }
    ShownField ret { .field = *f };
    if (auto const unit = json.get("unit"); unit) {
        auto const u = TRY_EVAL(string(*unit, "unit"));
        if (u != "auto") {
            ret.unit = field_unit(*f, u);
            if (!ret.unit) {
                return error(std::format("\"unit\": {} can't be shown in `{}`", name, u));
            }
        }
    }
    return ret;
}

// A number, or "auto": none.
std::expected<std::optional<double>, LayoutError> bound(JSONValue const &json, std::string_view property)
{
    if (string_of(json) == "auto") {
        return std::optional<double> { };
    }
    if (auto const n = number_of(json); n) {
        return std::optional { *n };
    }
    return error(std::format("\"{}\" isn't a number or \"auto\"", property));
}

// `min` and `max`, of which `min` is the lower, if both are numbers.
std::expected<std::pair<std::optional<double>, std::optional<double>>, LayoutError> bounds(JSONValue const &json)
{
    std::optional<double> min { };
    std::optional<double> max { };
    if (auto const m = json.get("min"); m) {
        min = TRY_EVAL(bound(*m, "min"));
    }
    if (auto const m = json.get("max"); m) {
        max = TRY_EVAL(bound(*m, "max"));
    }
    if (min && max && *min >= *max) {
        return error("\"min\" isn't less than \"max\"");
    }
    return std::pair { min, max };
}

// A gauge or a graph has no text to measure.
std::expected<Placement, LayoutError> sized_placement(JSONValue const &json, std::string_view what)
{
    auto ret = TRY_EVAL(placement(json));
    if (!ret.size) {
        return error(std::format("a {} needs a \"size\"", what));
    }
    return ret;
}

std::expected<GaugeElement, LayoutError> gauge_element(JSONValue const &json)
{
    auto const [field, unit] = TRY_EVAL(shown_field(json, "gauge"));
    auto const [min, max] = TRY_EVAL(bounds(json));
    auto const   own = TRY_EVAL(text_style(json)).color;
    GaugeElement ret {
        .at = TRY_EVAL(sized_placement(json, "gauge")),
        .panel = TRY_EVAL(panel(json)),
        .field = field,
        .unit = unit,
        .min = min,
        .max = max,
        // The track is the gauge's color, faintly, unless it says.
        .track = Color { .r = own.r, .g = own.g, .b = own.b, .a = static_cast<unsigned char>(own.a / 5) },
        .fill = own,
    };
    if (auto const shape = json.get("shape"); shape) {
        auto const name = string_of(*shape).value_or("");
        if (name == "bar") {
            ret.shape = GaugeShape::Bar;
        } else if (name == "vbar") {
            ret.shape = GaugeShape::VBar;
        } else if (name == "arc") {
            ret.shape = GaugeShape::Arc;
        } else {
            return error("\"shape\" isn't bar, vbar or arc");
        }
    }
    if (auto const track = json.get("track"); track) {
        ret.track = TRY_EVAL(color(*track, "track"));
    }
    if (auto const fill = json.get("fill"); fill) {
        ret.fill = TRY_EVAL(color(*fill, "fill"));
    }
    if (auto const thickness = json.get("thickness"); thickness) {
        ret.thickness = static_cast<float>(TRY_EVAL(number(*thickness, "thickness")));
        if (*ret.thickness <= 0) {
            return error("\"thickness\" isn't more than 0");
        }
    }
    if (auto const c = json.get("color_by"); c) {
        ret.thresholds = TRY_EVAL(color_by(*c, ret.field));
    }
    return ret;
}

std::expected<GraphElement, LayoutError> graph_element(JSONValue const &json)
{
    auto const [field, unit] = TRY_EVAL(shown_field(json, "graph"));
    auto const [min, max] = TRY_EVAL(bounds(json));
    GraphElement ret {
        .at = TRY_EVAL(sized_placement(json, "graph")),
        .panel = TRY_EVAL(panel(json)),
        .field = field,
        .unit = unit,
        .min = min,
        .max = max,
        .line = Stroke { .color = TRY_EVAL(text_style(json)).color },
    };
    if (auto const axis = json.get("x_axis"); axis) {
        auto const name = string_of(*axis).value_or("");
        if (name == "time") {
            ret.axis = GraphAxis::Time;
        } else if (name == "distance") {
            ret.axis = GraphAxis::Distance;
        } else {
            return error("\"x_axis\" isn't time or distance");
        }
    }
    if (auto const window = json.get("window"); window && string_of(*window) != "all") {
        auto const w = number_of(*window);
        if (!w || *w <= 0) {
            return error("\"window\" isn't more than 0, or \"all\"");
        }
        ret.window = *w;
    }
    if (auto const line = json.get("line"); line) {
        ret.line = TRY_EVAL(stroke(*line, "line", ret.line));
    }
    if (auto const fill = json.get("fill"); fill) {
        ret.fill = TRY_EVAL(color(*fill, "fill"));
    }
    if (auto const cursor = json.get("cursor"); cursor) {
        if (!cursor->is_object()) {
            return error("\"cursor\" isn't a dot, such as { \"color\": \"#ffffff\", \"radius\": 6 }");
        }
        TRY(check_properties(*cursor, "\"cursor\"", { { "color", "radius" } }));
        Cursor c { .color = ret.line.color };
        if (auto const col = cursor->get("color"); col) {
            c.color = TRY_EVAL(color(*col, "cursor.color"));
        }
        if (auto const radius = cursor->get("radius"); radius) {
            c.radius = static_cast<float>(TRY_EVAL(number(*radius, "cursor.radius")));
            if (c.radius <= 0) {
                return error("\"cursor.radius\" isn't more than 0");
            }
        }
        ret.cursor = c;
    }
    return ret;
}

// `{ "line": { ... }, "fill": "#..." }`, over `base`.
std::expected<Trace, LayoutError> profile_trace(JSONValue const &json, std::string_view property, Trace base)
{
    if (!json.is_object()) {
        return error(std::format("\"{}\" isn't a line and a fill, such as {{ \"line\": {{ \"width\": 3 }} }}", property));
    }
    TRY(check_properties(json, std::format("\"{}\"", property), { { "line", "fill" } }));
    auto ret = base;
    if (auto const line = json.get("line"); line) {
        ret.line = TRY_EVAL(stroke(*line, std::format("{}.line", property), ret.line));
    }
    if (auto const fill = json.get("fill"); fill) {
        ret.fill = TRY_EVAL(color(*fill, std::format("{}.fill", property)));
    }
    return ret;
}

// Over the element's own marker, `base`; `null` for none.
// A profile's marker can have a line down from it; a map's ignores one
// from the defaults or a style, which can be for the profiles.
std::expected<std::optional<Marker>, LayoutError> marker(JSONValue const &json, Marker base, bool with_drop_line)
{
    if (json.is_null()) {
        return std::optional<Marker> { };
    }
    if (!json.is_object()) {
        return error("\"marker\" isn't a dot, such as { \"color\": \"#ffffff\", \"radius\": 8 }, or null");
    }
    TRY(check_properties(json, "\"marker\"", { { "shape", "color", "radius", "outline", "drop_line" } }));
    auto ret = base;
    if (auto const shape = json.get("shape"); shape && string_of(*shape) != "dot") {
        return error("\"marker.shape\" isn't dot");
    }
    if (auto const c = json.get("color"); c) {
        ret.color = TRY_EVAL(color(*c, "marker.color"));
    }
    if (auto const radius = json.get("radius"); radius) {
        ret.radius = static_cast<float>(TRY_EVAL(number(*radius, "marker.radius")));
        if (ret.radius <= 0) {
            return error("\"marker.radius\" isn't more than 0");
        }
    }
    if (auto const outline = json.get("outline"); outline) {
        ret.outline = TRY_EVAL(color(*outline, "marker.outline"));
    }
    if (auto const drop = json.get("drop_line"); drop && with_drop_line) {
        // `true` is a thin line in the marker's color, faintly.
        auto const faint = Stroke { .color = Color { .r = ret.color.r, .g = ret.color.g, .b = ret.color.b, .a = static_cast<unsigned char>(ret.color.a / 2) }, .width = 1 };
        if (drop->is_boolean()) {
            ret.drop_line = TRY_EVAL(boolean(*drop, "marker.drop_line")) ? std::optional { faint } : std::nullopt;
        } else {
            ret.drop_line = TRY_EVAL(stroke(*drop, "marker.drop_line", faint));
        }
    }
    return ret;
}

std::expected<JSONValue, LayoutError> cascaded(JSONValue const &element, JSONValue const &defaults, JSONValue const &styles, Names whole = { });

// A label's style: its own properties over its named style, over the
// profile's.
std::expected<TextStyle, LayoutError> label_style(JSONValue const &json, std::string_view property, TextStyle const &base, JSONValue const &styles)
{
    auto const cascaded_label = cascaded(json, JSONValue::object(), styles);
    if (!cascaded_label) {
        return error(std::format("\"{}\": {}", property, cascaded_label.error().message));
    }
    return text_style(*cascaded_label, base);
}

std::expected<ProfileElement, LayoutError> profile_element(JSONValue const &json, JSONValue const &styles)
{
    auto const [min, max] = TRY_EVAL(bounds(json));
    ProfileElement ret {
        .at = TRY_EVAL(sized_placement(json, "profile")),
        .panel = TRY_EVAL(panel(json)),
        .style = TRY_EVAL(text_style(json)),
        .min = min,
        .max = max,
    };
    ret.done.line.color = ret.style.color;
    if (auto const extent = json.get("extent"); extent) {
        if (extent->is_object()) {
            return error("\"extent\": a part of the activity isn't there yet, only \"all\" and \"lap\"");
        }
        auto const name = string_of(*extent).value_or("");
        if (name == "all") {
            ret.extent = ProfileExtent::All;
        } else if (name == "lap") {
            ret.extent = ProfileExtent::Lap;
        } else {
            return error("\"extent\" isn't all or lap");
        }
    }
    if (auto const smooth = json.get("smooth"); smooth) {
        ret.smooth = TRY_EVAL(not_negative(*smooth, "smooth"));
    }
    if (auto const span = json.get("min_span"); span) {
        ret.min_span = TRY_EVAL(number(*span, "min_span"));
        if (ret.min_span <= 0) {
            return error("\"min_span\" isn't more than 0");
        }
    }
    if (auto const done = json.get("done"); done) {
        ret.done = TRY_EVAL(profile_trace(*done, "done", ret.done));
    }
    if (auto const remaining = json.get("remaining"); remaining) {
        ret.remaining = TRY_EVAL(profile_trace(*remaining, "remaining", ret.done));
    }
    auto const m = json.get("marker");
    auto const own = Marker { .color = ret.done.line.color };
    ret.marker = m ? TRY_EVAL(marker(*m, own, true)) : std::optional { own };
    if (auto const label = json.get("marker_label"); label) {
        if (!label->is_object()) {
            return error("\"marker_label\" isn't a text, such as { \"text\": \"{altitude:.0f}\" }");
        }
        TRY(check_properties(*label, "\"marker_label\"", { { "text", "style" }, TextStyleProperties }));
        auto const text = label->get("text");
        if (!text) {
            return error("\"marker_label\" needs a \"text\"");
        }
        auto parsed = parse_template(TRY_EVAL(string(*text, "marker_label.text")));
        if (!parsed) {
            return error(std::format("\"marker_label.text\": {}", parsed.error()));
        }
        ret.marker_label = ProfileLabel { .text = std::move(*parsed), .style = TRY_EVAL(label_style(*label, "marker_label", ret.style, styles)) };
    }
    if (auto const labels = json.get("axis_labels"); labels) {
        if (!labels->is_object()) {
            return error("\"axis_labels\" isn't an object, such as { \"min_max\": true }");
        }
        // Distance ticks are deferred: taken, and not drawn yet.
        TRY(check_properties(*labels, "\"axis_labels\"", { { "min_max", "distance", "style" }, TextStyleProperties }));
        if (auto const min_max = labels->get("min_max"); min_max && TRY_EVAL(boolean(*min_max, "axis_labels.min_max"))) {
            ret.axis_labels = TRY_EVAL(label_style(*labels, "axis_labels", ret.style, styles));
        }
    }
    if (auto const laps = json.get("laps"); laps) {
        ret.laps = TRY_EVAL(stroke(*laps, "laps", Stroke { .width = 1 }));
    }
    return ret;
}

// The done track in the map's color; the remaining one as the done one,
// at a third of its alpha.
std::expected<MapTrack, LayoutError> map_track(JSONValue const &json, Color color)
{
    MapTrack                 ret { .done = Stroke { .color = color, .width = 4 } };
    std::optional<JSONValue> remaining { };
    if (!json.is_null()) {
        if (!json.is_object()) {
            return error("\"track\" isn't an object, such as { \"done\": { \"width\": 6 } }");
        }
        TRY(check_properties(json, "\"track\"", { { "done", "remaining", "outline" } }));
        if (auto const done = json.get("done"); done) {
            ret.done = TRY_EVAL(stroke(*done, "track.done", ret.done));
        }
        remaining = json.get("remaining");
        if (auto const outline = json.get("outline"); outline) {
            ret.outline = TRY_EVAL(stroke(*outline, "track.outline", Stroke { .color = Color { .r = 0, .g = 0, .b = 0, .a = 128 } }));
        }
    }
    auto const faint = Color { .r = ret.done.color.r, .g = ret.done.color.g, .b = ret.done.color.b, .a = static_cast<unsigned char>(ret.done.color.a / 3) };
    ret.remaining = Stroke { .color = faint, .width = ret.done.width };
    if (remaining) {
        ret.remaining = TRY_EVAL(stroke(*remaining, "track.remaining", ret.remaining));
    }
    return ret;
}

std::expected<MapTileLayer, LayoutError> map_tiles(JSONValue const &json)
{
    if (!json.is_object()) {
        return error("\"tiles\" isn't an object, such as { \"source\": \"osm\" }, or null");
    }
    TRY(check_properties(json, "\"tiles\"", { { "source", "opacity", "filter" } }));
    MapTileLayer ret;
    if (auto const source = json.get("source"); source && string_of(*source) != "osm") {
        return error("\"tiles.source\" isn't osm, the only source there is yet");
    }
    if (auto const opacity = json.get("opacity"); opacity) {
        auto const o = number_of(*opacity);
        if (!o || *o < 0 || *o > 1) {
            return error("\"tiles.opacity\" isn't a number from 0 to 1");
        }
        ret.opacity = static_cast<float>(*o);
    }
    if (auto const filter = json.get("filter"); filter) {
        auto const name = string_of(*filter).value_or("");
        if (name == "none") {
            ret.filter.kind = TileFilterKind::None;
        } else if (name == "grayscale") {
            ret.filter.kind = TileFilterKind::Grayscale;
        } else if (name == "dark") {
            ret.filter.kind = TileFilterKind::Dark;
        } else if (auto const tint = parse_color(name); tint) {
            ret.filter = TileFilter { .kind = TileFilterKind::Tint, .tint = *tint };
        } else {
            return error("\"tiles.filter\" isn't none, grayscale, dark or a color");
        }
    }
    return ret;
}

std::expected<MapElement, LayoutError> map_element(JSONValue const &json, JSONValue const &styles)
{
    auto const style = TRY_EVAL(text_style(json));
    auto const color = style.color;
    MapElement ret {
        .at = TRY_EVAL(sized_placement(json, "map")),
        .panel = TRY_EVAL(panel(json)),
        .track = TRY_EVAL(map_track(json.get_with_default("track"), color)),
    };
    if (auto const view = json.get("view"); view) {
        auto const name = string_of(*view).value_or("");
        if (name == "fit") {
            ret.mode = MapMode::Fit;
        } else if (name == "follow") {
            ret.mode = MapMode::Follow;
        } else {
            return error("\"view\" isn't fit or follow");
        }
    }
    if (auto const zoom = json.get("zoom"); zoom) {
        auto const z = number_of(*zoom);
        if (!z || *z != std::floor(*z) || *z < 1 || *z > MaxMapZoom) {
            return error(std::format("\"zoom\" isn't a whole number from 1 to {}", MaxMapZoom));
        }
        ret.zoom = static_cast<int>(*z);
    }
    if (auto const clip = json.get("clip"); clip) {
        auto const name = string_of(*clip).value_or("");
        if (name == "rect") {
            ret.clip = ClipKind::Rect;
        } else if (name == "rounded") {
            ret.clip = ClipKind::Rounded;
        } else if (name == "circle") {
            ret.clip = ClipKind::Circle;
        } else {
            return error("\"clip\" isn't rect, rounded or circle");
        }
    }
    auto const m = json.get("marker");
    auto const own = Marker { .color = ret.track.done.color, .radius = 8 };
    ret.marker = m ? TRY_EVAL(marker(*m, own, false)) : std::optional { own };
    if (auto const tiles = json.get("tiles"); tiles && !tiles->is_null()) {
        ret.tiles = TRY_EVAL(map_tiles(*tiles));
    }
    // Small, and readable over footage, rather than in the map's text size.
    ret.attribution = TextStyle {
        .font = style.font,
        .font_size = 14,
        .color = style.color,
        .shadow = Shadow { .color = { 0, 0, 0, 160 }, .offset = { 1, 1 } },
    };
    if (auto const attribution = json.get("attribution"); attribution) {
        if (!attribution->is_object()) {
            return error("\"attribution\" isn't an object, such as { \"style\": \"label\" }");
        }
        TRY(check_properties(*attribution, "\"attribution\"", { { "style" }, TextStyleProperties }));
        ret.attribution = TRY_EVAL(label_style(*attribution, "attribution", ret.attribution, styles));
    }
    return ret;
}

// The element's properties after the cascade: the defaults, its styles in
// order, and its own. The objects in `whole` replace those under them
// rather than merge with them.
std::expected<JSONValue, LayoutError> cascaded(JSONValue const &element, JSONValue const &defaults, JSONValue const &styles, Names whole)
{
    auto const cascade = [whole](JSONValue base, JSONValue const &over) {
        for (auto const name : whole) {
            if (over.get(std::string { name })) {
                base.erase(std::string { name });
            }
        }
        return ST::cascade(std::move(base), over);
    };
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

std::expected<ImageElement, LayoutError> image_element(JSONValue const &json)
{
    // Its size may have only one side, which placement() doesn't take.
    auto unsized = json;
    unsized.erase("size");
    ImageElement ret { .at = TRY_EVAL(placement(unsized)), .panel = TRY_EVAL(panel(json)) };
    auto const   src = json.get("src");
    if (!src) {
        return error("an image needs a \"src\"");
    }
    ret.src = TRY_EVAL(string(*src, "src"));
    // The raylib we build with reads no JPEG, and SVG isn't drawn.
    if (to_lower(std::filesystem::path { ret.src }.extension().string()) != ".png") {
        return error(std::format("the picture `{}` isn't a PNG: only PNG pictures are supported", ret.src));
    }
    if (auto const tint = json.get("tint"); tint) {
        ret.tint = TRY_EVAL(color(*tint, "tint"));
    }
    if (auto const size = json.get("size"); size) {
        if (!size->is_object()) {
            return error("\"size\" isn't an object with a width, a height, or both");
        }
        TRY(check_properties(*size, "\"size\"", { { "width", "height" } }));
        auto const side = [&size](char const *name) -> std::expected<std::optional<float>, LayoutError> {
            auto const s = size->get(name);
            if (!s) {
                return std::optional<float> { };
            }
            auto const n = TRY_EVAL(number(*s, std::format("size.{}", name)));
            if (n <= 0) {
                return error(std::format("\"size.{}\" isn't more than 0", name));
            }
            return std::optional { static_cast<float>(n) };
        };
        ret.width = TRY_EVAL(side("width"));
        ret.height = TRY_EVAL(side("height"));
    }
    return ret;
}

// None without `visible` or `fade`: the element always shows.
std::expected<std::optional<Visibility>, LayoutError> visibility(JSONValue const &json)
{
    auto const visible = json.get("visible");
    auto const fade = json.get("fade");
    if (!visible && !fade) {
        return std::optional<Visibility> { };
    }
    Visibility ret;
    if (visible) {
        if (!visible->is_object()) {
            return error("\"visible\" isn't an object, such as { \"from\": 0, \"to\": 5 }");
        }
        TRY(check_properties(*visible, "\"visible\"", { { "from", "to" } }));
        if (auto const from = visible->get("from"); from) {
            ret.from = TRY_EVAL(number(*from, "visible.from"));
        }
        if (auto const to = visible->get("to"); to && string_of(*to) != "end") {
            auto const t = number_of(*to);
            if (!t) {
                return error("\"visible.to\" isn't a number or \"end\"");
            }
            ret.to = *t;
        }
        // Ending at the video's start: probably meant as its end.
        if (ret.to == 0.0) {
            return error("\"visible\": it's never visible, as \"to\" is the video's start: its end is \"end\"");
        }
        // Both from the start, or both from the end: empty whatever the
        // video's length.
        if (ret.to && (ret.from < 0) == (*ret.to < 0) && ret.from >= *ret.to) {
            return error("\"visible\": it's never visible, as \"from\" isn't before \"to\"");
        }
    }
    if (fade) {
        auto const f = number_of(*fade);
        if (!f || *f < 0) {
            return error("\"fade\" isn't a number of 0 or more");
        }
        ret.fade = *f;
    }
    return std::optional { ret };
}

std::expected<std::optional<OverlayElement>, LayoutError> typed_element(JSONValue const &json, JSONValue const &defaults, JSONValue const &styles);

// The element, and when it shows, which every type has.
std::expected<std::optional<OverlayElement>, LayoutError> element(JSONValue const &json, JSONValue const &defaults, JSONValue const &styles)
{
    auto ret = TRY_EVAL(typed_element(json, defaults, styles));
    if (ret) {
        auto const visible = TRY_EVAL(visibility(TRY_EVAL(cascaded(json, defaults, styles))));
        std::visit([&visible](auto &e) { e.visible = visible; }, *ret);
    }
    return ret;
}

std::expected<std::optional<OverlayElement>, LayoutError> typed_element(JSONValue const &json, JSONValue const &defaults, JSONValue const &styles)
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
        return std::optional<OverlayElement> { TRY_EVAL(value_element(TRY_EVAL(cascaded(json, defaults, styles)), json)) };
    }
    // A gauge's and a graph's color is what they draw in, by default.
    if (name == "gauge") {
        TRY(check_properties(json, "a gauge", { ElementProperties, { "color" }, BoxStyleProperties, GaugeProperties }));
        return std::optional<OverlayElement> { TRY_EVAL(gauge_element(TRY_EVAL(cascaded(json, defaults, styles)))) };
    }
    if (name == "map") {
        TRY(check_properties(json, "a map", { ElementProperties, { "color" }, BoxStyleProperties, MapProperties }));
        if (auto const m = json.get("marker"); m && m->is_object() && m->get("drop_line")) {
            return error("\"drop_line\" isn't a property of a map's marker");
        }
        return std::optional<OverlayElement> { TRY_EVAL(map_element(TRY_EVAL(cascaded(json, defaults, styles)), styles)) };
    }
    if (name == "profile") {
        TRY(check_properties(json, "a profile", { ElementProperties, TextStyleProperties, BoxStyleProperties, ProfileProperties }));
        return std::optional<OverlayElement> { TRY_EVAL(profile_element(TRY_EVAL(cascaded(json, defaults, styles)), styles)) };
    }
    if (name == "image") {
        TRY(check_properties(json, "an image", { ElementProperties, BoxStyleProperties, ImageProperties }));
        // One side of a size is the other in the picture's aspect ratio,
        // which merging it with a size under it would undo.
        return std::optional<OverlayElement> { TRY_EVAL(image_element(TRY_EVAL(cascaded(json, defaults, styles, { "size" })))) };
    }
    if (name == "graph") {
        TRY(check_properties(json, "a graph", { ElementProperties, { "color" }, BoxStyleProperties, GraphProperties }));
        return std::optional<OverlayElement> { TRY_EVAL(graph_element(TRY_EVAL(cascaded(json, defaults, styles)))) };
    }
    return error(std::format("\"type\": `{}` isn't an element type", name));
}

}

float visibility_alpha(std::optional<Visibility> const &visible, VideoTime time)
{
    if (!visible) {
        return 1;
    }
    // Times from the start, inside the video, so that the element fades
    // in and out within it.
    auto const seconds = [&time](double t) { return (t < 0) ? time.length + t : t; };
    auto const from = std::max(0.0, seconds(visible->from));
    auto const to = std::min(time.length, visible->to ? seconds(*visible->to) : time.length);
    if (time.at < from || time.at >= to) {
        return 0;
    }
    if (visible->fade <= 0) {
        return 1;
    }
    auto const alpha = std::min({ 1.0, (time.at - from) / visible->fade, (to - time.at) / visible->fade });
    return static_cast<float>(alpha);
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
    auto const style_properties = { TextStyleProperties, BoxStyleProperties, TextProperties, ValueProperties, GaugeProperties, GraphProperties, ProfileProperties, MapProperties, ImageProperties, LaterProperties,
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

std::vector<std::string> element_fonts(OverlayElement const &element)
{
    if (auto const *text = std::get_if<TextElement>(&element)) {
        return { text->style.font };
    }
    if (auto const *value = std::get_if<ValueElement>(&element)) {
        return { value->style.font, value->unit_style.font };
    }
    if (auto const *profile = std::get_if<ProfileElement>(&element)) {
        std::vector<std::string> ret;
        if (profile->marker_label) {
            ret.push_back(profile->marker_label->style.font);
        }
        if (profile->axis_labels) {
            ret.push_back(profile->axis_labels->font);
        }
        return ret;
    }
    if (auto const *map = std::get_if<MapElement>(&element); map && map->tiles) {
        return { map->attribution.font };
    }
    return { };
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

Color threshold_color(std::vector<Threshold> const &thresholds, std::optional<double> shown, Color own)
{
    auto ret = own;
    for (auto const &t : thresholds) {
        if (shown && *shown >= t.from) {
            ret = t.color;
        }
    }
    return ret;
}

namespace {

// The number as it's shown, rounded by its spec, so that the color goes
// with what's read: 259.6 W shown as 260 is at a threshold of 260. A spec
// whose text doesn't read back, such as with a fill, keeps the value.
double as_shown(std::string_view text, double value)
{
    auto const start = text.find_first_not_of(" +");
    auto const end = text.find_last_not_of(' ');
    if (start == std::string_view::npos) {
        return value;
    }
    text = text.substr(start, end + 1 - start);
    double ret { 0 };
    auto const [rest, error] = std::from_chars(text.data(), text.data() + text.size(), ret);
    return (error == std::errc { } && rest == text.data() + text.size()) ? ret : value;
}

}

std::optional<ShownValue> shown_value(ValueElement const &value, FieldValues const &values, Units units)
{
    // A grade over the element's own window, or the field as it is.
    auto const number = value.window ? values.grade(*value.window) : values[value.field];
    auto const has = value.window ? number.has_value() : values.has(value.field);
    if (!has && value.hide_when_missing) {
        return { };
    }
    auto const unit = value.unit.value_or(default_unit(value.field, units, values.sport));
    auto const spec = value.spec.empty() ? default_spec(value.field) : std::string_view { value.spec };
    auto       text = value.window ? format_field(value.field, number, unit, spec, value.missing) : format_value(values, value.field, unit, spec, value.missing);
    auto const shown = number ? std::optional { as_shown(text, unit.shown(*number)) } : std::nullopt;
    return ShownValue {
        .number = std::move(text),
        .unit = has ? std::string { unit.name } : std::string { },
        .color = threshold_color(value.thresholds, shown, value.style.color),
    };
}

std::set<int> layout_codepoints(OverlayLayout const &layout)
{
    // The units' degrees, °C and °F, which a layout doesn't spell out.
    std::set<int> ret { 0xb0 };
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
        } else if (auto const *profile = std::get_if<ProfileElement>(&element); profile && profile->marker_label) {
            for (auto const &part : profile->marker_label->text.parts) {
                if (auto const *literal = std::get_if<std::string>(&part)) {
                    add(*literal);
                }
            }
        } else if (auto const *map = std::get_if<MapElement>(&element); map && map->tiles) {
            add(OsmAttribution);
        }
    }
    return ret;
}

}
