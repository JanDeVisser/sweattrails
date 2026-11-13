/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <format>

#include <StringUtil.h>

#include <App.h>

#include <widget/ActivityInfoBox.h>

namespace ST {

namespace {

constexpr float Margin = 10.0f;
constexpr float EntryIndent = 24.0f; // Entries and details, under their heading
constexpr float RowSpacing = 4.0f;

#ifdef __APPLE__
constexpr char const *RevealName = "Show in Finder";
#else
constexpr char const *RevealName = "Open folder";
#endif

Color const HeadingColor = LIGHTGRAY;
Color const SmallColor = GRAY;

float text_width(std::string const &text, FontSize size)
{
    return text.empty() ? 0.0f : App::measure_text(text.c_str(), size).x;
}

// The width of an entry: its text, a space, and its small part.
float line_width(InfoLine const &line)
{
    auto       width = text_width(line.text, FontSize::Medium);
    auto const small = text_width(line.small, FontSize::Small);
    if (small > 0) {
        width += (width > 0 ? text_width(" ", FontSize::Medium) : 0.0f) + small;
    }
    return width;
}

}

std::vector<InfoRow> info_rows(ActivityInfo const &info)
{
    using enum InfoRow::Kind;
    std::vector<InfoRow> ret;
    if (!info.file.empty()) {
        ret.push_back({ .kind = Heading, .line = { .text = "File" } });
        ret.push_back({ .kind = Entry, .line = { .text = info.file } });
    }
    ret.push_back({ .kind = Heading, .line = { .text = "Recorded with" } });
    ret.push_back({ .kind = Entry, .line = info.device });
    ret.push_back({ .kind = Heading, .line = { .text = "Sensors" } });
    if (info.sensors.empty()) {
        ret.push_back({ .kind = Entry, .line = { .text = std::string { NoSensors } } });
    }
    for (auto const &sensor : info.sensors) {
        ret.push_back({ .kind = Entry, .line = sensor.name });
        if (!sensor.details.empty()) {
            ret.push_back({ .kind = Details, .line = { .small = sensor.details } });
        }
    }
    return ret;
}

std::string info_title(ActivityInfo const &info)
{
    return info.recording.empty() ? "Activity info" : std::format("Activity info · {}", info.recording);
}

std::string info_hint(ActivityInfo const &info)
{
    std::vector<std::string> keys;
    if (!info.path.empty()) {
        keys.push_back(std::format("F: {}", RevealName));
    }
    if (!info.recording.empty()) {
        keys.emplace_back("Tab: next recording");
    }
    keys.emplace_back("Esc closes");
    return join(keys, " · ");
}

ActivityInfoBox::ActivityInfoBox(Describe describe, SwitchRecording switch_recording, Reveal reveal)
    : Modal("Activity info")
    , describe_fnc(std::move(describe))
    , switch_fnc(std::move(switch_recording))
    , reveal_fnc(std::move(reveal))
{
    background = BLACK;
}

void ActivityInfoBox::initialize()
{
    refresh();
    resize();
}

void ActivityInfoBox::refresh()
{
    if (auto described = describe_fnc(); described) {
        info = std::move(*described);
    }
}

void ActivityInfoBox::show()
{
    make_visible();
}

void ActivityInfoBox::reveal()
{
    if (!info.path.empty()) {
        reveal_fnc(info.path);
    }
}

bool ActivityInfoBox::process_key(KeyboardModifier modifier, int key)
{
    if (status == ModalStatus::Dormant) {
        return false;
    }
    if (key == KEY_TAB && (modifier == KModNone || modifier == KModShift)) {
        switch_fnc(modifier == KModShift ? -1 : 1);
        refresh();
        resize();
        return true;
    }
    if (modifier != KModNone) {
        return false;
    }
    switch (key) {
    case KEY_ESCAPE:
        status = ModalStatus::Dismissed;
        hide();
        return true;
    case KEY_F:
        reveal();
        return true;
    default:
        return false;
    }
}

// A click on the hint shows the file, as F does.
void ActivityInfoBox::process_input()
{
    if (info.path.empty() || !CheckCollisionPointRec(GetMousePosition(), hint_area)) {
        return;
    }
    App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        reveal();
    }
}

float ActivityInfoBox::row_height(InfoRow::Kind kind)
{
    auto const size = (kind == InfoRow::Kind::Details) ? FontSize::Small : FontSize::Medium;
    return App::the()->cells[static_cast<size_t>(size)].y + RowSpacing;
}

void ActivityInfoBox::resize()
{
    auto const &screen = App::the()->viewport;
    auto const  rows = info_rows(info);
    auto        widest = std::max(text_width(info_title(info), FontSize::Medium), text_width(info_hint(info), FontSize::Small));
    auto        rows_height = 0.0f;
    for (auto const &row : rows) {
        auto const indent = (row.kind == InfoRow::Kind::Heading) ? 0.0f : EntryIndent - Margin;
        widest = std::max(widest, indent + line_width(row.line));
        rows_height += row_height(row.kind);
    }
    auto const title_height = row_height(InfoRow::Kind::Heading);
    auto const hint_height = row_height(InfoRow::Kind::Details);
    viewport.width = std::clamp(widest + 2 * Margin, screen.width / 3, screen.width * 0.8f);
    viewport.height = Margin + title_height + Margin + rows_height + Margin + hint_height + Margin;
    viewport.x = (screen.width - viewport.width) / 2;
    viewport.y = std::max(0.0f, (screen.height - viewport.height) / 3);
}

void ActivityInfoBox::draw()
{
    auto const &medium = App::the()->cells[static_cast<size_t>(FontSize::Medium)];
    auto const &small = App::the()->cells[static_cast<size_t>(FontSize::Small)];
    draw_rectangle(0.0f, 0.0f, 0.0f, 0.0f, BLACK);
    draw_outline(2, 2, -2.0f, -2.0f, RAYWHITE);

    auto y = Margin;
    render_text(Margin, y, info_title(info), FontSize::Medium, RAYWHITE);
    y += row_height(InfoRow::Kind::Heading);
    draw_line(2, y + Margin / 2, -2, y + Margin / 2, RAYWHITE);
    y += Margin;

    for (auto const &row : info_rows(info)) {
        auto const x = (row.kind == InfoRow::Kind::Heading) ? Margin : EntryIndent;
        auto const color = (row.kind == InfoRow::Kind::Heading) ? HeadingColor : RAYWHITE;
        auto       small_x = x;
        if (!row.line.text.empty()) {
            render_text(x, y, row.line.text, FontSize::Medium, color);
            small_x += text_width(row.line.text, FontSize::Medium) + text_width(" ", FontSize::Medium);
        }
        if (!row.line.small.empty()) {
            // On the same baseline, near enough, as the text before it.
            auto const drop = (row.kind == InfoRow::Kind::Details) ? 0.0f : medium.y - small.y;
            render_text(small_x, y + drop, row.line.small, FontSize::Small, SmallColor);
        }
        y += row_height(row.kind);
    }

    draw_line(2, y + Margin / 2, -2, y + Margin / 2, RAYWHITE);
    y += Margin;
    auto const hint = info_hint(info);
    render_text(Margin, y, hint, FontSize::Small, SmallColor);
    hint_area = Rectangle {
        .x = viewport.x + Margin,
        .y = viewport.y + y,
        .width = text_width(hint, FontSize::Small),
        .height = small.y,
    };
}

}
