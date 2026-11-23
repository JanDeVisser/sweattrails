/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <App.h>
#include <InfoBox.h>

namespace ST {

namespace {

constexpr float Margin = 10.0f;
constexpr float EntryIndent = 24.0f; // Entries and details, under their heading
constexpr float RowSpacing = 4.0f;

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

InfoBox::InfoBox(std::string_view const &name)
    : Modal(name)
{
    background = BLACK;
}

void InfoBox::click_link(size_t, double)
{
}

void InfoBox::click_hint()
{
}

bool InfoBox::hint_clickable() const
{
    return false;
}

void InfoBox::close()
{
    status = ModalStatus::Dismissed;
    hide();
}

bool InfoBox::process_key(KeyboardModifier modifier, int key)
{
    if (status == ModalStatus::Dormant || modifier != KModNone || key != KEY_ESCAPE) {
        return false;
    }
    close();
    return true;
}

void InfoBox::process_input()
{
    handle_input(ScrollInput::current());
}

void InfoBox::handle_input(ScrollInput const &input)
{
    auto const on_hint = hint_clickable() && CheckCollisionPointRec(input.mouse, hint_area);
    auto const on_link = std::ranges::find_if(links, [&input](InfoLink const &link) {
        return CheckCollisionPointRec(input.mouse, link.area);
    });
    if (!on_hint && on_link == links.end()) {
        return;
    }
    App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
    if (!input.pressed) {
        return;
    }
    if (on_link != links.end()) {
        click_link(on_link->row, input.time);
    } else {
        click_hint();
    }
}

float InfoBox::row_height(InfoRow::Kind kind)
{
    auto const size = (kind == InfoRow::Kind::Details) ? FontSize::Small : FontSize::Medium;
    return App::the()->cells[static_cast<size_t>(size)].y + RowSpacing;
}

void InfoBox::resize()
{
    auto const &screen = App::the()->viewport;
    auto        widest = std::max(text_width(box_title(), FontSize::Medium), text_width(hint_text(GetTime()), FontSize::Small));
    auto        rows_height = 0.0f;
    for (auto const &row : box_rows()) {
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

void InfoBox::draw()
{
    auto const &medium = App::the()->cells[static_cast<size_t>(FontSize::Medium)];
    auto const &small = App::the()->cells[static_cast<size_t>(FontSize::Small)];
    draw_rectangle(0.0f, 0.0f, 0.0f, 0.0f, BLACK);
    draw_outline(2, 2, -2.0f, -2.0f, RAYWHITE);

    auto y = Margin;
    render_text(Margin, y, box_title(), FontSize::Medium, RAYWHITE);
    y += row_height(InfoRow::Kind::Heading);
    draw_line(2, y + Margin / 2, -2, y + Margin / 2, RAYWHITE);
    y += Margin;

    links.clear();
    auto const rows = box_rows();
    for (auto ix = 0uz; ix < rows.size(); ++ix) {
        auto const &row = rows[ix];
        auto const  x = (row.kind == InfoRow::Kind::Heading) ? Margin : EntryIndent;
        auto const  color = row.dim ? SmallColor : (row.kind == InfoRow::Kind::Heading) ? HeadingColor
                                                                                        : RAYWHITE;
        auto        small_x = x;
        if (!row.line.text.empty()) {
            render_text(x, y, row.line.text, FontSize::Medium, color);
            if (row.link) {
                links.push_back(InfoLink {
                    .row = ix,
                    .area = Rectangle { .x = viewport.x + x, .y = viewport.y + y, .width = text_width(row.line.text, FontSize::Medium), .height = medium.y },
                });
            }
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
    auto const hint = hint_text(GetTime());
    render_text(Margin, y, hint, FontSize::Small, SmallColor);
    hint_area = Rectangle {
        .x = viewport.x + Margin,
        .y = viewport.y + y,
        .width = text_width(hint, FontSize::Small),
        .height = small.y,
    };
}

}
