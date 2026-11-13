/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include <StringUtil.h>
#include <Utf8.h>

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

std::string product_text(std::string_view name)
{
    auto const trimmed = strip(name);
    if (trimmed.empty()) {
        return "";
    }
    auto const runes = to_wstring(trimmed);
    auto const is_control = [](wchar_t ch) { return ch < 0x20 || (ch >= 0x7F && ch < 0xA0); };
    if (!runes || std::ranges::any_of(*runes, is_control)) {
        return "";
    }
    return std::string { trimmed };
}

std::optional<u16> product_number(std::optional<u16> product)
{
    if (product == 0) {
        return { };
    }
    return product;
}

void SensorList::add(FIT::device_info const &info)
{
    auto &device = devices[info.device_index];
    update(device.source, info.source_type);
    update(device.device_type, info.device_type);
    update(device.manufacturer, info.manufacturer);
    update(device.product, product_number(info.product));
    if (auto name = product_text(info.product_name.value_or("")); !name.empty()) {
        device.product_name = std::move(name);
    }
    update(device.serial_number, info.serial_number);
    update(device.software_version, info.software_version);
    // A sensor that has dropped out can say "unknown" at the end, which
    // would hide the "low" that explains why it dropped out.
    if (info.battery_status != FIT::battery_status::unknown || !device.battery) {
        update(device.battery, info.battery_status);
    }
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
        return device.serial_number && device.serial_number != creator_serial;
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
