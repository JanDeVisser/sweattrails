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
#include <vector>

#include <fit/FIT.h>
#include <fit/Profile.h>

namespace ST {

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
    // older files, is one of its parts if it has the device's serial
    // number.
    [[nodiscard]] std::vector<Sensor> sensors() const;
};

}
