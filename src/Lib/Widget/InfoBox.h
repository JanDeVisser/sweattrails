/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * A popup of headings, entries under them and their details, with a title
 * at the top and a hint at the bottom, which Esc closes: the activity
 * info box (specs/48-device-info.md) and the weather box
 * (specs/59-weather.md). A subclass says what's in it.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <raylib.h>

#include <Modal.h>
#include <Scroll.h>

namespace ST {

// A line of the popup: `text` in the normal font, then `small` in the
// smaller one, either of which can be empty.
struct InfoLine {
    std::string text { };
    std::string small { };

    bool operator==(InfoLine const &) const = default;
};

// A row of the popup, from the top: a heading, an entry under it, or an
// entry's details, in the smaller font.
struct InfoRow {
    enum class Kind {
        Heading,
        Entry,
        Details,
    };

    Kind     kind { Kind::Entry };
    InfoLine line { };
    bool     link { false }; // A click on it calls click_link(): one row at most

    bool operator==(InfoRow const &) const = default;
};

struct InfoBox : public Modal {
    Rectangle hint_area { }; // Where draw() drew the hint, for a click
    Rectangle link_area { }; // Where draw() drew the link row's text, likewise

    explicit InfoBox(std::string_view const &name);

    [[nodiscard]] virtual std::string          box_title() const = 0;
    [[nodiscard]] virtual std::vector<InfoRow> box_rows() const = 0;
    // The line at the bottom at `now`, as GetTime().
    [[nodiscard]] virtual std::string hint_text(double now) const = 0;

    // What a click on the link row, or on the hint, does: nothing, unless a
    // subclass says. The hand cursor shows over the hint only if it's
    // clickable.
    virtual void               click_link(double now);
    virtual void               click_hint();
    [[nodiscard]] virtual bool hint_clickable() const;

    void close();

    // Esc closes.
    bool process_key(KeyboardModifier modifier, int key) override;
    void process_input() override;
    void handle_input(ScrollInput const &input);
    void resize() override;
    void draw() override;

private:
    [[nodiscard]] static float row_height(InfoRow::Kind kind);
};

}
