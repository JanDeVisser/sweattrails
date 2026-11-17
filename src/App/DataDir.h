/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <filesystem>

namespace ST {

// The app's share/ directory, with its fonts, icons and overlays: inside the
// installed app, or the build directory's during development.
std::filesystem::path const &data_dir();

}
