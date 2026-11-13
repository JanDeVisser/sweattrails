/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests reading the sensors of an activity from its FIT file's
 * device_info messages (specs/48-device-info.md), with FIT files built
 * byte by byte with TestFit.h.
 */

#include <optional>
#include <string>

#include <storage/Activity.h>
#include <storage/Devices.h>
#include <storage/Storage.h>

#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using namespace ST::Test;

namespace {

constexpr u8 Uint32z = 0x8C;

constexpr u8  AntPlus = 1;
constexpr u8  BluetoothLE = 3;
constexpr u8  Local = 5;
constexpr u8  NoSource = 0xFF;
constexpr u8  HeartRate = 120; // ANT+
constexpr u8  BlePower = 2;
constexpr u8  Barometer = 4; // Local
constexpr u8  BatteryOk = 3;
constexpr u8  BatteryLow = 4;
constexpr u8  NoBattery = 0xFF;
constexpr u16 Garmin = 1;
constexpr u16 Favero = 263;
constexpr u32 EdgeSerial = 3439796858;

// What a device_info message says. Left out, a value is written as FIT's
// invalid value, as devices do.
struct Listing {
    u8                 index { 0 };
    u8                 source { NoSource };
    u8                 type { 0xFF };
    u16                manufacturer { 0xFFFF };
    std::optional<u32> serial { };
    u16                product { 0xFFFF };
    u16                software { 0xFFFF }; // In hundredths
    u8                 battery { NoBattery };
};

void add_device_info(FITBuilder &fit, Listing const &listing)
{
    fit.definition(6, 23,
        {
            { .num = 253, .size = 4, .base_type = Uint32 },
            { .num = 0, .size = 1, .base_type = Uint8 },
            { .num = 1, .size = 1, .base_type = Uint8 },
            { .num = 2, .size = 2, .base_type = Uint16 },
            { .num = 3, .size = 4, .base_type = Uint32z },
            { .num = 4, .size = 2, .base_type = Uint16 },
            { .num = 5, .size = 2, .base_type = Uint16 },
            { .num = 11, .size = 1, .base_type = Uint8 },
            { .num = 25, .size = 1, .base_type = Enum },
        });
    fit.message(6);
    fit.u32_le(Start);
    fit.byte(listing.index);
    fit.byte(listing.type);
    fit.u16_le(listing.manufacturer);
    fit.u32_le(listing.serial.value_or(0));
    fit.u16_le(listing.product);
    fit.u16_le(listing.software);
    fit.byte(listing.battery);
    fit.byte(listing.source);
}

// An Edge with its barometer, a heart rate strap and a BLE power meter,
// listed at the start and, the sensors with other battery statuses, at
// the end. The power meter only says its product at the start.
std::string edge_ride()
{
    FITBuilder fit;
    add_file_id(fit, Garmin);
    auto const edge = Listing { .index = 0, .source = Local, .manufacturer = Garmin, .serial = EdgeSerial, .product = 4315, .software = 2905 };
    auto const barometer = Listing { .index = 1, .source = Local, .type = Barometer, .manufacturer = Garmin, .product = 4315 };
    auto       strap = Listing { .index = 3, .source = AntPlus, .type = HeartRate, .manufacturer = Garmin, .serial = 3615618938, .product = 4606, .software = 510, .battery = BatteryOk };
    auto       power = Listing { .index = 2, .source = BluetoothLE, .type = BlePower, .manufacturer = Favero, .serial = 1234, .product = 22, .battery = BatteryOk };
    for (auto const &listing : { edge, barometer, strap, power }) {
        add_device_info(fit, listing);
    }
    add_session(fit, 60, 10);
    add_records(fit, 60, 140, 200);
    strap.battery = BatteryLow;
    power.battery = NoBattery;
    power.product = 0xFFFF;
    power.software = 312;
    for (auto const &listing : { edge, barometer, strap, power }) {
        add_device_info(fit, listing);
    }
    return fit.file();
}

struct Loaded {
    Storage  storage { };
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    bool     loaded { false };

