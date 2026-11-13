/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the text of the activity info popup (specs/48-device-info.md),
 * on activities made by hand.
 */

#include <filesystem>
#include <string>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <widget/ActivityInfo.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace fs = std::filesystem;

namespace {

fs::path const Store { "/Users/someone/Library/Application Support/sweattrails" };

struct Made {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };

    Made()
    {
        activity.sidecar = Store / "activity/2026/11/1794572226.json";
        activity.manufacturer = FIT::manufacturer::garmin;
        activity.product = 4315;
    }
};

Sensor heart_rate_strap()
{
    return Sensor {
        .source = FIT::source_type::antplus,
        .device_type = 120,
        .manufacturer = FIT::manufacturer::garmin,
        .product = 4606,
        .serial_number = 3615618938,
        .software_version = 5.1f,
        .battery = FIT::battery_status::ok,
    };
}

void test_the_file_is_relative_to_the_store()
{
    Made       t;
    auto const info = describe(t.activity, Store);
    CHECK_EQ(info.file, "activity/2026/11/1794572226.fit");
    CHECK_EQ(info.path.string(), (Store / "activity/2026/11/1794572226.fit").string());
}

void test_a_recordings_file_is_in_its_folder()
{
    Made t;
    t.activity.sidecar = Store / "activity/2026/11/1794572226.recordings/1794572230.json";
    CHECK_EQ(describe(t.activity, Store).file, "activity/2026/11/1794572226.recordings/1794572230.fit");
}

void test_which_recording()
{
    Made t;
    CHECK_EQ(describe(t.activity, Store).recording, "");
    t.activity.recordings = 2;
    t.activity.recording_number = 2;
    CHECK_EQ(describe(t.activity, Store).recording, "Recording 2 of 2");
}

void test_a_file_outside_the_store_has_its_full_path()
{
    Made t;
    t.activity.sidecar = "/tmp/ride.json";
    CHECK_EQ(describe(t.activity, Store).file, "/tmp/ride.fit");
}

void test_no_file_without_a_sidecar()
{
    Made t;
    t.activity.sidecar.clear();
    auto const info = describe(t.activity, Store);
    CHECK_EQ(info.file, "");
    CHECK(info.path.empty());
}

void test_the_device_by_product_number()
{
    Made t;
    CHECK(describe(t.activity, Store).device == (InfoLine { .text = "Garmin", .small = "(product 4315)" }));
}

void test_the_device_by_product_name()
{
    Made t;
    t.activity.manufacturer = FIT::manufacturer::wahoo_fitness;
    t.activity.product_name = "ELEMNT BOLT";
    CHECK(describe(t.activity, Store).device == (InfoLine { .text = "Wahoo ELEMNT BOLT", .small = "" }));
}

void test_an_unknown_device()
{
    Made t;
    t.activity.manufacturer.reset();
    t.activity.product.reset();
    CHECK(describe(t.activity, Store).device == (InfoLine { .text = "Unknown device", .small = "" }));
}

void test_a_sensor()
{
    Made t;
    t.activity.sensors = { heart_rate_strap() };
    auto const info = describe(t.activity, Store);
    CHECK_EQ(info.sensors.size(), 1uz);
    if (info.sensors.empty()) {
        return;
    }
    CHECK_EQ(info.sensors[0].name.text, "Garmin heart rate monitor");
    CHECK_EQ(info.sensors[0].name.small, "(product 4606)");
    CHECK_EQ(info.sensors[0].details, "Serial 3615618938 · Software 5.10 · Battery ok");
}

void test_a_sensors_missing_values_are_left_out()
{
    Made t;
    t.activity.sensors = { Sensor { .source = FIT::source_type::bluetooth_low_energy, .device_type = 2, .battery = FIT::battery_status::unknown } };
    auto const info = describe(t.activity, Store);
    if (info.sensors.size() != 1) {
        CHECK_EQ(info.sensors.size(), 1uz);
        return;
    }
    CHECK_EQ(info.sensors[0].name.text, "Power meter");
    CHECK_EQ(info.sensors[0].name.small, "");
    CHECK_EQ(info.sensors[0].details, "");
}

