/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "raylib.h"
#include <cmath>

#include <SweatTrails.h>
#include <Widget.h>

namespace ST {

enum class ModalStatus {
    Dormant = 0,
    Active,
    Submitted,
    Dismissed,
};

enum QueryOption : uint8_t {
    QueryOptionYes = 0x01,
    QueryOptionNo = 0x02,
    QueryOptionYesNo = 0x03,
    QueryOptionCancel = 0x04,
    QueryOptionYesNoCancel = 0x07,
    QueryOptionOK = 0x08,
    QueryOptionOKCancel = 0x0C,
};

struct Modal : public Widget {
    float       textsize { 1.0 };
    std::string prompt { };
    ModalStatus status { ModalStatus::Dormant };

    explicit Modal(std::string_view const &prompt)
        : Widget(App::the())
        , prompt(prompt)
    {
        background = DARKGRAY; // colour_to_color(SweatTrails::the()->theme.editor.bg);
    }

    virtual void dismiss()
    {
    }

    virtual void submit()
    {
    }

    void make_visible()
    {
        SweatTrails::the()->push_modal(self());
        status = ModalStatus::Active;
    }

    void hide()
    {
        SweatTrails::the()->pop_modal();
        status = ModalStatus::Dormant;
    }
};

template<typename Payload, bool Search = true, bool Sort = true, bool Shrink = false, typename ToString = std::nullptr_t>
struct ListBox : public Modal {
    using pListBox = std::shared_ptr<ListBox>;
    struct ListBoxEntry {
        std::string text;
        Payload     payload;

        ListBoxEntry(std::string_view const &text, Payload const &payload)
            : text(text)
            , payload(payload)
        {
        }

        auto operator<=>(ListBoxEntry const &other) const = default;
    };

    using ListBoxEntries = std::vector<ListBoxEntry>;
    ListBoxEntries entries { };
    ListBoxEntries matches { };
    std::string    search { };
    int            lines { 0 };
    int            top_line { 0 };
    int            selection { -1 };
    ModalStatus    status { ModalStatus::Dormant };
    ToString       to_string_fnc { nullptr };

    explicit ListBox(std::string_view const &prompt, ToString to_string = nullptr)
        : Modal(prompt)
        , to_string_fnc(std::move(to_string))
    {
    }

    void initialize() override
    {
        sort();
        filter();
        resize();
    }

    void draw_entries(size_t y_offset)
    {
        auto const &cell = SweatTrails::the()->cell;
        size_t      maxlen = (viewport.width - 28) / (cell.x * textsize);
        auto       &entries_to_draw = entries;
        if constexpr (Search) {
            if (!search.empty()) {
                entries_to_draw = matches;
            }
        }
        for (auto ix = top_line; ix < top_line + lines && ix < entries_to_draw.size(); ++ix) {
            auto const &e = entries_to_draw[ix];
            auto        text_color = RAYWHITE; // Theme::the().fg();
            if (ix == selection) {
                draw_rectangle(8, y_offset - 1, -8, cell.y * textsize + 1, RAYWHITE /*Theme::the().selection_bg()*/);
                text_color = DARKGRAY; // Theme::the().selection_fg();
            }
            std::string_view sv { e.text };
            if constexpr (!std::is_same<ToString, std::nullptr_t>::value) {
                assert(to_string_fnc != nullptr);
                sv = to_string_fnc(e.text, e.payload);
            } else if constexpr (std::is_convertible<Payload, std::string>()) {
                sv = std::string { e.payload };
            }
            if (sv.length() > maxlen) {
                sv = sv.substr(0, maxlen);
            }
            render_sized_text(10ul, y_offset, sv, SweatTrails::the()->font.value(), textsize, text_color);
            y_offset += (cell.y * textsize) + 2;
        }
    }

    void draw() override
    {
        auto bg = BLACK;    // Theme::the().bg();
        auto fg = RAYWHITE; // Theme::the().fg();
        draw_rectangle(0.0f, 0.0f, 0.0f, 0.0, bg);
        draw_outline(2ul, 2ul, -2.0f, -2.0f, fg);
        render_text(8, 8, prompt, SweatTrails::the()->font.value(), fg);
        render_text(-8, 8, search, SweatTrails::the()->font.value(), fg);
        draw_line(2, SweatTrails::the()->cell.y + 10, -2, SweatTrails::the()->cell.y + 10, fg);
        draw_entries(SweatTrails::the()->cell.y + 14);
    }

