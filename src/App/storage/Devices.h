/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The sensors connected to the device that recorded an activity, from the
 * FIT file's device_info messages. See specs/48-device-info.md.
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fit/FIT.h>
#include <fit/Profile.h>

namespace ST {

// A device's product name as the FIT file has it, trimmed, or "" for one
// that isn't text: Zwift fills its product name with leftover bytes,
// which drew as "?>?".
[[nodiscard]] std::string product_text(std::string_view name);

// A device's product number, but for 0, which Zwift writes and which no
// maker uses for a real model.
[[nodiscard]] std::optional<u16> product_number(std::optional<u16> product);

// A sensor as the FIT file lists it. Anything the file leaves out is
// empty.
struct Sensor {
    std::optional<FIT::source_type>    source { };
    std::optional<u8>                  device_type { }; // ANT+'s or BLE's, by `source`
    std::optional<FIT::manufacturer>   manufacturer { };
    std::optional<u16>                 product { };
    std::string                        product_name { };
    std::optional<u32>                 serial_number { };
    std::optional<float32>             software_version { };
    std::optional<FIT::battery_status> battery { };

    bool operator==(Sensor const &) const = default;
};

// The devices of a FIT file's device_info messages, by device index. A
// file lists each several times: a later listing's values replace an
// earlier one's, and a value it leaves out stays as it was. So does a
// known battery status, under a later "unknown".
struct SensorList {
    std::map<FIT::device_index, Sensor> devices { };

    void add(FIT::device_info const &info);

    // The sensors connected to the recording device, by index: not the
    // device itself (index 0), nor its own parts, such as its GPS, which
    // are local. A listing that doesn't say how it's connected, as in
    // older files and a Wahoo's, is a sensor if it has a serial number of
    // its own: the device's parts have its serial number, or none.
    [[nodiscard]] std::vector<Sensor> sensors() const;
};

}
