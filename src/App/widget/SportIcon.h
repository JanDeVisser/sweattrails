/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string_view>

#include <fit/Profile.h>

namespace ST {

// The file name, in <datadir>/icons, of the 96 pixel icon for a sport. The
// icons are from Icons8 (https://icons8.com).
std::string_view sport_icon(FIT::sport the_sport, std::optional<FIT::sub_sport> the_sub_sport);

}
