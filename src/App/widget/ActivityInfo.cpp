/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>
#include <optional>

#include <StringUtil.h>

#include <widget/ActivityInfo.h>
#include <widget/DataFields.h>

namespace ST {

namespace {

std::optional<std::string_view> antplus_type_name(u8 type)
{
    using enum FIT::antplus_device_type;
    auto const known = FIT::antplus_device_type_of(type);
    if (!known) {
        return { };
    }
    switch (*known) {
    case heart_rate:
        return "heart rate monitor";
    case bike_power:
        return "power meter";
    case bike_speed_cadence:
        return "speed and cadence sensor";
    case bike_speed:
        return "speed sensor";
    case bike_cadence:
        return "cadence sensor";
    case stride_speed_distance:
        return "foot pod";
    case bike_radar:
        return "radar";
    case bike_light_main:
    case bike_light_shared:
        return "bike light";
    case fitness_equipment:
        return "trainer";
    case shifting:
        return "shifting";
    case muscle_oxygen:
        return "muscle oxygen sensor";
    case environment_sensor_legacy:
    case env_sensor:
        return "temperature sensor";
    case control:
    case control_hub:
        return "remote";
    case bike_aero:
        return "aero sensor";
    case weight_scale:
        return "scale";
    default:
        return { };
    }
}

std::optional<std::string_view> ble_type_name(u8 type)
{
    using enum FIT::ble_device_type;
    auto const known = FIT::ble_device_type_of(type);
    if (!known) {
        return { };
    }
    switch (*known) {
    case connected_gps:
        return "connected GPS";
    case heart_rate:
        return "heart rate monitor";
    case bike_power:
        return "power meter";
    case bike_speed_cadence:
        return "speed and cadence sensor";
    case bike_speed:
        return "speed sensor";
    case bike_cadence:
        return "cadence sensor";
    case footpod:
        return "foot pod";
    case bike_trainer:
        return "trainer";
    default:
        return { };
    }
}

// "Garmin HRM-Pro", or what there is of it.
std::string join_words(std::vector<std::string> const &words)
{
    std::string ret;
    for (auto const &word : words) {
        if (word.empty()) {
            continue;
        }
        if (!ret.empty()) {
            ret += ' ';
        }
        ret += word;
    }
    return ret;
}

// The product number, in the smaller font, when there's no name for it.
std::string product_number(std::string_view product_name, std::optional<u16> product)
{
    if (!product_name.empty() || !product) {
        return "";
    }
    return std::format("(product {})", *product);
}

std::string battery_name(FIT::battery_status battery)
{
    // FIT's names, but for "new_", whose underscore keeps it off C++'s new.
    if (battery == FIT::battery_status::new_) {
        return "new";
    }
    return FIT::tag(battery);
}

std::string sensor_details(Sensor const &sensor)
{
    std::vector<std::string> details;
    if (sensor.serial_number) {
        details.push_back(std::format("Serial {}", *sensor.serial_number));
    }
    if (sensor.software_version) {
        details.push_back(std::format("Software {:.2f}", *sensor.software_version));
    }
    // "Unknown" says no more than a battery left out.
    if (sensor.battery && *sensor.battery != FIT::battery_status::unknown) {
        details.push_back(std::format("Battery {}", battery_name(*sensor.battery)));
    }
    return join(details, " · ");
}

}

std::string sensor_type_name(Sensor const &sensor)
{
    if (!sensor.device_type) {
        return "sensor";
    }
    std::optional<std::string_view> name;
    if (sensor.source == FIT::source_type::bluetooth_low_energy || sensor.source == FIT::source_type::bluetooth) {
        // A Wahoo lists BLE sensors with ANT+'s types, such as 120 for a
        // heart rate strap, which isn't a BLE type.
        name = ble_type_name(*sensor.device_type);
        if (!FIT::ble_device_type_of(*sensor.device_type)) {
            name = antplus_type_name(*sensor.device_type);
        }
    } else if (sensor.source != FIT::source_type::wifi) {
        // ANT+, plain ANT, and older files, which don't say, use ANT+'s types.
        name = antplus_type_name(*sensor.device_type);
    }
    return std::string { name.value_or("sensor") };
}

ActivityInfo describe(Activity const &activity, std::filesystem::path const &store)
{
    ActivityInfo ret;
    ret.path = fit_path(activity);
    if (!ret.path.empty()) {
        auto const relative = store.empty() ? std::filesystem::path { } : ret.path.lexically_relative(store);
        // A file outside the store, which it shouldn't be, by its full path.
        auto const outside = relative.empty() || *relative.begin() == "..";
        ret.file = (outside ? ret.path : relative).string();
    }
    if (activity.recordings > 1) {
        ret.recording = std::format("Recording {} of {}", activity.recording_number, activity.recordings);
    }
    auto name = device_name(activity);
    ret.device = InfoLine {
        .text = name.empty() ? "Unknown device" : std::move(name),
        .small = product_number(activity.product_name, activity.product),
    };
    for (auto const &sensor : activity.sensors) {
        ret.sensors.push_back(SensorInfo {
            .name = InfoLine {
                .text = join_words({ maker_name(sensor.manufacturer), sensor.product_name, sensor_type_name(sensor) }),
                .small = product_number(sensor.product_name, sensor.product),
            },
            .details = sensor_details(sensor),
        });
    }
    return ret;
}

}
