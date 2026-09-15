/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/storage.zig.
 *
 * The zig code notifies the UI with typed states (`app.notify(.{ .OrganizingInbox
 * = name })`). There is no equivalent yet in the C++ app, so the translation
 * uses SweatTrails::set_message(); replace that with a real notification
 * mechanism when one exists.
 */

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdlib>
#include <format>
#include <string_view>
#include <system_error>

#include <Logging.h>
#include <StringUtil.h>
#include <SweatTrails.h>

#include <storage/Activity.h>
#include <storage/Storage.h>

namespace ST {

namespace {

StorageResult<fs::path> create_dir_path_open(fs::path const &parent, std::string_view const &sub_path)
{
    auto            ret = parent / sub_path;
    std::error_code ec { };
    if (!fs::is_directory(ret)) {
        fs::create_directories(ret, ec);
        if (ec) {
            return std::unexpected(StorageError { LibCError { ec.message() } });
        }
    }
    return ret;
}

bool eql_ignore_case(std::string_view const &lhs, std::string_view const &rhs)
{
    return to_lower(lhs) == to_lower(rhs);
}

}

StorageResult<Storage> Storage::init()
{
    char const *home_env = getenv("HOME");
    fs::path    home { (home_env != nullptr) ? home_env : "." };
    fs::path    data_dir_name {
#ifdef IS_APPLE
        home / "Library" / "Application Support" / "sweattrails"
#else
        home / ".local" / "share" / "sweattrails"
#endif
    };

    std::error_code ec { };
    if (!fs::is_directory(data_dir_name)) {
        fs::create_directories(data_dir_name, ec);
        if (ec) {
            return std::unexpected(StorageError { LibCError { ec.message() } });
        }
    }
    info(Storage, "Opened data_dir `{}`", data_dir_name.string());

    Storage ret { };
    ret.data_dir = data_dir_name;
    ret.inbox_dir = TRY_EVAL(create_dir_path_open(data_dir_name, "inbox"));
    ret.activity_dir = TRY_EVAL(create_dir_path_open(data_dir_name, "activity"));
    ret.tiles_dir = TRY_EVAL(create_dir_path_open(data_dir_name, "tiles"));
    return ret;
}

Error<StorageError> Storage::rescan()
{
    trace(Storage, "rescan()");
    years.clear();
    std::error_code ec { };
    for (auto const &entry : fs::directory_iterator { activity_dir, ec }) {
        if (!entry.is_directory()) {
            continue;
        }
        auto const name = entry.path().filename().string();
        if (name.length() != 4 || !name.starts_with("20")) {
            continue;
        }
        u16  year_num { 0 };
        auto res = std::from_chars(name.data(), name.data() + name.length(), year_num, 10);
        if (res.ec != std::errc { } || res.ptr != name.data() + name.length()) {
            continue;
        }
        Year year { .year = year_num, .months = { } };
        for (size_t month = 0; month < 12; ++month) {
            if (TRY_EVAL(Month(year_num, static_cast<DateTime::Month>(month)).has_activities(*this))) {
                if (!year.months) {
                    year.months = std::array<bool, 12> { };
                }
                (*year.months)[month] = true;
            } else if (year.months) {
                (*year.months)[month] = false;
            }
        }
        years.emplace_back(year);
    }
    if (ec) {
        return std::unexpected(StorageError { LibCError { ec.message() } });
    }
    std::stable_sort(years.begin(), years.end(), Year::less_than);
    return { };
}

std::optional<Year> Storage::find_year(size_t year) const
{
    for (auto const &y : years) {
        if (y.year == year) {
            return y;
        }
    }
    return { };
}

StorageResult<Years> Storage::get_years()
{
    TRY(rescan());
    return years;
}

StorageResult<std::optional<Activities>> Storage::find_current_folder()
{
    TRY(rescan());
    if (years.empty()) {
        return std::optional<Activities> { };
    }
    auto const &y = years.back();
    assert(y.months.has_value());
    for (size_t ix_m = 0; ix_m < 12; ++ix_m) {
        auto const month_ix = 12 - ix_m - 1;
        if ((*y.months)[month_ix]) {
            Month month { y.year, static_cast<DateTime::Month>(month_ix) };
            return TRY_EVAL(month.list(*this));
        }
    }
    return std::optional<Activities> { };
}

StorageResult<std::optional<Activity>> Storage::find_newest()
{
    TRY(rescan());
    if (years.empty()) {
        return std::optional<Activity> { };
    }
    auto const &y = years.back();
    assert(y.months.has_value());
    for (size_t ix_m = 0; ix_m < 12; ++ix_m) {
        auto const month_ix = 12 - ix_m - 1;
        if ((*y.months)[month_ix]) {
            Month       month { y.year, static_cast<DateTime::Month>(month_ix) };
            auto const &activities = TRY_EVAL(month.list(*this));
            if (activities.activities.empty()) {
                continue;
            }
            auto newest = activities.activities.back();
            return TRY_EVAL(newest.id.load(*this, LoadingDepth::Deep));
        }
    }
    return std::optional<Activity> { };
}

StorageResult<Activities> Storage::set_current_folder(Month const &month)
{
    TRY(rescan());
    return month.list(*this);
}

StorageResult<std::optional<Activity>> Storage::load_activity(ActivityID const &id)
{
    return id.load(*this, LoadingDepth::Deep);
}

Error<StorageError> Storage::process_inbox(SweatTrails &app)
{
    app.set_message("Organizing inbox");
    u32             processed { 0 };
    std::error_code ec { };
    for (auto const &entry : fs::directory_iterator { inbox_dir, ec }) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!eql_ignore_case(entry.path().extension().string(), ".fit")) {
            continue;
        }
        auto const file_name = entry.path().filename().string();
        if (auto res = organize_fit_file(app, file_name); !res) {
            log_error("Error organizing inbox file {}: {}", file_name, res.error().to_string());
            continue;
        }
        processed += 1;
    }
    if (ec) {
        return std::unexpected(StorageError { LibCError { ec.message() } });
    }
    if (processed > 1) {
        TRY(rescan());
    }
    app.set_message(std::format("Organized {} inbox files", processed));
    return { };
}

Error<StorageError> Storage::organize_fit_file(SweatTrails &app, std::string_view const &file_name)
{
    app.set_message(std::format("Organizing {}", file_name));
    auto const stem = fs::path { file_name }.stem().string();
    Activity   activity {
        *this,
        ActivityID {
            Month { 2020, DateTime::Month::January }, // dummy
            stem,
            FileTypes::one(ActivityFile::Fit),
        },
    };

    activity.depth = LoadingDepth::Shallow;
    if (TRY_EVAL(activity.load_from_dir(inbox_dir))) {
        assert(activity.segment.start_time.timestamp > 0);
        auto const dest_dir_name = std::format(
            "{:04}/{:02}",
            static_cast<u16>(activity.segment.start_time.year),
            static_cast<u8>(activity.segment.start_time.month) + 1);
        auto const dest_dir = TRY_EVAL(create_dir_path_open(activity_dir, dest_dir_name));
        auto const dest = dest_dir / std::format("{}.fit", activity.segment.start_time.timestamp);
        if (fs::exists(dest)) {
            // renamePreserve: never clobber an existing file
            info(Storage, "File `{}` already exists at `{}`", file_name, dest_dir_name);
            return { };
        }
        std::error_code ec { };
        fs::rename(inbox_dir / file_name, dest, ec);
        if (ec) {
            return std::unexpected(StorageError { LibCError { ec.message() } });
        }
    }
    return { };
}

}
