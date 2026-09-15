/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace ST {

extern int                             parse_options(int argc, char const **argv);
extern std::optional<std::string_view> get_option(std::string_view option);
extern std::vector<std::string_view>   get_option_values(std::string_view option);
extern bool                            has_option(std::string_view opt);

}
