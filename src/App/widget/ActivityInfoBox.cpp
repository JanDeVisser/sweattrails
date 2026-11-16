/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>

#include <StringUtil.h>

#include <App.h>

#include <widget/ActivityInfoBox.h>

namespace ST {

namespace {

#ifdef __APPLE__
constexpr char const *RevealName = "Show in Finder";
#else
constexpr char const *RevealName = "Open folder";
#endif

}

std::vector<InfoRow> info_rows(ActivityInfo const &info)
{
    using enum InfoRow::Kind;
    std::vector<InfoRow> ret;
    if (!info.file.empty()) {
        ret.push_back({ .kind = Heading, .line = { .text = "File" } });
        ret.push_back({ .kind = Entry, .line = { .text = info.file }, .link = true });
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
        keys.emplace_back("C: copy path");
    }
    if (!info.recording.empty()) {
        keys.emplace_back("Tab: next recording");
    }
    keys.emplace_back("Esc closes");
    return join(keys, " · ");
}

ActivityInfoBox::ActivityInfoBox(Describe describe, SwitchRecording switch_recording, Reveal reveal, Copy copy)
    : InfoBox("Activity info")
    , describe_fnc(std::move(describe))
    , switch_fnc(std::move(switch_recording))
    , reveal_fnc(std::move(reveal))
    , copy_fnc(std::move(copy))
{
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

void ActivityInfoBox::copy(double now)
{
    if (!info.path.empty()) {
        copy_fnc(info.path.string());
        copied_at = now;
    }
}

std::string ActivityInfoBox::box_title() const
{
    return info_title(info);
}

std::vector<InfoRow> ActivityInfoBox::box_rows() const
{
    return info_rows(info);
}

std::string ActivityInfoBox::hint_text(double now) const
{
    if (copied_at && now - *copied_at < CopiedSeconds) {
        return "Copied";
    }
    return info_hint(info);
}

void ActivityInfoBox::click_link(double now)
{
    copy(now);
}

void ActivityInfoBox::click_hint()
{
    reveal();
}

bool ActivityInfoBox::hint_clickable() const
{
    return !info.path.empty();
}

bool ActivityInfoBox::process_key(KeyboardModifier modifier, int key)
{
    if (status == ModalStatus::Dormant) {
        return false;
    }
    if (key == KEY_TAB && (modifier == KModNone || modifier == KModShift)) {
        switch_fnc(modifier == KModShift ? -1 : 1);
        copied_at.reset();
        refresh();
        resize();
        return true;
    }
    if (modifier == KModNone && key == KEY_F) {
        reveal();
        return true;
    }
    if (modifier == KModNone && key == KEY_C) {
        copy(GetTime());
        return true;
    }
    return InfoBox::process_key(modifier, key);
}

}