    Loaded(std::string const &file, LoadingDepth depth)
    {
        activity.depth = depth;
        auto const res = activity.load_from_slice(file);
        loaded = res.has_value() && *res;
    }
};

void test_sensors_are_read_on_a_deep_load()
{
    Loaded t { edge_ride(), LoadingDepth::Deep };
    CHECK(t.loaded);
    // By index: the power meter, then the strap. Not the Edge, nor its
    // barometer.
    CHECK_EQ(t.activity.sensors.size(), 2uz);
    if (t.activity.sensors.size() != 2) {
        return;
    }
    auto const &power = t.activity.sensors[0];
    CHECK(power.source == FIT::source_type::bluetooth_low_energy);
    CHECK_EQ(static_cast<int>(power.device_type.value_or(0)), static_cast<int>(BlePower));
    CHECK(power.manufacturer == FIT::manufacturer::favero_electronics);
    auto const &strap = t.activity.sensors[1];
    CHECK(strap.source == FIT::source_type::antplus);
    CHECK_EQ(static_cast<int>(strap.device_type.value_or(0)), static_cast<int>(HeartRate));
    CHECK_EQ(strap.serial_number.value_or(0), 3615618938u);
    CHECK_EQ(strap.product.value_or(0), 4606u);
    CHECK(strap.software_version && std::abs(*strap.software_version - 5.10f) < 0.001f);
}

void test_the_last_listing_wins()
{
    Loaded t { edge_ride(), LoadingDepth::Deep };
    if (t.activity.sensors.size() != 2) {
        CHECK_EQ(t.activity.sensors.size(), 2uz);
        return;
    }
    auto const &power = t.activity.sensors[0];
    CHECK(t.activity.sensors[1].battery == FIT::battery_status::low);
    CHECK(power.software_version && std::abs(*power.software_version - 3.12f) < 0.001f);
    // What only the first listing said stays.
    CHECK(power.battery == FIT::battery_status::ok);
    CHECK_EQ(power.product.value_or(0), 22u);
}

void test_the_recording_devices_product_is_kept()
{
    FITBuilder fit;
    fit.definition(4, 0,
        {
            { .num = 0, .size = 1, .base_type = Enum },
            { .num = 1, .size = 2, .base_type = Uint16 },
            { .num = 2, .size = 2, .base_type = Uint16 },
            { .num = 4, .size = 4, .base_type = Uint32 },
        });
    fit.message(4);
    fit.byte(4); // An activity
    fit.u16_le(Garmin);
    fit.u16_le(4315);
    fit.u32_le(Start);
    add_session(fit, 60, 10);
    Loaded t { fit.file(), LoadingDepth::Shallow };
    CHECK(t.loaded);
    CHECK_EQ(t.activity.product.value_or(0), 4315u);
}

void test_no_sensors_on_a_shallow_load()
{
    Loaded t { edge_ride(), LoadingDepth::Shallow };
    CHECK(t.loaded);
    CHECK(t.activity.sensors.empty());
}

void test_no_sensors_without_device_info()
{
    FITBuilder fit;
    add_file_id(fit, 260); // Zwift
    add_session(fit, 60, 10);
    Loaded t { fit.file(), LoadingDepth::Deep };
    CHECK(t.loaded);
    CHECK(t.activity.sensors.empty());
}

// Older files don't say how a device is connected: the device's own parts
// have its serial number.
void test_without_source_type_the_creators_serial_is_a_part()
{
    SensorList list;
    auto const listed = [](u8 index, std::optional<u32> serial) {
        return FIT::device_info { .device_index = index, .manufacturer = FIT::manufacturer::garmin, .serial_number = serial };
    };
    list.add(listed(0, EdgeSerial));
    list.add(listed(1, EdgeSerial));
    list.add(listed(2, 42));
    list.add(listed(3, std::nullopt));
    auto const sensors = list.sensors();
    CHECK_EQ(sensors.size(), 2uz);
    if (sensors.size() == 2) {
        CHECK_EQ(sensors[0].serial_number.value_or(0), 42u);
        CHECK(!sensors[1].serial_number);
    }
}

void test_a_product_name_is_kept()
{
    SensorList list;
    list.add(FIT::device_info { .device_index = 1, .source_type = FIT::source_type::antplus, .product_name = "HRM-Pro" });
    list.add(FIT::device_info { .device_index = 1, .source_type = FIT::source_type::antplus, .product_name = "" });
    auto const sensors = list.sensors();
    CHECK_EQ(sensors.size(), 1uz);
    if (!sensors.empty()) {
        CHECK_EQ(sensors[0].product_name, "HRM-Pro");
    }
}

}

int main()
{
    test_sensors_are_read_on_a_deep_load();
    test_the_last_listing_wins();
    test_the_recording_devices_product_is_kept();
    test_no_sensors_on_a_shallow_load();
    test_no_sensors_without_device_info();
    test_without_source_type_the_creators_serial_is_a_part();
    test_a_product_name_is_kept();
    return test_result();
}
