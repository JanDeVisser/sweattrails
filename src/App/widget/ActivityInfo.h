/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * What the activity info popup says (specs/48-device-info.md): the FIT
 * file, the device that recorded it, and the sensors connected to that
 * device. Text only, so that tests can check it without fonts.
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <storage/Activity.h>
#include <storage/Devices.h>

namespace ST {

constexpr std::string_view NoSensors = "No sensors listed in this file";

// A line of the popup: `text` in the normal font, then `small` in the
// smaller one, either of which can be empty.
struct InfoLine {
    std::string text { };
    std::string small { };

    bool operator==(InfoLine const &) const = default;
};

struct SensorInfo {
    InfoLine    name { };    // "Garmin heart rate monitor (product 3865)"
    std::string details { }; // In the smaller font: "Serial 123 · Software 5.10 · Battery ok"

    bool operator==(SensorInfo const &) const = default;
};

struct ActivityInfo {
    std::string             file { };      // As shown: relative to the store
    std::filesystem::path   path { };      // The file itself, for Show in Finder; empty for none
    std::string             recording { }; // "Recording 2 of 2", for one of several
    InfoLine                device { };
    std::vector<SensorInfo> sensors { }; // None says NoSensors

    bool operator==(ActivityInfo const &) const = default;
};

// What a sensor is, from its ANT+ or BLE device type: "heart rate
// monitor", "power meter", ... or "sensor" for a type it doesn't know.
std::string sensor_type_name(Sensor const &sensor);

// The popup's text for `activity`, its file named from `store`, the
// store's folder.
ActivityInfo describe(Activity const &activity, std::filesystem::path const &store);

}