    void resize() override
    {
        auto const &screen = SweatTrails::the()->viewport;
        auto const &cell = SweatTrails::the()->cell;
        viewport.x = screen.width / 4;
        viewport.y = screen.height / 4;
        viewport.width = screen.width / 2;
        viewport.height = screen.height / 2;
        lines = (viewport.height - 19 + cell.y) / (cell.y + 2);
        if constexpr (Shrink && !Search) {
            lines = clamp(lines, 0, entries.size());
        }
        Vector2 m = MeasureTextEx(SweatTrails::the()->font.value(), prompt.c_str(), SweatTrails::the()->font->baseSize, 2);
        if (m.x > viewport.width - 16) {
            viewport.width = m.x + 16;
            viewport.x = (screen.width - m.x) / 2;
        }
        viewport.height = 19 + cell.y + lines * (cell.y + 2);
    }

    bool character(int ch) override
    {
        if constexpr (Search) {
            auto c = static_cast<char>(ch);
            search.append(std::string_view { &c, 1 });
            filter();
            return true;
        }
        return false;
    }

    bool process_key(KeyboardModifier modifier, int key) override
    {
        if (status == ModalStatus::Dormant) {
            return false;
        }
        size_t sz = entries.size();
        if constexpr (Search) {
            if (!search.empty()) {
                sz = matches.size();
            }
        }

        auto handle = [sz, this, key, modifier]() -> bool {
            if (modifier != KModNone) {
                return false;
            }
            switch (key) {
            case KEY_ESCAPE: {
                status = ModalStatus::Dismissed;
                dismiss();
                return true;
            }
            case KEY_ENTER:
            case KEY_KP_ENTER: {
                if (selection >= 0 && selection < sz) {
                    status = ModalStatus::Submitted;
                    submit();
                }
                return true;
            }
            case KEY_UP: {
                if (selection > 0 && selection < sz) {
                    --selection;
                    while (selection < top_line) {
                        --top_line;
                    }
                }
                return true;
            }
            case KEY_DOWN: {
                if (selection < sz - 1) {
                    ++selection;
                    while (selection > top_line + lines - 1) {
                        ++top_line;
                    }
                }
                return true;
            }
            case KEY_PAGE_UP: {
                if (selection >= lines) {
                    selection -= lines;
                    if (top_line > lines) {
                        top_line -= lines;
                    }
                }
                return true;
            }
            case KEY_PAGE_DOWN: {
                if (selection < sz - lines - 1) {
                    selection += lines;
                    top_line += lines;
                }
                return true;
            }
            case KEY_BACKSPACE: {
                if constexpr (Search) {
                    search.erase(search.length() - 1, 1);
                    filter();
                    return true;
                }
                return false;
            }
            default:
                return false;
            }
        };

        bool ret = handle();
        if (status != ModalStatus::Active) {
            SweatTrails::the()->pop_modal();
        }
        return ret;
    }

    void sort()
    {
        if constexpr (Sort) {
            std::sort(entries.begin(), entries.end());
        }
    }

    void filter()
    {
        matches.clear();
        if constexpr (Search) {
            if (search.empty()) {
                return;
            }
            for (auto const &e : entries) {
                std::string sv { e.text };
                if constexpr (!std::is_same<ToString, std::nullptr_t>::value) {
                    assert(to_string_fnc != nullptr);
                    sv = to_string_fnc(e.text, e.payload);
                } else if constexpr (std::is_convertible<Payload, std::string>()) {
                    sv = std::string { e.payload };
                }
                if (strcasestr(sv.c_str(), search.c_str()) != nullptr) {
                    matches.push_back(e);
                }
            }
            selection = 0;
        }
    }

    void refresh()
    {
        sort();
        filter();
        resize();
        selection = 0;
        top_line = 0;
        search = { };
        status = ModalStatus::Active;
    }

