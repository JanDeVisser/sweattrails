/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

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
        App::the()->pop_modal(self());
        status = ModalStatus::Dormant;
    }
};

struct ListBoxConfig {
    bool search { true };
    bool sort { true };
    bool shrink { false };
};

constexpr static ListBoxConfig DefaultListBoxConfig { };

// The first `count` characters of UTF-8 `text`, not cutting one in two.
inline std::string_view first_chars(std::string_view text, size_t count)
{
    size_t ix = 0;
    for (; ix < text.size() && count > 0; --count) {
        ++ix;
        while (ix < text.size() && (static_cast<unsigned char>(text[ix]) & 0xC0) == 0x80) {
            ++ix;
        }
    }
    return text.substr(0, ix);
}

// The number of characters in UTF-8 `text`.
inline size_t char_count(std::string_view text)
{
    return static_cast<size_t>(std::ranges::count_if(text, [](char ch) { return (static_cast<unsigned char>(ch) & 0xC0) != 0x80; }));
}

template<typename Payload, ListBoxConfig Config = DefaultListBoxConfig, typename ToString = std::nullptr_t>
struct ListBox : public Modal {
    using pListBox = std::shared_ptr<ListBox>;
    struct ListBoxEntry {
        std::string              text; // What a search matches
        Payload                  payload;
        std::string              icon { };    // An image file, drawn before the row at its height
        std::string              prefix { };  // Drawn before the text, not searched
        std::vector<std::string> columns { }; // At the row's end, right-aligned, lined up across rows

        ListBoxEntry(std::string_view const &text, Payload const &payload)
            : text(text)
            , payload(payload)
        {
        }

        auto operator<=>(ListBoxEntry const &other) const = default;
    };

    using ListBoxEntries = std::vector<ListBoxEntry>;
    ListBoxEntries                   entries { };
    ListBoxEntries                   matches { };
    std::string                      search { };
    ScrollRange                      scroll { }; // Of the entries on display
    Scrollbar                        scrollbar { };
    Rectangle                        track { };     // The scrollbar's, where draw() drew it
    Rectangle                        rows_area { }; // The entries', where draw() drew them
    int                              selection { -1 };
    ToString                         to_string_fnc { nullptr };
    std::map<std::string, Texture2D> icons { }; // By file, loaded when first drawn

    explicit ListBox(std::string_view const &prompt, ToString to_string = nullptr)
        : Modal(prompt)
        , to_string_fnc(std::move(to_string))
    {
    }

    ~ListBox() override
    {
        for (auto const &[file, texture] : icons) {
            UnloadTexture(texture);
        }
    }

