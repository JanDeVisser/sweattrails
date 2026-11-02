/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <charconv>
#include <format>

#include <IO.h>
#include <JSON.h>
#include <Logging.h>

#include <storage/Activity.h>
#include <storage/Recordings.h>
#include <storage/Storage.h>
#include <storage/Volume.h>

namespace ST {

namespace {

constexpr std::string_view RecordingsKey = "recordings";
constexpr std::string_view Separate = "separate";
constexpr std::string_view TitleKey = "title";
constexpr std::string_view FolderSuffix = ".recordings";

fs::path sidecar_of(fs::path const &fit)
{
    return fs::path { fit }.replace_extension(".json");
}

std::optional<JSONValue> read_sidecar(fs::path const &fit)
{
    std::error_code ec { };
    if (!fs::exists(sidecar_of(fit), ec)) {
        return { };
    }
    auto const contents = read_file_by_name(sidecar_of(fit).string());
    if (!contents) {
        return { };
    }
    auto json = JSONValue::deserialize(*contents);
    if (!json || !json->is_object()) {
        return { };
    }
    return *json;
}

bool in_folder(fs::path const &fit)
{
    return fit.parent_path().filename().string().ends_with(FolderSuffix);
}

// Where the inbox files the activity starting at `start`.
fs::path filed_path(Storage const &storage, i64 start)
{
    auto const date = DateTime::from_timestamp(static_cast<i32>(start));
    return storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1) / std::format("{}.fit", start);
}

// Moves a recording's files, `<stem>.fit` and what goes with it, into
// `dir`, keeping their names. Its FIT file's new path.
StorageResult<fs::path> move_recording(fs::path const &fit, fs::path const &dir)
{
    std::error_code ec { };
    fs::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(StorageError { LibCError { ec } });
    }
    auto const stem = fit.stem().string();
    auto const dest = dir / fit.filename();
    if (fs::exists(dest, ec)) {
        return std::unexpected(StorageError { LibCError { "`{}` already exists", dest.string() } });
    }
    // A .strava file is the old app's name and description.
    for (auto const *ext : { ".fit", ".json", ".fit.original", ".strava" }) {
        auto const from = fit.parent_path() / std::format("{}{}", stem, ext);
        if (!fs::exists(from, ec)) {
            continue;
        }
        fs::rename(from, dir / from.filename(), ec);
        if (ec) {
            return std::unexpected(StorageError { LibCError { ec } });
        }
    }
    return dest;
}

std::expected<void, LibCError> mark_separate(fs::path const &fit)
{
    return update_sidecar(sidecar_of(fit), [](JSONValue &json) { json.set(RecordingsKey, JSONValue { std::string { Separate } }); });
}

}

std::optional<RecordingInfo> recording_info(Storage &storage, fs::path const &fit)
{
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    activity.depth = LoadingDepth::Shallow;
    auto const loaded = activity.load_from_file(fit);
    if (!loaded || !*loaded) {
        return { };
    }
    RecordingInfo ret {
        .fit = fit,
        .start = activity.segment.start_time.timestamp,
        .elapsed = activity.segment.elapsed.elapsed,
        .sport = activity.sport,
        .sub_sport = activity.sub_sport,
        .manufacturer = activity.manufacturer,
    };
    if (auto const sidecar = read_sidecar(fit); sidecar) {
        ret.separate = sidecar->try_get<std::string>(RecordingsKey).value_or("") == Separate;
    }
    return ret;
}

fs::path recordings_dir(fs::path const &main_fit)
{
    return main_fit.parent_path() / std::format("{}{}", main_fit.stem().string(), FolderSuffix);
}

std::vector<fs::path> recordings_of(fs::path const &main_fit)
{
    std::vector<fs::path> ret;
    std::error_code       ec { };
    for (fs::directory_iterator it { recordings_dir(main_fit), ec }; !ec && it != fs::directory_iterator { }; it.increment(ec)) {
        if (it->path().extension() == ".fit") {
            ret.push_back(it->path());
        }
    }
    std::ranges::sort(ret);
    return ret;
}

