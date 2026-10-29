/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>

#include <Window.h>

namespace ST {

int start_monitor(int saved, int monitor_count)
{
    return (saved >= 0 && saved < monitor_count) ? saved : 0;
}

Rectangle restored_window(Vector2 monitor_position, Vector2 monitor_size)
{
    auto const width = std::floor(monitor_size.x * 2 / 3);
    auto const height = std::floor(monitor_size.y * 2 / 3);
    return Rectangle {
        .x = monitor_position.x + std::floor((monitor_size.x - width) / 2),
        .y = monitor_position.y + std::floor((monitor_size.y - height) / 2),
        .width = width,
        .height = height,
    };
}

}
