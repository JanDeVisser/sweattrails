/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the activity info popup (specs/48-device-info.md) without a
 * window: its rows, its hint, and its keys.
 */

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <widget/ActivityInfoBox.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace fs = std::filesystem;

namespace {

ActivityInfo recording(int number)
{
    return ActivityInfo {
        .file = std::format("activity/2026/11/{}.fit", number),
        .path = std::format("/store/activity/2026/11/{}.fit", number),
        .recording = std::format("Recording {} of 2", number),
        .device = { .text = "Garmin", .small = "(product 4315)" },
        .sensors = {
            SensorInfo {
                .name = { .text = "Garmin heart rate monitor", .small = "(product 4606)" },
                .details = "Serial 3615618938 · Battery ok",
            },
        },
    };
}

// A box on two recordings, which Tab switches between, that records
// what it was asked to show. resize() measures text, which needs fonts.
struct TestBox : public ActivityInfoBox {
    int                   shown { 1 };
    std::vector<int>      steps { };
    std::vector<fs::path> revealed { };

    TestBox()
        : ActivityInfoBox(nullptr, nullptr, nullptr)
    {
        describe_fnc = [this]() -> std::optional<ActivityInfo> { return recording(shown); };
        switch_fnc = [this](int step) {
            steps.push_back(step);
            shown = (shown == 1) ? 2 : 1;
        };
        reveal_fnc = [this](fs::path const &file) { revealed.push_back(file); };
    }

    void resize() override
    {
    }

    void open()
    {
        refresh();
        make_visible();
    }
};

void test_rows()
{
    using enum InfoRow::Kind;
    auto const                 rows = info_rows(recording(1));
    std::vector<InfoRow> const expected {
        { .kind = Heading, .line = { .text = "File" } },
        { .kind = Entry, .line = { .text = "activity/2026/11/1.fit" } },
        { .kind = Heading, .line = { .text = "Recorded with" } },
        { .kind = Entry, .line = { .text = "Garmin", .small = "(product 4315)" } },
        { .kind = Heading, .line = { .text = "Sensors" } },
        { .kind = Entry, .line = { .text = "Garmin heart rate monitor", .small = "(product 4606)" } },
        { .kind = Details, .line = { .small = "Serial 3615618938 · Battery ok" } },
    };
    CHECK(rows == expected);
}

void test_rows_without_file_or_sensors()
{
    using enum InfoRow::Kind;
    auto const                 rows = info_rows(ActivityInfo { .device = { .text = "Zwift" } });
    std::vector<InfoRow> const expected {
        { .kind = Heading, .line = { .text = "Recorded with" } },
        { .kind = Entry, .line = { .text = "Zwift" } },
        { .kind = Heading, .line = { .text = "Sensors" } },
        { .kind = Entry, .line = { .text = std::string { NoSensors } } },
    };
    CHECK(rows == expected);
}

void test_a_sensor_without_details_has_no_details_row()
{
    auto info = recording(1);
    info.sensors[0].details.clear();
    auto const rows = info_rows(info);
    CHECK_EQ(rows.size(), 6uz);
    CHECK(rows.back().kind == InfoRow::Kind::Entry);
}

void test_title_and_hint()
{
#ifdef __APPLE__
    std::string const reveal = "F: Show in Finder";
#else
    std::string const reveal = "F: Open folder";
#endif
    CHECK_EQ(info_title(recording(2)), "Activity info · Recording 2 of 2");
    CHECK_EQ(info_hint(recording(2)), reveal + " · Tab: next recording · Esc closes");
    ActivityInfo const one_recording { .file = "activity/2026/11/1.fit", .path = "/store/activity/2026/11/1.fit" };
    CHECK_EQ(info_title(one_recording), "Activity info");
    CHECK_EQ(info_hint(one_recording), reveal + " · Esc closes");
    // Without a file there's nothing to show.
    CHECK_EQ(info_hint(ActivityInfo { }), "Esc closes");
}

void test_esc_closes()
{
    auto box = std::make_shared<TestBox>();
    box->open();
    CHECK_EQ(App::the()->modals.size(), 1uz);
    CHECK(box->process_key(KModNone, KEY_ESCAPE));
    CHECK(App::the()->modals.empty());
    // Closed, it takes no more keys.
    CHECK(!box->process_key(KModNone, KEY_F));
    CHECK(box->revealed.empty());
}

void test_f_reveals_the_file()
{
    auto box = std::make_shared<TestBox>();
    box->open();
    CHECK(box->process_key(KModNone, KEY_F));
    CHECK(box->revealed == (std::vector<fs::path> { "/store/activity/2026/11/1.fit" }));
    CHECK(box->status == ModalStatus::Active);
    box->hide();
}

void test_f_without_a_file_does_nothing()
{
    auto box = std::make_shared<TestBox>();
    box->describe_fnc = []() -> std::optional<ActivityInfo> { return ActivityInfo { }; };
    box->open();
    CHECK(box->process_key(KModNone, KEY_F));
    CHECK(box->revealed.empty());
    box->hide();
}

void test_tab_switches_recordings()
{
    auto box = std::make_shared<TestBox>();
    box->open();
    CHECK(box->process_key(KModNone, KEY_TAB));
    CHECK_EQ(box->info.recording, "Recording 2 of 2");
    CHECK(box->process_key(KModShift, KEY_TAB));
    CHECK_EQ(box->info.recording, "Recording 1 of 2");
    CHECK(box->steps == (std::vector<int> { 1, -1 }));
    box->hide();
}

// With the activity gone, the box keeps what it showed.
void test_nothing_to_describe_keeps_the_info()
{
    auto box = std::make_shared<TestBox>();
    box->open();
    box->describe_fnc = []() -> std::optional<ActivityInfo> { return std::nullopt; };
    box->refresh();
    CHECK_EQ(box->info.file, "activity/2026/11/1.fit");
    box->hide();
}

void test_other_keys_are_not_taken()
{
    auto box = std::make_shared<TestBox>();
    box->open();
    CHECK(!box->process_key(KModNone, KEY_G));
    CHECK(!box->process_key(KModControl, KEY_F));
    CHECK(box->revealed.empty());
    box->hide();
}

}

int main()
{
    ScopedTestApp app;
    test_rows();
    test_rows_without_file_or_sensors();
    test_a_sensor_without_details_has_no_details_row();
    test_title_and_hint();
    test_esc_closes();
    test_f_reveals_the_file();
    test_f_without_a_file_does_nothing();
    test_tab_switches_recordings();
    test_nothing_to_describe_keeps_the_info();
    test_other_keys_are_not_taken();
    return test_result();
}
