/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Reads entries from a ZIP archive held in memory, such as an activity
 * downloaded from Garmin Connect. Only what such archives use: entries that
 * are stored or deflated. No ZIP64, encryption or archives split over
 * several files.
 *
 * Also reads gzip files, such as the `.fit.gz` files of Strava's export.
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

// The contents of a gzip file. One that has more than one member, as
// concatenated gzip files do, gives the first.
std::expected<std::string, LibCError> gunzip(std::string_view gz);

}
