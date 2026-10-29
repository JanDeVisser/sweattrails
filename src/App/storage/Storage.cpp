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

// The directory, under activity/, of an activity that starts at `start`.
std::string month_dir_name(DateTime const &start)
{
    return std::format("{:04}/{:02}", static_cast<u16>(start.year), static_cast<u8>(start.month) + 1);
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
    // Made at the start, so that it's there to be found.
    ret.overlays_dir = TRY_EVAL(ensure_directory(ret.data_dir / "overlays"));
    return ret;
}

StorageResult<std::vector<Month>> Storage::months_with_activities() const
{
    std::vector<Month> ret;
    std::error_code    ec { };
    for (fs::directory_iterator it { activity_dir, ec }; !ec && it != fs::directory_iterator { }; it.increment(ec)) {
        auto const     &entry = *it;
        std::error_code entry_ec { };
        if (!entry.is_directory(entry_ec)) {
            if (entry_ec) {
                warning(Storage, "Skipping `{}`: {}", entry.path().string(), LibCError { entry_ec }.to_string());
            }
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
        for (size_t month = 0; month < 12; ++month) {
            // A month directory that can't be read is skipped, rather than
            // failing the whole scan.
            Month const month_id { year_num, static_cast<DateTime::Month>(month) };
            auto const  has_activities = month_id.has_activities(*this);
            if (!has_activities) {
                warning(Storage, "Skipping `{}`: {}", month_id.dir_name(), has_activities.error().to_string());
            }
            if (has_activities.value_or(false)) {
                ret.push_back(month_id);
            }
        }
    }
    if (ec) {
        return std::unexpected(StorageError { LibCError { ec } });
    }
    std::ranges::sort(ret);
    return ret;
}

// find_newest() and find_current_folder() expect every year to have
// months: a year directory without any has nothing to show anyway.
Error<StorageError> Storage::rescan()
{
    trace(Storage, "rescan()");
    years.clear();
    for (auto const &month : TRY_EVAL(months_with_activities())) {
        if (years.empty() || years.back().year != month.year) {
            years.push_back(Year { .year = month.year, .months = std::array<bool, 12> { } });
        }
        (*years.back().months)[static_cast<size_t>(month.month)] = true;
    }
    return { };
}

// A month whose files all fail to load lists nothing, as Ctrl+M shows it,
// and is passed over.
StorageResult<std::optional<ActivityID>> Storage::activity_next_to(ActivityID const &id, int step)
{
    auto const  same_month = TRY_EVAL(id.month.list(*this));
    auto const &activities = same_month.activities;
    auto const  here = std::ranges::find_if(activities, [&id](Activity const &a) { return a.id.name == id.name; });
    if (here != activities.end()) {
        auto const ix = std::distance(activities.begin(), here) + step;
        if (ix >= 0 && ix < std::ssize(activities)) {
            return std::optional { activities[static_cast<size_t>(ix)].id };
        }
    }
    auto const months = TRY_EVAL(months_with_activities());
    auto       month = id.month;
    while (true) {
        auto const next = (step > 0) ? month_after(months, month) : month_before(months, month);
        if (!next) {
            return std::optional<ActivityID> { };
        }
        auto const listed = TRY_EVAL(next->list(*this));
        if (!listed.activities.empty()) {
            return std::optional { (step > 0) ? listed.activities.front().id : listed.activities.back().id };
        }
        month = *next;
    }
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

bool Storage::has_activity(i64 start_time) const
{
    auto const      start = DateTime::from_timestamp(static_cast<i32>(start_time));
    std::error_code ec { };
    return fs::exists(activity_dir / month_dir_name(start) / std::format("{}.fit", start_time), ec);
}

StorageResult<u32> Storage::process_inbox(InboxProgress const &on_file)
{
    // Collect the file names first: organizing a file moves it out of the
    // inbox, and it's unspecified whether a directory_iterator sees that.
    // This runs on the job thread, where an exception would end the app, so
    // use the std::filesystem overloads that don't throw.
    //
    // A sidecar has to be in the inbox before its FIT file: one that comes
    // after is left behind, as the inbox only looks at .fit files. The
    // Garmin Connect download writes the sidecar first, and runs as a job
    // too, so never at the same time as this.
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
    for (size_t ix = 0; ix < file_names.size(); ++ix) {
        auto const &file_name = file_names[ix];
        if (on_file) {
            on_file(file_name, ix, file_names.size());
        }
        // TODO: Files that can't be read, and Skipped ones that aren't
        // activities, stay in the inbox and are tried, and logged, again on
        // every start. Move them to e.g. inbox/rejected.
        auto res = organize_fit_file(file_name);
        if (!res) {
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
    // Load the file by its own name: process_inbox() also accepts e.g. `.FIT`.
    if (!TRY_EVAL(activity.load_from_file(inbox_dir / file_name))) {
        return InboxOutcome::Skipped;
    }
    assert(activity.segment.start_time.timestamp > 0);
    auto const dest_dir_name = month_dir_name(activity.segment.start_time);
    auto const dest_dir = TRY_EVAL(ensure_directory(activity_dir / dest_dir_name));
    auto const dest_stem = std::format("{}", activity.segment.start_time.timestamp);
    if (TRY_EVAL(path_exists(dest_dir / (dest_stem + ".fit")))) {
        // renamePreserve: never clobber an existing file. Set the duplicate
        // aside so it isn't processed again on every start. The same file can
        // arrive more than once, so number the copies.
        auto const duplicates_dir = TRY_EVAL(ensure_directory(inbox_dir / "duplicates"));
        auto       duplicate_stem = stem;
        for (auto n = 1; TRY_EVAL(stem_taken(duplicates_dir, duplicate_stem)); ++n) {
            duplicate_stem = std::format("{}-{}", stem, n);
        }
        info(Storage, "File `{}` already exists at `{}`; moving it to `duplicates/{}.fit`", file_name, dest_dir_name, duplicate_stem);
        TRY(move_with_sidecar(file_name, duplicates_dir, duplicate_stem));
        return InboxOutcome::Duplicate;
    }
    TRY(move_with_sidecar(file_name, dest_dir, dest_stem));
    return InboxOutcome::Organized;
}

// Moves inbox file `file_name` to `<dest_dir>/<dest_stem>.fit`. The Garmin
// Connect download writes a JSON sidecar, `<stem>.json`, next to each one,
// which goes along with its FIT file. Existing files are never
// overwritten: if the destination already has a sidecar, for example because
// its FIT file was deleted to import it again, that one is kept and the new
// one stays in the inbox.
Error<StorageError> Storage::move_with_sidecar(std::string_view const &file_name, fs::path const &dest_dir, std::string_view const &dest_stem) const
{
    auto const stem = fs::path { file_name }.stem().string();
    // TODO: Checking and then renaming isn't atomic: a file created at the
    // destination in between, by a second instance of the app say, is
    // overwritten. renamex_np(RENAME_EXCL) on macOS and
    // renameat2(RENAME_NOREPLACE) on Linux would make "never overwrite"
    // atomic.
    auto const dest = dest_dir / std::format("{}.fit", dest_stem);
    if (TRY_EVAL(path_exists(dest))) {
        return std::unexpected(StorageError { LibCError { "`{}` already exists", dest.string() } });
    }
    std::error_code ec { };
    fs::rename(inbox_dir / file_name, dest, ec);
    if (ec) {
        return std::unexpected(StorageError { LibCError { ec } });
    }

    // The FIT file has moved, so the activity is stored whatever happens to
    // the sidecar. Not moving it is only worth a warning.
    auto const sidecar = inbox_dir / std::format("{}.json", stem);
    auto const sidecar_dest = dest_dir / std::format("{}.json", dest_stem);
    if (auto const taken = path_exists(sidecar_dest); !taken || *taken) {
        if (path_exists(sidecar).value_or(true)) {
            warning(Storage, "Leaving sidecar `{}` in the inbox: {}", sidecar.string(),
                taken ? std::format("`{}` already exists", sidecar_dest.string())
                      : std::format("could not check `{}`: {}", sidecar_dest.string(), taken.error().to_string()));
        }
        return { };
    }
    fs::rename(sidecar, sidecar_dest, ec);
    if (ec == std::errc::no_such_file_or_directory) {
        // The download names sidecars `.json`. On a case-sensitive filesystem,
        // a `.JSON` put there by hand isn't found, so say so.
        if (auto const upper = inbox_dir / std::format("{}.JSON", stem); path_exists(upper).value_or(false)) {
            warning(Storage, "Leaving sidecar `{}` in the inbox: sidecars must be named `.json`", upper.string());
        }
    } else if (ec) {
        warning(Storage, "Could not move sidecar `{}` to `{}`: {}", sidecar.string(), dest_dir.string(), LibCError { ec }.to_string());
    }
    return { };
}

}
