/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The activity info popup (specs/48-device-info.md): the FIT file of the
 * activity on display, the device that recorded it and its sensors. Esc
 * closes it, F shows the file in Finder, and Tab and Shift+Tab switch
 * recordings, as they do without the popup.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <raylib.h>

#include <Modal.h>

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

    bool operator==(InfoRow const &) const = default;
};

[[nodiscard]] std::vector<InfoRow> info_rows(ActivityInfo const &info);

// The line at the top: "Activity info", and which recording for one of
// several.
[[nodiscard]] std::string info_title(ActivityInfo const &info);

// The line at the bottom: what F does, Tab for several recordings, and Esc.
[[nodiscard]] std::string info_hint(ActivityInfo const &info);

struct ActivityInfoBox : public Modal {
    // What the popup shows: the activity on display, or nothing without one.
    using Describe = std::function<std::optional<ActivityInfo>()>;
    // Shows the next (+1) or previous (-1) recording.
    using SwitchRecording = std::function<void(int)>;
    using Reveal = std::function<void(std::filesystem::path const &)>;

    Describe        describe_fnc;
    SwitchRecording switch_fnc;
    Reveal          reveal_fnc;
    ActivityInfo    info { };
    Rectangle       hint_area { }; // Where draw() drew the hint, for a click

    ActivityInfoBox(Describe describe, SwitchRecording switch_recording, Reveal reveal);

    // Describes the activity on display, and sizes the box for it.
    void initialize() override;
    // Describes the activity on display again.
    void refresh();
    void show();
    void reveal();

    bool process_key(KeyboardModifier modifier, int key) override;
    void process_input() override;
    void resize() override;
    void draw() override;

private:
    [[nodiscard]] static float row_height(InfoRow::Kind kind);
};

}
