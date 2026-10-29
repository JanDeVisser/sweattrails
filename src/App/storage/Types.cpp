/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/storage/types.zig.
 */

#include <algorithm>
#include <format>
#include <system_error>

#include <Logging.h>
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

Month Month::plus(int months) const
{
    auto const index = static_cast<int>(year) * 12 + static_cast<int>(month) + months;
    return Month { static_cast<size_t>(index / 12), static_cast<DateTime::Month>(index % 12) };
}

std::optional<Month> month_after(std::span<Month const> months, Month const &from)
{
    auto const it = std::ranges::upper_bound(months, from);
    return (it != months.end()) ? std::optional { *it } : std::nullopt;
}

std::optional<Month> month_before(std::span<Month const> months, Month const &from)
{
    auto const it = std::ranges::lower_bound(months, from);
    return (it != months.begin()) ? std::optional { *std::prev(it) } : std::nullopt;
}

std::optional<fs::path> Month::dir(Storage const &storage) const
{
    auto            ret = storage.activity_dir / dir_name();
    std::error_code ec { };
    if (!fs::is_directory(ret, ec)) {
        // error.FileNotFound => null. Other errors, e.g. a year directory
        // that can't be searched, also mean there's nothing to list, but are
        // worth a warning.
        if (ec && ec != std::errc::no_such_file_or_directory) {
            warning(Storage, "Skipping `{}`: {}", ret.string(), LibCError { ec }.to_string());
        }
        return { };
    }
    return ret;
}

StorageResult<bool> Month::has_activities(Storage const &storage) const
{
    if (auto d = dir(storage); d) {
        std::error_code ec { };
        for (fs::directory_iterator it { *d, ec }; !ec && it != fs::directory_iterator { }; it.increment(ec)) {
            auto const     &entry = *it;
            std::error_code entry_ec { };
            if (!entry.is_regular_file(entry_ec)) {
                if (entry_ec) {
                    warning(Storage, "Skipping `{}`: {}", entry.path().string(), LibCError { entry_ec }.to_string());
                }
                continue;
            }
            auto const ext = entry.path().extension().string();
            // Like Activities::list(), only count `.fit` in lower case.
            if (ext == ".fit") {
                return true;
            } else if (eql_ignore_case(ext, ".strava")) {
                return true;
            } else if (eql_ignore_case(ext, ".sweattrails")) {
                return true;
            }
        }
        if (ec) {
            return std::unexpected(StorageError { LibCError { ec } });
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