bool same_activity(RecordingInfo const &a, RecordingInfo const &b)
{
    auto const a_kind = volume_sport(a.sport, a.sub_sport);
    auto const b_kind = volume_sport(b.sport, b.sub_sport);
    if (a_kind || b_kind ? a_kind != b_kind : a.sport != b.sport) {
        return false;
    }
    auto const from = std::max(a.start, b.start);
    auto const to = std::min(a.start + a.elapsed, b.start + b.elapsed);
    auto const shorter = std::min(a.elapsed, b.elapsed);
    return shorter > 0 && to > from && static_cast<double>(to - from) >= SameActivityOverlap * shorter;
}

size_t main_recording(std::vector<RecordingInfo> const &recordings, std::vector<FIT::manufacturer> const &prefer)
{
    if (recordings.empty()) {
        return 0;
    }
    std::vector<size_t> candidates;
    for (auto const device : prefer) {
        for (auto ix = 0uz; ix < recordings.size(); ++ix) {
            if (recordings[ix].manufacturer == device) {
                candidates.push_back(ix);
            }
        }
        if (!candidates.empty()) {
            break;
        }
    }
    if (candidates.empty()) {
        for (auto ix = 0uz; ix < recordings.size(); ++ix) {
            candidates.push_back(ix);
        }
    }
    return *std::ranges::max_element(candidates, { }, [&recordings](size_t ix) { return recordings[ix].elapsed; });
}

std::expected<std::vector<FIT::manufacturer>, std::string> decode_prefer(std::optional<JSONValue> const &recordings)
{
    if (!recordings || recordings->is_null()) {
        return std::vector<FIT::manufacturer> { };
    }
    if (!recordings->is_object()) {
        return std::unexpected(std::string { "`recordings` isn't an object" });
    }
    if (!recordings->has("prefer")) {
        return std::vector<FIT::manufacturer> { };
    }
    auto const names = recordings->try_get_array<std::string>("prefer");
    if (!names) {
        return std::unexpected(std::string { "`recordings.prefer` isn't a list of names" });
    }
    std::vector<FIT::manufacturer> ret;
    for (auto const &name : *names) {
        auto const m = FIT::manufacturer_by_name(name);
        if (!m) {
            return std::unexpected(std::format("`recordings.prefer` has \"{}\", which isn't a device maker FIT knows", name));
        }
        ret.push_back(*m);
    }
    return ret;
}