// A Wahoo ELEMNT ROAM names its sensors by their type, "Radar" or
// "Power": with a type it knows, the popup goes by that instead.
void test_a_known_types_product_name_is_left_out()
{
    Made t;
    auto strap = heart_rate_strap();
    strap.product_name = "Heartrate";
    strap.battery = FIT::battery_status::new_;
    auto const power = Sensor {
        .source = FIT::source_type::antplus,
        .device_type = 11,
        .manufacturer = FIT::manufacturer::sram,
        .product = 1052,
        .product_name = "Power",
    };
    auto const shifting = Sensor { .source = FIT::source_type::antplus, .device_type = 34, .product_name = "Gear Selection" };
    t.activity.sensors = { strap, power, shifting };
    auto const info = describe(t.activity, Store);
    if (info.sensors.size() != 3) {
        CHECK_EQ(info.sensors.size(), 3uz);
        return;
    }
    CHECK(info.sensors[0].name == (InfoLine { .text = "Garmin heart rate monitor", .small = "(product 4606)" }));
    CHECK_EQ(info.sensors[0].details, "Serial 3615618938 · Software 5.10 · Battery new");
    CHECK(info.sensors[1].name == (InfoLine { .text = "SRAM power meter", .small = "(product 1052)" }));
    // Without a maker, the type starts the line.
    CHECK(info.sensors[2].name == (InfoLine { .text = "Shifting", .small = "" }));
}

// Of a type it doesn't know, the product name is all it has.
void test_an_unknown_types_product_name_is_kept()
{
    Made t;
    t.activity.sensors = {
        Sensor { .source = FIT::source_type::antplus, .device_type = 250, .manufacturer = FIT::manufacturer::garmin, .product = 9, .product_name = "Varia" },
        Sensor { .source = FIT::source_type::antplus, .device_type = 250, .manufacturer = FIT::manufacturer::garmin, .product = 9 },
    };
    auto const info = describe(t.activity, Store);
    if (info.sensors.size() != 2) {
        CHECK_EQ(info.sensors.size(), 2uz);
        return;
    }
    CHECK(info.sensors[0].name == (InfoLine { .text = "Garmin Varia", .small = "" }));
    CHECK(info.sensors[1].name == (InfoLine { .text = "Garmin sensor", .small = "(product 9)" }));
}

void test_sensor_types()
{
    auto const type = [](std::optional<FIT::source_type> source, std::optional<u8> device_type) {
        return sensor_type_name(Sensor { .source = source, .device_type = device_type });
    };
    CHECK_EQ(type(FIT::source_type::antplus, 11), "power meter");
    CHECK_EQ(type(FIT::source_type::antplus, 40), "radar");
    CHECK_EQ(type(FIT::source_type::antplus, 121), "speed and cadence sensor");
    CHECK_EQ(type(FIT::source_type::bluetooth_low_energy, 1), "heart rate monitor");
    // A Wahoo ELEMNT ROAM lists BLE sensors with ANT+'s types.
    CHECK_EQ(type(FIT::source_type::bluetooth_low_energy, 120), "heart rate monitor");
    CHECK_EQ(type(FIT::source_type::bluetooth_low_energy, 99), "sensor"); // Neither's
    CHECK_EQ(type(std::nullopt, 120), "heart rate monitor");              // Older files: ANT+
    CHECK_EQ(type(FIT::source_type::antplus, 250), "sensor");
    CHECK_EQ(type(FIT::source_type::antplus, std::nullopt), "sensor");
    CHECK_EQ(type(FIT::source_type::wifi, 1), "sensor");
}

void test_no_sensors()
{
    Made t;
    CHECK(describe(t.activity, Store).sensors.empty());
}

}

int main()
{
    test_the_file_is_relative_to_the_store();
    test_a_recordings_file_is_in_its_folder();
    test_which_recording();
    test_a_file_outside_the_store_has_its_full_path();
    test_no_file_without_a_sidecar();
    test_the_device_by_product_number();
    test_the_device_by_product_name();
    test_an_unknown_device();
    test_a_sensor();
    test_a_sensors_missing_values_are_left_out();
    test_a_known_types_product_name_is_left_out();
    test_an_unknown_types_product_name_is_kept();
    test_sensor_types();
    test_no_sensors();
    return test_result();
}
