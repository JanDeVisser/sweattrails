/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The activity info popup (specs/48-device-info.md): the FIT file of the
 * activity on display, the device that recorded it and its sensors. Esc
 * closes it, F shows the file in Finder, C copies its path (specs/53-
 * small-ui-upgrades.md), and Tab and Shift+Tab switch recordings, as they
 * do without the popup.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include <Modal.h>
#include <Scroll.h>

#include <widget/ActivityInfo.h>

namespace ST {

// A row of the popup, from the top: a heading, such as "Sensors", an
// entry under it, or an entry's details, in the smaller font.
struct InfoRow {
    enum class Kind {
        Heading,
        Entry,
        Details,
    };

    Kind     kind { Kind::Entry };
    InfoLine line { };
    bool     file { false }; // The file's name, which a click copies

    bool operator==(InfoRow const &) const = default;
};

[[nodiscard]] std::vector<InfoRow> info_rows(ActivityInfo const &info);

// The line at the top: "Activity info", and which recording for one of
// several.
[[nodiscard]] std::string info_title(ActivityInfo const &info);

// The line at the bottom: what F and C do, Tab for several recordings,
// and Esc.
[[nodiscard]] std::string info_hint(ActivityInfo const &info);

struct ActivityInfoBox : public Modal {
    // What the popup shows: the activity on display, or nothing without one.
    using Describe = std::function<std::optional<ActivityInfo>()>;
    // Shows the next (+1) or previous (-1) recording.
    using SwitchRecording = std::function<void(int)>;
    using Reveal = std::function<void(std::filesystem::path const &)>;
    // Puts the text on the clipboard.
    using Copy = std::function<void(std::string const &)>;

    // How long "Copied" shows in place of the hint.
    static constexpr double CopiedSeconds = 2.0;

    Describe              describe_fnc;
    SwitchRecording       switch_fnc;
    Reveal                reveal_fnc;
    Copy                  copy_fnc;
    ActivityInfo          info { };
    Rectangle             hint_area { }; // Where draw() drew the hint, for a click
    Rectangle             file_area { }; // Where draw() drew the file's line, likewise
    std::optional<double> copied_at { }; // When the path was copied, as GetTime()

    ActivityInfoBox(Describe describe, SwitchRecording switch_recording, Reveal reveal, Copy copy);

    // Describes the activity on display, and sizes the box for it.
    void initialize() override;
    // Describes the activity on display again.
    void refresh();
    void show();
    void reveal();
    // Copies the file's full path, at `now`, as GetTime().
    void copy(double now);
    // The line at the bottom at `now`: "Copied" for a while after a copy,
    // else info_hint().
    [[nodiscard]] std::string hint_text(double now) const;

    bool process_key(KeyboardModifier modifier, int key) override;
    void process_input() override;
    // A click on the hint shows the file, and one on the file's line
    // copies its path.
    void handle_input(ScrollInput const &input);
    void resize() override;
    void draw() override;

private:
    [[nodiscard]] static float row_height(InfoRow::Kind kind);
};

}
