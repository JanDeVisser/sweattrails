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

#include <InfoBox.h>

#include <widget/ActivityInfo.h>

namespace ST {

// The rows under the title: the file, the device, and the sensors.
[[nodiscard]] std::vector<InfoRow> info_rows(ActivityInfo const &info);

// The line at the top: "Activity info", and which recording for one of
// several.
[[nodiscard]] std::string info_title(ActivityInfo const &info);

// The line at the bottom: what F and C do, Tab for several recordings,
// and Esc.
[[nodiscard]] std::string info_hint(ActivityInfo const &info);

struct ActivityInfoBox : public InfoBox {
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

    [[nodiscard]] std::string          box_title() const override;
    [[nodiscard]] std::vector<InfoRow> box_rows() const override;
    // "Copied" for a while after a copy, else info_hint().
    [[nodiscard]] std::string hint_text(double now) const override;

    // A click on the file's line copies its path, and one on the hint
    // shows the file.
    void               click_link(size_t row, double now) override;
    void               click_hint() override;
    [[nodiscard]] bool hint_clickable() const override;

    bool process_key(KeyboardModifier modifier, int key) override;
};

}
