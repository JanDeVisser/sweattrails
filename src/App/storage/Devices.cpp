/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <storage/Devices.h>

namespace ST {

namespace {

constexpr FIT::device_index Creator = 0;

template<typename T>
void update(std::optional<T> &value, std::optional<T> const &listed)
{
    if (listed) {
        value = listed;
    }
}

}

void SensorList::add(FIT::device_info const &info)
{
    auto &device = devices[info.device_index];
    update(device.source, info.source_type);
    update(device.device_type, info.device_type);
    update(device.manufacturer, info.manufacturer);
    update(device.product, info.product);
    if (info.product_name && !info.product_name->empty()) {
        device.product_name = *info.product_name;
    }
    update(device.serial_number, info.serial_number);
    update(device.software_version, info.software_version);
    update(device.battery, info.battery_status);
}

std::vector<Sensor> SensorList::sensors() const
{
    std::optional<u32> creator_serial { };
    if (auto const creator = devices.find(Creator); creator != devices.end()) {
        creator_serial = creator->second.serial_number;
    }
    auto const is_sensor = [&creator_serial](FIT::device_index index, Sensor const &device) {
        if (index == Creator) {
            return false;
        }
        if (device.source) {
            return *device.source != FIT::source_type::local;
        }
        return !creator_serial || device.serial_number != creator_serial;
    };
    std::vector<Sensor> ret;
    for (auto const &[index, device] : devices) {
        if (is_sensor(index, device)) {
            ret.push_back(device);
        }
    }
    return ret;
}

}
