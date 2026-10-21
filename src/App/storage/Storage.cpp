/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/storage.zig.
 *
 * The zig code notifies the UI with typed states (`app.notify(.{ .OrganizingInbox
 * = name })`). Here process_inbox() takes a callback instead, so that storage
 * doesn't depend on the app.
 */

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdlib>
#include <format>
#include <string_view>
#include <system_error>

#include <IO.h>
#include <Logging.h>
#include <StringUtil.h>

#include <storage/Activity.h>
#include <storage/Storage.h>

namespace ST {

namespace {

bool eql_ignore_case(std::string_view const &lhs, std::string_view const &rhs)
{
    return to_lower(lhs) == to_lower(rhs);
}

// Whether `<dir>/<stem>.fit`, or its sidecar `<dir>/<stem>.json`, exists.
std::expected<bool, LibCError> stem_taken(fs::path const &dir, std::string_view const &stem)
{
    for (auto const *ext : { ".fit", ".json" }) {
        if (TRY_EVAL(path_exists(dir / std::format("{}{}", stem, ext)))) {
            return true;
        }
    }
    return false;
}

}

StorageResult<Storage> Storage::init(std::string_view const &app_name)
{
    Storage ret { };
    ret.data_dir = TRY_EVAL(ensure_directory(user_data_dir(app_name)));
    info(Storage, "Opened data_dir `{}`", ret.data_dir.string());
    ret.inbox_dir = TRY_EVAL(ensure_directory(ret.data_dir / "inbox"));
    ret.activity_dir = TRY_EVAL(ensure_directory(ret.data_dir / "activity"));
    ret.tiles_dir = TRY_EVAL(ensure_directory(ret.data_dir / "tiles"));
    return ret;
}

Error<StorageError> Storage::rescan()
{
    trace(Storage, "rescan()");
    years.clear();
    std::error_code ec { };
    for (fs::directory_iterator it { activity_dir, ec }; !ec && it != fs::directory_iterator { }; it.increment(ec)) {
        auto const     &entry = *it;
        std::error_code entry_ec { };
        if (!entry.is_directory(entry_ec)) {
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
        return std::unexpected(StorageError { LibCError { ec } });
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

StorageResult<u32> Storage::process_inbox(InboxProgress const &on_file)
{
    // Collect the file names first: organizing a file moves it out of the
    // inbox, and it's unspecified whether a directory_iterator sees that.
    //
    // TODO: If the webhook writes a sidecar after its FIT file, and this runs
    // in between, the FIT file is moved without it and the sidecar stays in
    // the inbox, which is never looked at for .json files.
    // This runs on the job thread, where an exception would end the app, so
    // use the std::filesystem overloads that don't throw.
    std::vector<std::string> file_names { };
    std::error_code          ec { };
    for (fs::directory_iterator it { inbox_dir, ec }; !ec && it != fs::directory_iterator { }; it.increment(ec)) {
        std::error_code entry_ec { };
        if (it->is_regular_file(entry_ec) && eql_ignore_case(it->path().extension().string(), ".fit")) {
            file_names.emplace_back(it->path().filename().string());
        }
        if (entry_ec) {
            warning(Storage, "Skipping inbox entry `{}`: {}", it->path().string(), LibCError { entry_ec }.to_string());
        }
    }
    if (ec) {
        return std::unexpected(StorageError { LibCError { ec } });
    }

    u32 processed { 0 };
    for (auto const &file_name : file_names) {
        if (on_file) {
            on_file(file_name);
        }
        auto res = organize_fit_file(file_name);
        if (!res) {
            // TODO: Files that can't be read, and Skipped ones that aren't
            // activities, stay in the inbox and are tried, and logged, again
            // on every start. Move them to e.g. inbox/rejected.
            warning(Storage, "Error organizing inbox file {}: {}", file_name, res.error().to_string());
            continue;
        }
        if (*res == InboxOutcome::Organized) {
            processed += 1;
        }
    }
    if (processed > 0) {
        TRY(rescan());
    }
    return processed;
}

StorageResult<Storage::InboxOutcome> Storage::organize_fit_file(std::string_view const &file_name)
{
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
    if (!TRY_EVAL(activity.load_from_dir(inbox_dir))) {
        return InboxOutcome::Skipped;
    }
    assert(activity.segment.start_time.timestamp > 0);
    auto const dest_dir_name = std::format(
        "{:04}/{:02}",
        static_cast<u16>(activity.segment.start_time.year),
        static_cast<u8>(activity.segment.start_time.month) + 1);
    auto const dest_dir = TRY_EVAL(ensure_directory(activity_dir / dest_dir_name));
    auto const dest_stem = std::format("{}", activity.segment.start_time.timestamp);
    // A sidecar without its FIT file counts as a duplicate too, so that
    // move_with_sidecar() finds both destinations free.
    if (TRY_EVAL(stem_taken(dest_dir, dest_stem))) {
        // renamePreserve: never clobber an existing file. Set the duplicate
        // aside so it isn't processed again on every start. The same file can
        // arrive more than once, so number the copies.
        auto const duplicates_dir = TRY_EVAL(ensure_directory(inbox_dir / "duplicates"));
        auto       duplicate_stem = stem;
        for (auto n = 1; TRY_EVAL(stem_taken(duplicates_dir, duplicate_stem)); ++n) {
            duplicate_stem = std::format("{}-{}", stem, n);
        }
        info(Storage, "File `{}` already exists at `{}`; moving it to `duplicates/{}.fit`", file_name, dest_dir_name, duplicate_stem);
        TRY(move_with_sidecar(stem, duplicates_dir, duplicate_stem));
        return InboxOutcome::Duplicate;
    }
    TRY(move_with_sidecar(stem, dest_dir, dest_stem));
    return InboxOutcome::Organized;
}

// Moves inbox file `<stem>.fit` to `<dest_dir>/<dest_stem>.fit`. The webhook
// that collects the FIT files writes a JSON sidecar next to each one, which
// goes along with its FIT file. Existing files are never overwritten: both
// destinations are checked before anything is moved.
Error<StorageError> Storage::move_with_sidecar(std::string_view const &stem, fs::path const &dest_dir, std::string_view const &dest_stem) const
{
    // TODO: Checking and then renaming isn't atomic: a file created in
    // between (by the webhook, say) is overwritten. renamex_np(RENAME_EXCL)
    // on macOS and renameat2(RENAME_NOREPLACE) on Linux would make "never
    // overwrite" atomic.
    if (TRY_EVAL(stem_taken(dest_dir, dest_stem))) {
        return std::unexpected(StorageError { LibCError { "`{}` already exists in `{}`", dest_stem, dest_dir.string() } });
    }
    std::error_code ec { };
    fs::rename(inbox_dir / std::format("{}.fit", stem), dest_dir / std::format("{}.fit", dest_stem), ec);
    if (ec) {
        return std::unexpected(StorageError { LibCError { ec } });
    }

    // The FIT file has moved, so the activity is stored whatever happens to
    // the sidecar. Failing to move it is only worth a warning.
    auto const sidecar = inbox_dir / std::format("{}.json", stem);
    fs::rename(sidecar, dest_dir / std::format("{}.json", dest_stem), ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        warning(Storage, "Could not move sidecar `{}` to `{}`: {}", sidecar.string(), dest_dir.string(), LibCError { ec }.to_string());
    }
    return { };
}

}
