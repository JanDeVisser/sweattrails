/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cmath>

#include <raylib.h>

#include <App.h>
#include <Scroll.h>
#include <TextLine.h>
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
        background = DARKGRAY; // TODO: Take this from a theme, once there is one.
    }

    virtual void dismiss()
    {
    }

    virtual void submit()
    {
    }

    void make_visible()
    {
        App::the()->push_modal(self());
        status = ModalStatus::Active;
    }

    void hide()
    {
        App::the()->pop_modal();
        status = ModalStatus::Dormant;
    }
};

struct ListBoxConfig {
    bool search { true };
    bool sort { true };
    bool shrink { false };
};

constexpr static ListBoxConfig DefaultListBoxConfig { };

template<typename Payload, ListBoxConfig Config = DefaultListBoxConfig, typename ToString = std::nullptr_t>
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
    ScrollRange    scroll { }; // Of the entries on display
    Scrollbar      scrollbar { };
    Rectangle      track { };     // The scrollbar's, where draw() drew it
    Rectangle      rows_area { }; // The entries', where draw() drew them
    int            selection { -1 };
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

    // The entries on display: the matches while there is a search, else all
    // of them. `selection` and `scroll.top_line` index this list.
    [[nodiscard]] ListBoxEntries const &shown() const
    {
        return (Config.search && !search.empty()) ? matches : entries;
    }

    // The selected entry. Only call this when there is one, as in submit().
    [[nodiscard]] ListBoxEntry const &selected() const
    {
        return shown()[static_cast<size_t>(selection)];
    }

    // The number of entries on display, as an int like selection and
    // scroll.top_line, so that the arithmetic on them can't wrap around.
    [[nodiscard]] int shown_count() const
    {
        return static_cast<int>(shown().size());
    }

    // Scrolls just far enough to have the selection on screen.
    void scroll_to_selection()
    {
        scroll.count = shown_count();
        scroll.scroll_to(selection);
    }

    void draw_entries(size_t y_offset)
    {
        auto const &cell = App::the()->cells[static_cast<size_t>(FontSize::Medium)];
        auto const  row_height = (cell.y * textsize) + 2;
        auto const  bar_width = scroll.overflows() ? cell.x : 0.0f;
        size_t      maxlen = (viewport.width - 28 - bar_width) / (cell.x * textsize);
        auto const &entries_to_draw = shown();
        rows_area = Rectangle { .x = viewport.x, .y = viewport.y + y_offset, .width = viewport.width - 10 - bar_width, .height = scroll.lines * row_height };
        track = Rectangle { .x = viewport.x + viewport.width - 8 - bar_width, .y = rows_area.y, .width = bar_width, .height = rows_area.height };
        Scrollbar::draw(track, scroll);
        for (int ix = scroll.top_line; ix < scroll.top_line + scroll.lines && ix < shown_count(); ++ix) {
            auto const &e = entries_to_draw[static_cast<size_t>(ix)];
            auto        text_color = RAYWHITE; // Theme::the().fg();
            if (ix == selection) {
                draw_rectangle(8, y_offset - 1, -8 - bar_width, cell.y * textsize + 1, RAYWHITE /*Theme::the().selection_bg()*/);
                text_color = DARKGRAY; // Theme::the().selection_fg();
            }
            // Keep the text in a string: a string_view of the temporaries
            // below would dangle.
            std::string text { e.text };
            if constexpr (!std::is_same<ToString, std::nullptr_t>::value) {
                assert(to_string_fnc != nullptr);
                text = to_string_fnc(e.text, e.payload);
            } else if constexpr (std::is_convertible<Payload, std::string>()) {
                text = std::string { e.payload };
            }
            std::string_view sv { text };
            if (sv.length() > maxlen) {
                sv = sv.substr(0, maxlen);
            }
            render_text(10ul, y_offset, sv, FontSize::Medium, text_color);
            y_offset += (cell.y * textsize) + 2;
        }
    }

    void draw() override
    {
        auto bg = BLACK;    // Theme::the().bg();
        auto fg = RAYWHITE; // Theme::the().fg();
        draw_rectangle(0.0f, 0.0f, 0.0f, 0.0, bg);
        draw_outline(2ul, 2ul, -2.0f, -2.0f, fg);
        render_text(8, 8, prompt, FontSize::Medium, fg);
        render_text(-8, 8, search, FontSize::Medium, fg);
        draw_line(2, App::the()->cell().y + 10, -2, App::the()->cell().y + 10, fg);
        draw_entries(App::the()->cell().y + 14);
    }

    void resize() override
    {
        auto const &screen = App::the()->viewport;
        auto const &cell = App::the()->cell();
        viewport.x = screen.width / 4;
        viewport.y = screen.height / 4;
        viewport.width = screen.width / 2;
        viewport.height = screen.height / 2;
        scroll.lines = (viewport.height - 19 + cell.y) / (cell.y + 2);
        if constexpr (Config.shrink && !Config.search) {
            scroll.lines = clamp(scroll.lines, 0, entries.size());
        }
        auto    f = App::the()->font();
        Vector2 m = MeasureTextEx(*f, prompt.c_str(), f->baseSize, 2);
        if (m.x > viewport.width - 16) {
            viewport.width = m.x + 16;
            viewport.x = (screen.width - m.x) / 2;
        }
        viewport.height = 19 + cell.y + scroll.lines * (cell.y + 2);
        // The number of lines may have changed.
        scroll_to_selection();
    }

    // The scrollbar and the mouse wheel. They scroll, and leave the
    // selection where it is.
    void process_input() override
    {
        handle_scrolling(ScrollInput::current());
    }

    void handle_scrolling(ScrollInput const &input)
    {
        scroll.count = shown_count();
        scrollbar.handle(scroll, track, rows_area, input);
    }

    bool character(int ch) override
    {
        if constexpr (Config.search) {
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
        auto const count = shown_count();

        auto handle = [count, this, key, modifier]() -> bool {
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
                if (selection >= 0 && selection < count) {
                    status = ModalStatus::Submitted;
                    submit();
                }
                return true;
            }
            case KEY_UP: {
                if (selection > 0) {
                    --selection;
                    scroll_to_selection();
                }
                return true;
            }
            case KEY_DOWN: {
                if (selection + 1 < count) {
                    ++selection;
                    scroll_to_selection();
                }
                return true;
            }
            case KEY_PAGE_UP: {
                if (count > 0) {
                    selection = std::max(0, selection - scroll.lines);
                    scroll.count = count;
                    scroll.page(-1);
                    scroll_to_selection();
                }
                return true;
            }
            case KEY_PAGE_DOWN: {
                if (count > 0) {
                    selection = std::min(count - 1, selection + scroll.lines);
                    scroll.count = count;
                    scroll.page(1);
                    scroll_to_selection();
                }
                return true;
            }
            case KEY_BACKSPACE: {
                if constexpr (Config.search) {
                    if (!search.empty()) {
                        search.pop_back();
                        filter();
                    }
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
            App::the()->pop_modal();
        }
        return ret;
    }

    void sort()
    {
        if constexpr (Config.sort) {
            std::sort(entries.begin(), entries.end());
        }
    }

    void filter()
    {
        matches.clear();
        if constexpr (Config.search) {
            // The list on display changes, so start at its top. Also when the
            // search is cleared: the selection indexed the matches.
            selection = 0;
            scroll.top_line = 0;
            if (search.empty()) {
                return;
            }
            for (auto const &e : entries) {
                std::string sv { e.text };
                if constexpr (!std::is_same_v<ToString, std::nullptr_t>) {
                    assert(to_string_fnc != nullptr);
                    sv = to_string_fnc(e.text, e.payload);
                } else if constexpr (std::is_convertible<Payload, std::string>()) {
                    sv = std::string { e.payload };
                }
                if (strcasestr(sv.c_str(), search.c_str()) != nullptr) {
                    matches.push_back(e);
                }
            }
        }
    }

    void refresh()
    {
        search = { };
        sort();
        filter();
        selection = 0;
        scroll.top_line = 0;
        resize();
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
        pC       target;
        TextLine line { };
        Submit   submit_fnc;
        Dismiss  dismiss_fnc;

        InputBox(pC target, std::string_view const &prompt, Submit submit, Dismiss dismiss)
            : Modal(prompt)
            , target(target)
            , submit_fnc(submit)
            , dismiss_fnc(dismiss)
        {
            background = BLACK; // Theme::the().bg();
        }

        void initialize() override
        {
            resize();
        }

        void submit() override
        {
            submit_fnc(target, line.utf8());
        }

        void dismiss() override
        {
            if constexpr (!std::is_same<Dismiss, std::nullptr_t>::value) {
                dismiss_fnc(target);
            }
        }

        bool character(int ch) override
        {
            line.insert(static_cast<rune>(ch));
            return true;
        }

        bool process_key(KeyboardModifier modifier, int key) override
        {
            if (status == ModalStatus::Dormant) {
                return false;
            }

            auto handle = [this, modifier, key]() -> bool {
                auto const clipboard = []() -> std::string {
                    auto const *text = GetClipboardText();
                    return (text != nullptr) ? std::string { text } : std::string { };
                };
                if (line.handle_key(modifier, key, clipboard)) {
                    return true;
                }
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
            viewport.x = App::the()->viewport.width / 4;
            viewport.y = App::the()->viewport.height / 4;
            viewport.width = App::the()->viewport.width / 2;
            viewport.height = 21 + 2 * App::the()->cell().y;
        }

        void draw() override
        {
            draw_rectangle(0.0, 0.0, 0.0, 0.0, BLACK);
            draw_outline(2, 2, -2.0, -2.0, RAYWHITE);
            render_text(8, 8, prompt, FontSize::Medium, RAYWHITE);
            draw_line(2, App::the()->cell().y + 10, -2, App::the()->cell().y + 10, RAYWHITE);
            draw_text();
        }

        // The text, scrolled sideways so that the cursor is in the box, and
        // the cursor after the text before it, as the font measures it.
        void draw_text()
        {
            auto const font = App::the()->font(FontSize::Medium);
            if (!font) {
                return;
            }
            auto const left = 10.0f;
            auto const width = viewport.width - 2 * left;
            auto const y = App::the()->cell().y + 14;
            auto const before = to_utf8(rune_view { line.text }.substr(0, line.cursor)).value_or(std::string { });
            auto const cursor_x = App::measure_text(before.c_str(), FontSize::Medium).x;
            auto const text_width = App::measure_text(line.utf8().c_str(), FontSize::Medium).x;
            auto const scroll = line.scroll_for(cursor_x, text_width, width - 2);

            BeginScissorMode(viewport.x + left, viewport.y + y - 2, width, App::the()->cell().y + 4);
            assert(sizeof(rune) == sizeof(int));
            DrawTextCodepoints(*font, reinterpret_cast<int const *>(line.text.c_str()), static_cast<int>(line.text.length()),
                Vector2 { viewport.x + left - scroll, viewport.y + y }, static_cast<float>(font->baseSize), 2, RAYWHITE);
            double t = GetTime();
            if ((t - floor(t)) < 0.5) {
                draw_rectangle_no_normalize(left + cursor_x - scroll, y - 2, 2, App::the()->cell().y + 2, RAYWHITE);
            }
            EndScissorMode();
        }
    };
    auto const &inputbox = Widget::make<InputBox>(target, prompt, fnc, dismiss);
    inputbox->line.set(def);
    inputbox->show();
}

template<typename C, typename Submit>
void query_box(std::shared_ptr<C> target, std::string_view const &prompt, Submit fnc, QueryOption options)
{
    using pC = std::shared_ptr<C>;
    struct QueryBox : public ListBox<QueryOption, ListBoxConfig { false, false, true }> {
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
            submit_fnc(target, selected().payload);
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
