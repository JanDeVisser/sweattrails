/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "raylib.h"
#include <App.h>
#include <JSON.h>
#include <Widget.h>

namespace ST {

char const *SizePolicy_name(SizePolicy policy)
{
    switch (policy) {
    case SizePolicy::Absolute:
        return "Absolute";
    case SizePolicy::Relative:
        return "Relative";
    case SizePolicy::Characters:
        return "Characters";
    case SizePolicy::Calculated:
        return "Calculated";
    case SizePolicy::Stretch:
        return "Stretch";
    default:
        UNREACHABLE();
    }
}

KeyboardModifier modifier_current()
{
    auto current_modifier = KModNone;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
        current_modifier = static_cast<KeyboardModifier>(current_modifier | KModShift);
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        current_modifier = static_cast<KeyboardModifier>(current_modifier | KModControl);
    }
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) {
        current_modifier = static_cast<KeyboardModifier>(current_modifier | KModSuper);
    }
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) {
        current_modifier = static_cast<KeyboardModifier>(current_modifier | KModAlt);
    }
    return current_modifier;
}

bool is_modifier_down(KeyboardModifier modifier)
{
    KeyboardModifier current_modifier = modifier_current();
    if (modifier == KModNone) {
        return current_modifier == KModNone;
    }
    return (current_modifier & modifier) == modifier;
}