    // Selects the entry with `payload`, if it's on display, and scrolls to
    // it.
    void select(Payload const &payload)
    {
        auto const &entries_shown = shown();
        auto const  it = std::ranges::find_if(entries_shown, [&payload](ListBoxEntry const &e) { return e.payload == payload; });
        if (it != entries_shown.end()) {
            selection = static_cast<int>(std::distance(entries_shown.begin(), it));
            scroll_to_selection();
        }
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

    // An entry's text as it's shown: the text, or what to_string_fnc or the
    // payload make of it.
    [[nodiscard]] std::string shown_text(ListBoxEntry const &e) const
    {
        if constexpr (!std::is_same<ToString, std::nullptr_t>::value) {
            assert(to_string_fnc != nullptr);
            return to_string_fnc(e.text, e.payload);
        } else if constexpr (std::is_convertible<Payload, std::string>()) {
            return std::string { e.payload };
        }
        return e.text;
    }

    Texture2D const &icon(std::string const &file)
    {
        if (auto const it = icons.find(file); it != icons.end()) {
            return it->second;
        }
        auto texture = LoadTexture(file.c_str());
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        return icons[file] = texture;
    }

    // In characters: the prefix's width, and each column's, as wide as the
    // widest among the entries on display, so that they line up. The
    // columns are counted from the row's right end, so that a row with
    // fewer has its last one under the others' last.
    struct Widths {
        size_t              prefix { 0 };
        std::vector<size_t> columns { }; // From the right
        bool                icon { false };
    };

    [[nodiscard]] Widths widths() const
    {
        Widths ret;
        for (auto const &e : shown()) {
            ret.icon = ret.icon || !e.icon.empty();
            ret.prefix = std::max(ret.prefix, char_count(e.prefix));
            ret.columns.resize(std::max(ret.columns.size(), e.columns.size()));
            for (size_t from_right = 0; from_right < e.columns.size(); ++from_right) {
                auto const &column = e.columns[e.columns.size() - 1 - from_right];
                ret.columns[from_right] = std::max(ret.columns[from_right], char_count(column));
            }
        }
        return ret;
    }

    void draw_entries(size_t y_offset)
    {
        auto const &cell = App::the()->cells[static_cast<size_t>(FontSize::Medium)];
        auto const  row_height = (cell.y * textsize) + 2;
        auto const  char_width = cell.x * textsize;
        auto const  bar_width = scroll.overflows() ? cell.x : 0.0f;
        auto const &entries_to_draw = shown();
        rows_area = Rectangle { .x = viewport.x, .y = viewport.y + y_offset, .width = viewport.width - 10 - bar_width, .height = scroll.lines * row_height };
        track = Rectangle { .x = viewport.x + viewport.width - 8 - bar_width, .y = rows_area.y, .width = bar_width, .height = rows_area.height };
        Scrollbar::draw(track, scroll);

        // The row, from the left: the icon, the prefix and the text, and the
        // columns at its right end, two characters apart.
        auto const w = widths();
        auto const text_x = 10.0f + (w.icon ? row_height + char_width : 0.0f) + (w.prefix > 0 ? static_cast<float>(w.prefix + 2) * char_width : 0.0f);
        auto       columns_width = 0.0f;
        for (auto const width : w.columns) {
            columns_width += static_cast<float>(width + 2) * char_width;
        }
        auto const right = viewport.width - 18 - bar_width;
        auto const maxlen = static_cast<size_t>(std::max(0.0f, (right - columns_width - text_x) / char_width));
        for (int ix = scroll.top_line; ix < scroll.top_line + scroll.lines && ix < shown_count(); ++ix) {
            auto const &e = entries_to_draw[static_cast<size_t>(ix)];
            auto        text_color = RAYWHITE; // Theme::the().fg();
            if (ix == selection) {
                draw_rectangle(8, y_offset - 1, -8 - bar_width, cell.y * textsize + 1, RAYWHITE /*Theme::the().selection_bg()*/);
                text_color = DARKGRAY; // Theme::the().selection_fg();
            }
            if (!e.icon.empty()) {
                auto const &texture = icon(e.icon);
                auto const  side = row_height - 2;
                DrawTexturePro(texture, Rectangle { 0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height) },
                    Rectangle { viewport.x + 10, viewport.y + y_offset, side, side }, Vector2 { }, 0, text_color);
            }
            if (!e.prefix.empty()) {
                render_text(text_x - static_cast<float>(w.prefix + 2) * char_width, y_offset, e.prefix, FontSize::Medium, text_color);
            }
            // Keep the text in a string: a string_view of a temporary would
            // dangle.
            auto const text = shown_text(e);
            render_text(text_x, y_offset, first_chars(text, maxlen), FontSize::Medium, text_color);
            auto x = right;
            for (size_t from_right = 0; from_right < e.columns.size(); ++from_right) {
                auto const &column = e.columns[e.columns.size() - 1 - from_right];
                render_text(x - static_cast<float>(char_count(column)) * char_width, y_offset, column, FontSize::Medium, text_color);
                x -= static_cast<float>(w.columns[from_right] + 2) * char_width;
            }
            y_offset += row_height;
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
            App::the()->pop_modal(self());
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
                auto const sv = shown_text(e);
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

// A list of entries to pick one of, each a text and a payload. Enter calls
// `submit(target, payload)` with the selected one's. The entry `initial`,
// if there's one with that text, is selected when the box opens.
template<typename C, typename Payload, typename Submit>
struct PickBox : public ListBox<Payload> {
    using pC = std::shared_ptr<C>;
    using Entries = std::vector<std::pair<std::string, Payload>>;

    pC     target;
    Submit submit_fnc;

    PickBox(pC target, std::string_view prompt, Entries const &entries, Submit submit)
        : ListBox<Payload>(prompt)
        , target(std::move(target))
        , submit_fnc(std::move(submit))
    {
        for (auto const &[text, payload] : entries) {
            this->entries.emplace_back(text, payload);
        }
    }

    // Selects the entry with this text, if there is one.
    void select(std::string_view text)
    {
        auto const &shown = this->shown();
        for (auto ix = 0uz; ix < shown.size(); ++ix) {
            if (shown[ix].text == text) {
                this->selection = static_cast<int>(ix);
                this->scroll_to_selection();
                return;
            }
        }
    }

    void submit() override
    {
        submit_fnc(target, this->selected().payload);
    }
};

template<typename C, typename Payload, typename Submit>
void list_box(std::shared_ptr<C> target, std::string_view const &prompt, std::vector<std::pair<std::string, Payload>> const &entries, Submit fnc, std::string_view initial = "")
{
    auto const &box = Widget::make<PickBox<C, Payload, Submit>>(target, prompt, entries, fnc);
    // After show(), which selects the first entry.
    box->show();
    box->select(initial);
}

inline static void message_box(std::string_view const &prompt, QueryOption options = QueryOptionOK)
{
    auto dummy = [](auto const &, auto) {
    };
    pWidget p { nullptr };
    query_box<Widget>(p, prompt, dummy, options);
}

}