std::optional<ActivityID> activity_id_of(fs::path const &fit)
{
    auto const number = [](std::string const &s) -> std::optional<size_t> {
        size_t n { 0 };
        auto const [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
        if (ec != std::errc { } || end != s.data() + s.size()) {
            return { };
        }
        return n;
    };
    auto const month = number(fit.parent_path().filename().string());
    auto const year = number(fit.parent_path().parent_path().filename().string());
    if (fit.extension() != ".fit" || !month || !year || *month < 1 || *month > 12) {
        return { };
    }
    return ActivityID { Month { *year, static_cast<DateTime::Month>(*month - 1) }, fit.stem().string(), FileTypes::one(ActivityFile::Fit) };
}

StorageResult<Grouped> group_recordings(Storage &storage, std::vector<RecordingInfo> const &recordings)
{
    auto const main_ix = main_recording(recordings, storage.prefer);
    auto       main_fit = recordings[main_ix].fit;
    // The user's own name, from a listed one: the activity's, whichever
    // recording it was given on.
    std::optional<std::string> title;
    for (auto const &recording : recordings) {
        if (!in_folder(recording.fit)) {
            if (auto const sidecar = read_sidecar(recording.fit); sidecar && sidecar->has(TitleKey)) {
                title = sidecar->try_get<std::string>(TitleKey).value_or("");
                break;
            }
        }
    }
    if (in_folder(main_fit)) {
        auto const filed = filed_path(storage, recordings[main_ix].start);
        main_fit = TRY_EVAL(move_recording(main_fit, filed.parent_path()));
    }
    Grouped    ret { .main = main_fit };
    auto const folder = recordings_dir(main_fit);
    for (auto ix = 0uz; ix < recordings.size(); ++ix) {
        auto const     &fit = recordings[ix].fit;
        std::error_code gone { };
        // Moved already, with the folder of a former main one.
        if (ix == main_ix || fit.parent_path() == folder || !fs::exists(fit, gone)) {
            continue;
        }
        if (!in_folder(fit)) {
            ret.hidden.push_back(recordings[ix].start);
        }
        // A former main one's others join this one's.
        for (auto const &other : recordings_of(fit)) {
            if (other.filename() != main_fit.filename()) {
                TRY(move_recording(other, folder));
            }
        }
        std::error_code ec { };
        fs::remove(recordings_dir(fit), ec);
        TRY(move_recording(fit, folder));
    }
    if (title) {
        auto const named = update_sidecar(sidecar_of(main_fit), [&title](JSONValue &json) {
            if (!json.has(TitleKey)) {
                json.set(TitleKey, JSONValue { *title });
            }
        });
        if (!named) {
            warning(Storage, "Couldn't give `{}` its activity's name: {}", main_fit.string(), named.error().to_string());
        }
    }
    return ret;
}

namespace {

// group_new() for the activity whose FIT file is `fit`, wherever it's
// filed: the once over the store finds activities filed in another time
// zone's month too.
StorageResult<std::optional<Grouped>> group_listed(Storage &storage, fs::path const &fit)
{
    auto const filed = recording_info(storage, fit);
    if (!filed || filed->separate) {
        return std::optional<Grouped> { };
    }
    auto const                 start = filed->start;
    std::vector<RecordingInfo> group { *filed };
    constexpr i64              Day = 24 * 60 * 60;
    for (auto const &stored : TRY_EVAL(storage.activity_starts(start - Day, start + filed->elapsed))) {
        if (stored.start == start) {
            continue;
        }
        auto const fit = fs::path { stored.sidecar }.replace_extension(".fit");
        auto const other = recording_info(storage, fit);
        if (!other || other->separate || !same_activity(*filed, *other)) {
            continue;
        }
        group.push_back(*other);
        for (auto const &recording : recordings_of(fit)) {
            if (auto const info = recording_info(storage, recording); info) {
                group.push_back(*info);
            }
        }
    }
    if (group.size() == 1) {
        return std::optional<Grouped> { };
    }
    return std::optional { TRY_EVAL(group_recordings(storage, group)) };
}

}

StorageResult<std::optional<Grouped>> group_new(Storage &storage, i64 start)
{
    return group_listed(storage, filed_path(storage, start));
}

StorageResult<std::vector<Grouped>> group_month(Storage &storage, Month const &month)
{
    std::vector<Grouped> ret;
    for (auto const &stored : TRY_EVAL(storage.month_starts(month))) {
        // Grouped with one before it in this month: no longer listed.
        auto const      fit = fs::path { stored.sidecar }.replace_extension(".fit");
        std::error_code ec { };
        if (!fs::exists(fit, ec)) {
            continue;
        }
        if (auto grouped = TRY_EVAL(group_listed(storage, fit)); grouped) {
            ret.push_back(std::move(*grouped));
        }
    }
    return ret;
}

StorageResult<size_t> split_recordings(Storage &storage, fs::path const &main_fit)
{
    // The inbox may have made another recording the main one since the
    // user was asked.
    if (std::error_code ec { }; in_folder(main_fit) || !fs::exists(main_fit, ec)) {
        return std::unexpected(StorageError { LibCError { "`{}` isn't the main recording of an activity any more", main_fit.filename().string() } });
    }
    size_t ret { 0 };
    for (auto const &recording : recordings_of(main_fit)) {
        i64        start { 0 };
        auto const stem = recording.stem().string();
        std::from_chars(stem.data(), stem.data() + stem.size(), start);
        auto const moved = TRY_EVAL(move_recording(recording, filed_path(storage, start).parent_path()));
        if (auto const marked = mark_separate(moved); !marked) {
            return std::unexpected(StorageError { marked.error() });
        }
        ++ret;
    }
    std::error_code ec { };
    fs::remove(recordings_dir(main_fit), ec);
    if (ret > 0) {
        if (auto const marked = mark_separate(main_fit); !marked) {
            return std::unexpected(StorageError { marked.error() });
        }
    }
    return ret;
}

}
