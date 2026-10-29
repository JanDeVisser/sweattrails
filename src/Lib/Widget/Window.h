/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>

#include <raylib.h>

namespace ST {

// The monitor to start on: the one saved, if it's still connected, else
// the main one, 0.
int start_monitor(int saved, int monitor_count);

// The window before it's maximized, and after it's un-maximized: two
// thirds of the monitor's width and height, centred on it, in whole
// screen coordinates. Nothing if that leaves no window, as for a monitor
// raylib gives a size of 0 for, when it can't find its video mode.
std::optional<Rectangle> restored_window(Vector2 monitor_position, Vector2 monitor_size);

}
