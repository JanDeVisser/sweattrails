/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A box over the screen to edit a text of several lines in, such as an
 * activity's description: a TextArea, drawn. Enter starts a new line,
 * Cmd+Enter (Ctrl+Enter off macOS) saves, and Esc cancels, asking first
 * if the text was changed.
 */

#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include <raylib.h>

#include <Utf8.h>

#include <Modal.h>
#include <TextArea.h>

namespace ST {

namespace TextAreaBoxLayout {

constexpr size_t VisibleLines = 10;
constexpr float  Margin = 10.0f;

#ifdef __APPLE__
constexpr KeyboardModifier SaveModifier = KModSuper;
constexpr char const      *Hint = "Cmd+Enter saves, Esc cancels";
#else
constexpr KeyboardModifier SaveModifier = KModControl;
constexpr char const      *Hint = "Ctrl+Enter saves, Esc cancels";
#endif

}

template<typename C, typename Submit>
void text_area_box(std::shared_ptr<C> target, std::string_view const &prompt, Submit fnc, std::string_view const &text)
{
    using pC = std::shared_ptr<C>;
    using namespace TextAreaBoxLayout;

    struct TextAreaBox : public Modal {
        pC          target;
        Submit      submit_fnc;
        TextArea    area { };
        std::string original; // What Esc goes back to without asking

        TextAreaBox(pC target, std::string_view const &prompt, Submit submit, std::string_view const &text)
            : Modal(prompt)
            , target(target)
            , submit_fnc(submit)
            , original(text)
        {
            background = BLACK;
            area.set(text);
        }

        void initialize() override
        {
            resize();
        }

        void submit() override
        {
            submit_fnc(target, area.utf8());
        }

        [[nodiscard]] float row_height() const
        {
            return App::the()->cell().y + 2;
        }

        // Where the text goes, in the box.
        [[nodiscard]] Rectangle text_rect() const
        {
            auto const top = App::the()->cell().y + 2 * Margin;
            return Rectangle {
                .x = Margin,
                .y = top,
                .width = viewport.width - 2 * Margin,
                .height = static_cast<float>(VisibleLines) * row_height(),
            };
        }

        void resize() override
        {
            auto const &app = App::the()->viewport;
            viewport.x = app.width / 4;
            viewport.y = app.height / 4;
            viewport.width = app.width / 2;
            viewport.height = text_rect().y + text_rect().height + App::the()->cell().y + 2 * Margin;
            area.visible_lines = VisibleLines;
            area.set_width(text_rect().width - 2, [](rune_view piece) {
                return App::measure_text(to_utf8(piece).value_or(std::string { }).c_str(), FontSize::Medium).x;
            });
        }

        bool character(int ch) override
        {
            area.insert(static_cast<rune>(ch));
            return true;
        }

        bool process_key(KeyboardModifier modifier, int key) override
        {
            if (status == ModalStatus::Dormant) {
                return false;
            }
            if (modifier == SaveModifier && (key == KEY_ENTER || key == KEY_KP_ENTER)) {
                status = ModalStatus::Submitted;
                submit();
                hide();
                return true;
            }
            if (modifier == KModNone && key == KEY_ESCAPE) {
                cancel();
                return true;
            }
            auto const clipboard = []() -> std::string {
                auto const *text = GetClipboardText();
                return (text != nullptr) ? std::string { text } : std::string { };
            };
            area.handle_key(modifier, key, clipboard);
            // The box has the keys while it's open, as any modal does.
            return true;
        }

        // Closes the box, unless the text was changed and the user would
        // rather go back to it.
        void cancel()
        {
            if (area.utf8() == original) {
                status = ModalStatus::Dismissed;
                hide();
                return;
            }
            auto const box = std::dynamic_pointer_cast<TextAreaBox>(self());
            query_box(box, "Discard the changes?", [](std::shared_ptr<TextAreaBox> const &box, QueryOption answer) {
                if (answer == QueryOptionYes) {
                    box->status = ModalStatus::Dismissed;
                    box->hide();
                } }, QueryOptionYesNo);
        }

        // A click puts the cursor where it is in the text, and the wheel
        // scrolls.
        void process_input() override
        {
            auto const      text = text_rect();
            Rectangle const on_screen { .x = viewport.x + text.x, .y = viewport.y + text.y, .width = text.width, .height = text.height };
            auto const      mouse = GetMousePosition();
            if (!CheckCollisionPointRec(mouse, on_screen)) {
                return;
            }
            if (auto const wheel = GetMouseWheelMove(); wheel != 0) {
                area.scroll(wheel > 0 ? -1 : 1);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                auto const row = static_cast<size_t>(std::floor((mouse.y - on_screen.y) / row_height()));
                area.cursor = area.position_at(area.top_line + row, mouse.x - on_screen.x);
                area.scroll_to_cursor();
            }
        }

        void draw() override
        {
            draw_rectangle(0.0, 0.0, 0.0, 0.0, BLACK);
            draw_outline(2, 2, -2.0, -2.0, RAYWHITE);
            render_text(8, 8, prompt, FontSize::Medium, RAYWHITE);
            draw_line(2, App::the()->cell().y + 10, -2, App::the()->cell().y + 10, RAYWHITE);
            auto const text = text_rect();
            BeginScissorMode(static_cast<int>(viewport.x + text.x), static_cast<int>(viewport.y + text.y),
                static_cast<int>(text.width), static_cast<int>(text.height));
            for (auto row = 0uz; row < area.visible_lines && area.top_line + row < area.lines.size(); ++row) {
                auto const &[start, end] = area.lines[area.top_line + row];
                render_text(text.x, text.y + static_cast<float>(row) * row_height(),
                    rune_view { area.text }.substr(start, end - start), FontSize::Medium, RAYWHITE);
            }
            auto const cursor_row = area.line_of(area.cursor);
            auto const t = GetTime();
            if (cursor_row >= area.top_line && (t - std::floor(t)) < 0.5) {
                auto const y = text.y + static_cast<float>(cursor_row - area.top_line) * row_height();
                draw_rectangle_no_normalize(text.x + area.x_of(area.cursor), y, 2, row_height() - 2, RAYWHITE);
            }
            EndScissorMode();
            auto const hint = App::measure_text(Hint, FontSize::Small);
            render_text(viewport.width - Margin - hint.x, text.y + text.height + Margin, Hint, FontSize::Small, GRAY);
        }
    };

    auto const box = Widget::make<TextAreaBox>(target, prompt, fnc, text);
    box->make_visible();
}

}
