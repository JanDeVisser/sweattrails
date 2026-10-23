/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Reads entries from a ZIP archive held in memory, such as an activity
 * downloaded from Garmin Connect. Only what such archives use: entries that
 * are stored or deflated. No ZIP64, encryption or archives split over
 * several files.
 */

#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <Error.h>

namespace ST {

// The names of the entries in `zip`, in the order of its directory.
std::expected<std::vector<std::string>, LibCError> zip_entry_names(std::string_view zip);

// The contents of the entry called `name`.
std::expected<std::string, LibCError> unzip(std::string_view zip, std::string_view name);

}
