/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/storage/types.zig.
 */

#include <format>
#include <system_error>

#include <StringUtil.h>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <storage/Types.h>

namespace ST {

namespace {

bool eql_ignore_case(std::string_view const &lhs, std::string_view const &rhs)
{
    return to_lower(lhs) == to_lower(rhs);
}

}

std::string Month::dir_name() const
{
    return std::format("{:04}/{:02}", year, static_cast<u8>(month) + 1);
}

std::optional<fs::path> Month::dir(Storage const &storage) const
{
    auto            ret = storage.activity_dir / dir_name();
    std::error_code ec { };
    if (!fs::is_directory(ret, ec)) {
        // error.FileNotFound => null
        return { };
    }
    return ret;
}

StorageResult<bool> Month::has_activities(Storage const &storage) const
{
    if (auto d = dir(storage); d) {
        std::error_code ec { };
        for (auto const &entry : fs::directory_iterator { *d, ec }) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto const ext = entry.path().extension().string();
            if (eql_ignore_case(ext, ".fit")) {
                return true;
            } else if (eql_ignore_case(ext, ".strava")) {
                return true;
            } else if (eql_ignore_case(ext, ".sweattrails")) {
                return true;
            }
        }
        if (ec) {
            return std::unexpected(StorageError { LibCError { ec.message() } });
        }
    }
    return false;
}

StorageResult<Activities> Month::list(Storage &storage) const
{
    Activities ret { storage, *this };
    TRY(ret.list());
    return ret;
}

StorageResult<std::optional<Activity>> ActivityID::load(Storage &storage, LoadingDepth depth) const
{
    Activity ret { storage, *this };
    if (TRY_EVAL(ret.load(depth))) {
        return ret;
    }
    return std::optional<Activity> { };
}

}
