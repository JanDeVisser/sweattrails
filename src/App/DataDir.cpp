/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <FileIO.h>

#include <DataDir.h>

namespace ST {

std::filesystem::path const &data_dir()
{
    static std::filesystem::path const dir = find_data_dir(executable_path(), SWEATTRAILS_DATADIR);
    return dir;
}

}