    void show()
    {
        refresh();
        make_visible();
    }
};

template<typename C, typename Submit, typename Dismiss = std::nullptr_t>
void input_box(std::shared_ptr<C> target, std::string_view const &prompt, Submit fnc, std::string_view const &def = "", Dismiss dismiss = nullptr)
{
    using pC = std::shared_ptr<C>;
    struct InputBox : public Modal {
        pC          target;
        std::string text;
        size_t      cursor { 0 };
        Submit      submit_fnc;
        Dismiss     dismiss_fnc;

        InputBox(pC target, std::string_view const &prompt, Submit submit, Dismiss dismiss = nullptr)
            : Modal(prompt)
            , target(target)
            , submit_fnc(submit)
            , dismiss_fnc(dismiss)
        {
            assert(submit_fnc != nullptr);
            background = BLACK; // Theme::the().bg();
        }

        void initialize() override
        {
            resize();
        }

        void submit() override
        {
            submit_fnc(target, text);
        }

        void dismiss() override
        {
            if constexpr (!std::is_same<Dismiss, std::nullptr_t>::value) {
                dismiss(target);
            }
        }

        bool character(int ch) override
        {
            if (cursor < text.length()) {
                text.insert(cursor, 1, static_cast<char>(ch));
            } else {
                text.append(1, static_cast<char>(ch));
            }
            return true;
        }

        bool process_key(KeyboardModifier modifier, int key) override
        {
            if (status == ModalStatus::Dormant) {
                return false;
            }

            auto handle = [this, modifier, key]() -> bool {
                if (modifier != KModNone) {
                    return false;
                }
                switch (key) {
                case KEY_ESCAPE: {
                    status = ModalStatus::Dismissed;
                    dismiss();
                    return true;
                }
                case KEY_ENTER:
                case KEY_KP_ENTER: {
                    status = ModalStatus::Submitted;
                    submit();
                    return true;
                }
                case KEY_LEFT: {
                    if (cursor > 0) {
                        --cursor;
                    }
                    return true;
                }
                case KEY_RIGHT: {
                    if (cursor < text.length()) {
                        ++cursor;
                    }
                    return true;
                }
                case KEY_BACKSPACE: {
                    if (cursor > 0) {
                        text.erase(cursor - 1, 1);
                        --cursor;
                    }
                    return true;
                }
                default:
                    return false;
                }
            };

            auto ret = handle();
            if (status != ModalStatus::Active) {
                hide();
            }
            return ret;
        }

        void show()
        {
            make_visible();
        }

        void resize() override
        {
            viewport.x = SweatTrails::the()->viewport.width / 4;
            viewport.y = SweatTrails::the()->viewport.height / 4;
            viewport.width = SweatTrails::the()->viewport.width / 2;
            viewport.height = 21 + 2 * SweatTrails::the()->cell.y;
        }

        void draw() override
        {
            draw_rectangle(0.0, 0.0, 0.0, 0.0, BLACK);
            draw_outline(2, 2, -2.0, -2.0, RAYWHITE);
            render_text(8, 8, prompt, SweatTrails::the()->font.value(), RAYWHITE);
            draw_line(2, SweatTrails::the()->cell.y + 10, -2, SweatTrails::the()->cell.y + 10, RAYWHITE);
            render_text(10, SweatTrails::the()->cell.y + 14, text, SweatTrails::the()->font.value(), RAYWHITE);
            double t = GetTime();
            if ((t - floor(t)) < 0.5) {
                draw_rectangle(10 + cursor * SweatTrails::the()->cell.x, SweatTrails::the()->cell.y + 12, 2, SweatTrails::the()->cell.y + 2, RAYWHITE);
            }
        }
    };
    auto const &inputbox = Widget::make<InputBox>(target, prompt, fnc, dismiss);
    inputbox->text = def;
    inputbox->show();
}

template<typename C, typename Submit>
void query_box(std::shared_ptr<C> target, std::string_view const &prompt, Submit fnc, QueryOption options)
{
    using pC = std::shared_ptr<C>;
    struct QueryBox : public ListBox<QueryOption, false, false, true> {
        pC     target;
        Submit submit_fnc;

        explicit QueryBox(pC const &target, std::string_view prompt, Submit submit, QueryOption options = QueryOptionOK)
            : ListBox(prompt)
            , target(target)
            , submit_fnc(std::move(submit))
        {
            if (options & QueryOptionYes) {
                entries.emplace_back("Yes", QueryOptionYes);
            }
            if (options & QueryOptionNo) {
                entries.emplace_back("No", QueryOptionNo);
            }
            if (options & QueryOptionCancel) {
                entries.emplace_back("Cancel", QueryOptionCancel);
            }
            if (options & QueryOptionOK) {
                entries.emplace_back("OK", QueryOptionOK);
            }
        }

        void submit() override
        {
            auto selected = entries[selection].payload;
            submit_fnc(target, selected);
        }
    };
    auto const &qbox = Widget::make<QueryBox>(target, prompt, fnc, options);
    qbox->show();
}

inline static void message_box(std::string_view const &prompt, QueryOption options = QueryOptionOK)
{
    auto dummy = [](auto const &, auto) {
    };
    pWidget p { nullptr };
    query_box<Widget>(p, prompt, dummy, options);
}

}