std::string modifier_string(KeyboardModifier modifier)
{
    std::string ret { };
#undef S
#define S(mod, ord, str)                       \
    if ((KMod##mod & modifier) == KMod##mod) { \
        ret += (str);                          \
    }
    KEYBOARDMODIFIERS(S)
#undef S
    return ret;
}

Widget::Widget(pWidget parent, SizePolicy policy, float policy_size)
    : policy(policy)
    , policy_size(policy_size)
    , parent(std::move(parent))
{
}

bool Widget::mouse_in() const
{
    return contains(GetMouseX(), GetMouseY());
}

Vector2 Widget::render_sized_text_(float x, float y, rune_view const &text, Font font, float size, Color color) const
{
    if (text.empty()) {
        return { 0, 0 };
    }
    auto        text_width = 0.0f;
    auto        text_height = 0.0f;
    rune_string t { text };

    auto update_metrics = [&text_width, &text_height, &font, size](wchar_t ch) -> void {
        auto glyph = GetGlyphInfo(font, ch);
        text_width += glyph.advanceX * size + 2;
        text_height = std::max(text_height, static_cast<float>(glyph.offsetY + glyph.image.height) * size);
    };

    for (auto &ch : t) {
        update_metrics(ch);
    }
    if (x < 0) {
        x = viewport.width - text_width + x;
    }
    if (y < 0) {
        y = viewport.height - text_height + y;
    }
    Vector2 pos { viewport.x + x, viewport.y + y };
    assert(sizeof(rune) == sizeof(int));
    DrawTextCodepoints(font, reinterpret_cast<int const *>(t.c_str()), t.length(), pos, (float) font.baseSize * size, 2, color);
    return { text_width, text_height };
}

Vector2 Widget::render_sized_text_(float x, float y, rune_view const &text, FontSize font_size, Color color) const
{
    auto f = App::the()->font(font_size);
    if (f) {
        return render_sized_text_(x, y, text, *f, 1.0, color);
    } else if (font_size != FontSize::Medium) {
        return render_sized_text_(x, y, text, FontSize::Medium, color);
    }
    return { 0, 0 };
}

void Widget::render_text_bitmap_(float x, float y, std::string_view const &text, Color color) const
{
    if (text.empty()) {
        return;
    }
    std::string t { text };
    if (x < 0) {
        auto text_width = MeasureText(t.c_str(), 20);
        x = viewport.width - (float) text_width + x;
    }
    if (y < 0) {
        y = viewport.height - 20 + y;
    }
    DrawText(t.c_str(), (float) viewport.x + PADDING + x, viewport.y + PADDING + y, 20, color);
}

void Widget::draw_line_(float x0, float y0, float x1, float y1, Color color) const
{
    if (x0 < 0) {
        x0 = viewport.width + x0;
    }
    if (y0 < 0) {
        y0 = viewport.height + y0;
    }
    if (x1 <= 0) {
        x1 = viewport.width + x1;
    }
    if (y1 <= 0) {
        y1 = viewport.height + y1;
    }
    x0 = clamp(x0, 0.0f, viewport.width);
    y0 = clamp(y0, 0.0f, viewport.height);
    x1 = clamp(x1, 0.0f, viewport.width - 1);
    y1 = clamp(y1, 0.0f, viewport.height - 1);
    DrawLine(static_cast<int>(viewport.x) + x0,
        static_cast<int>(viewport.y) + y0,
        static_cast<int>(viewport.x) + x1,
        static_cast<int>(viewport.y) + y1,
        color);
}

void Widget::draw_circle_(float x, float y, float r, Color color) const
{
    if (x < 0) {
        x = viewport.width + x;
    }
    if (y < 0) {
        y = viewport.height + y;
    }
    x = clamp(x, 0.0f, viewport.width);
    y = clamp(y, 0.0f, viewport.height);
    DrawCircleV(Vector2 { viewport.x + x, viewport.y + y }, r, color);
}

void Widget::draw_rectangle_(float x, float y, float width, float height, Color color) const
{
    auto const r { normalize(x, y, width, height) };
    DrawRectangleRec(r, color);
}

void Widget::draw_rectangle_no_normalize_(float x, float y, float width, float height, Color color) const
{
    Rectangle const r { .x = viewport.x + x, .y = viewport.y + y, .width = width, .height = height };
    DrawRectangleRec(r, color);
}

void Widget::draw_outline_(float x, float y, float width, float height, Color color) const
{
    DrawRectangleLinesEx(normalize(x, y, width, height), 1, color);
}

void Widget::draw_outline_no_normalize_(float x, float y, float width, float height, Color color) const
{
    Rectangle r { .x = viewport.x + x, .y = viewport.y + y, .width = width, .height = height };
    DrawRectangleLinesEx(r, 1, color);
}

void Widget::draw_hover_panel_(float x, float y, StringList const &text, Color bgcolor, Color textcolor, FontSize font_size) const
{
    assert(!text.empty());
    size_t longest_line { 0 };
    size_t maxlen { 0 };
    for (size_t ix = 0; ix < text.size(); ++ix) {
        auto const &line = text.at(ix);
        if (line.length() > maxlen) {
            maxlen = line.length();
            longest_line = ix;
        }
    }
    auto f = App::the()->font(font_size);
    assert(f.has_value());
    auto text_size { MeasureTextEx(*f, text.at(longest_line).c_str(), f->baseSize, 2) };
    auto width { static_cast<float>(text_size.x + 12) };
    auto height { (text_size.y + 2) * text.size() + 12 };
    draw_rectangle_no_normalize(x, y, width, height, bgcolor);
    draw_outline_no_normalize(x + 2, y + 2, width - 4, height - 4, textcolor);
    for (size_t ix = 0; ix < text.size(); ++ix) {
        render_text(x + 6, y + (text_size.y + 2) * ix + 6, text[ix], *f, textcolor);
    }
}

bool Widget::contains(Vector2 world_coordinates) const
{
    return (viewport.x < world_coordinates.x) && (world_coordinates.x < viewport.x + viewport.width) && (viewport.y < world_coordinates.y) && (world_coordinates.y < viewport.y + viewport.height);
}

std::optional<Vec<int>> Widget::coordinates(Vector2 world_coordinates) const
{
    if (!contains(world_coordinates)) {
        return { };
    }
    auto ret = Vec<int> { .x = static_cast<int>(world_coordinates.x - viewport.x), .y = static_cast<int>(world_coordinates.y - viewport.y) };
    return ret;
}

Spacer::Spacer(pWidget const &parent)
    : Widget(parent, SizePolicy::Stretch, 1.0f)
{
}

Spacer::Spacer(pWidget const &parent, SizePolicy policy, float policy_size)
    : Widget(parent, policy, policy_size)
{
}

Label::Label(pWidget const &parent, std::string_view const &text, Color color)
    : Widget(parent, SizePolicy::Characters, 1.0f)
    , color(color)
    , text(text)
{
}

void Label::draw()
{
    auto app = App::the();
    auto ix = static_cast<size_t>(size);
    if (!text.empty()) {
        render_text(
            app->padding.x,
            (app->char_sizes[ix].y - app->font_sizes[ix]) / 2,
            text, app->fonts[ix].value(),
            color);
    }
}
}
