/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <ranges>

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
constexpr std::string_view Chosen = "main";
constexpr std::string_view TitleKey = "title";
constexpr std::string_view FolderSuffix = RecordingsSuffix;

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
    // A .strava file is the old app's name and description; a .fit.stub
    // the summary the recording replaced (specs/50-stub-activities.md).
    for (auto const *ext : { ".fit", ".json", ".fit.original", ".fit.stub", ".strava" }) {
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

// Sets the sidecar's "recordings" to `value`, or removes it for none.
std::expected<void, LibCError> mark(fs::path const &fit, std::optional<std::string_view> value)
{
    return update_sidecar(sidecar_of(fit), [value](JSONValue &json) {
        if (value) {
            json.set(RecordingsKey, JSONValue { std::string { *value } });
        } else {
            json.erase(RecordingsKey);
        }
    });
}

RecordingInfo info_of(Activity const &activity)
{
    return RecordingInfo {
        .start = activity.segment.start_time.timestamp,
        .elapsed = activity.segment.elapsed.elapsed,
        .sport = activity.sport,
        .sub_sport = activity.sub_sport,
        .manufacturer = activity.manufacturer,
        .serial = activity.serial,
        .distance = activity.segment.distance,
    };
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
    auto ret = info_of(activity);
    ret.fit = fit;
    if (auto const sidecar = read_sidecar(fit); sidecar) {
        auto const marked = sidecar->try_get<std::string>(RecordingsKey).value_or("");
        ret.separate = marked == Separate;
        ret.chosen = marked == Chosen;
    }
    return ret;
}

std::optional<RecordingInfo> recording_info(Storage &storage, std::string_view fit)
{
    Activity activity { storage, ActivityID::dummy(ActivityFile::Fit) };
    activity.depth = LoadingDepth::Shallow;
    auto const loaded = activity.load_from_slice(fit);
    if (!loaded || !*loaded) {
        return { };
    }
    return info_of(activity);
}

bool same_recording(RecordingInfo const &a, RecordingInfo const &b)
{
    // A rewrite keeps the start, and may count the pauses differently.
    // Two devices don't start in the same second, and the inbox couldn't
    // file both anyway.
    if (a.start == b.start) {
        return true;
    }
    if (a.serial != 0 && b.serial != 0) {
        return a.serial == b.serial;
    }
    auto const elapsed = (a.elapsed > b.elapsed) ? a.elapsed - b.elapsed : b.elapsed - a.elapsed;
    auto const distance = std::abs(a.distance - b.distance);
    return elapsed <= SameRecordingSeconds && distance <= SameRecordingDistance * std::max(a.distance, b.distance);
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

fs::path main_of(fs::path const &fit)
{
    if (!in_folder(fit)) {
        return fit;
    }
    auto const folder = fit.parent_path().filename().string();
    return fit.parent_path().parent_path() / std::format("{}.fit", folder.substr(0, folder.size() - FolderSuffix.size()));
}

std::vector<ActivityID> recordings_in_order(Storage const &storage, ActivityID const &id)
{
    std::vector<ActivityID> ret { id.main() };
    auto const              dir = id.month.dir(storage);
    if (!dir) {
        return ret;
    }
    for (auto const &recording : recordings_of(*dir / std::format("{}.fit", id.name))) {
        auto other = id.main();
        other.recording = recording.stem().string();
        ret.push_back(std::move(other));
    }
    return ret;
}

size_t main_recording(std::vector<RecordingInfo> const &recordings, std::vector<FIT::manufacturer> const &prefer)
{
    if (recordings.empty()) {
        return 0;
    }
    if (auto const chosen = std::ranges::find_if(recordings, &RecordingInfo::chosen); chosen != recordings.end()) {
        return static_cast<size_t>(chosen - recordings.begin());
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

// The listed activities that are recordings of the same activity as
// `info`, with their recordings: anything starting from a day before it
// to its end can overlap it. Those split by the user only if
// `with_separate`.
StorageResult<std::vector<RecordingInfo>> same_activities(Storage &storage, RecordingInfo const &info, bool with_separate)
{
    std::vector<RecordingInfo> ret;
    constexpr i64              Day = 24 * 60 * 60;
    for (auto const &stored : TRY_EVAL(storage.activity_starts(info.start - Day, info.start + info.elapsed))) {
        if (stored.start == info.start) {
            continue;
        }
        auto const fit = fs::path { stored.sidecar }.replace_extension(".fit");
        auto const other = recording_info(storage, fit);
        if (!other || (other->separate && !with_separate) || !same_activity(info, *other)) {
            continue;
        }
        ret.push_back(*other);
        for (auto const &recording : recordings_of(fit)) {
            if (auto const recorded = recording_info(storage, recording); recorded) {
                ret.push_back(*recorded);
            }
        }
    }
    return ret;
}

// group_new() for the activity whose FIT file is `fit`, wherever it's
// filed: the once over the store finds activities filed in another time
// zone's month too.
StorageResult<std::optional<Grouped>> group_listed(Storage &storage, fs::path const &fit)
{
    auto const filed = recording_info(storage, fit);
    if (!filed || filed->separate) {
        return std::optional<Grouped> { };
    }
    std::vector<RecordingInfo> group { *filed };
    std::ranges::move(TRY_EVAL(same_activities(storage, *filed, false)), std::back_inserter(group));
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

StorageResult<std::vector<RecordingInfo>> recordings_to_group(Storage &storage, fs::path const &main_fit)
{
    auto const main = recording_info(storage, main_fit);
    if (in_folder(main_fit) || !main) {
        return std::unexpected(StorageError { LibCError { "`{}` isn't the main recording of an activity", main_fit.filename().string() } });
    }
    auto ret = TRY_EVAL(same_activities(storage, *main, true));
    for (auto const &recording : recordings_of(main_fit)) {
        if (auto const info = recording_info(storage, recording); info) {
            ret.push_back(*info);
        }
    }
    return ret;
}

StorageResult<Grouped> group_with_main(Storage &storage, fs::path const &main_fit)
{
    auto       others = TRY_EVAL(recordings_to_group(storage, main_fit));
    auto const main = recording_info(storage, main_fit);
    if (!main) {
        return std::unexpected(StorageError { LibCError { "`{}` can't be read", main_fit.filename().string() } });
    }
    if (others.empty()) {
        return Grouped { .main = main_fit };
    }
    // The main one first: if its sidecar can't be marked, the others keep
    // theirs, and nothing has changed. Before they move, as the sidecars
    // go along.
    if (auto const marked = mark(main_fit, Chosen); !marked) {
        return std::unexpected(StorageError { marked.error() });
    }
    for (auto &other : others) {
        if (other.separate || other.chosen) {
            if (auto const cleared = mark(other.fit, { }); !cleared) {
                return std::unexpected(StorageError { cleared.error() });
            }
        }
        other.separate = other.chosen = false;
    }
    std::vector<RecordingInfo> group { *main };
    group.front().separate = false;
    group.front().chosen = true;
    std::ranges::move(others, std::back_inserter(group));
    return group_recordings(storage, group);
}

StorageResult<Grouped> make_main(Storage &storage, fs::path const &recording_fit)
{
    auto const chosen = recording_info(storage, recording_fit);
    if (!in_folder(recording_fit) || !chosen) {
        return std::unexpected(StorageError { LibCError { "`{}` isn't another recording of an activity", recording_fit.filename().string() } });
    }
    auto const former = main_of(recording_fit);
    auto const former_info = recording_info(storage, former);
    if (!former_info) {
        return std::unexpected(StorageError { LibCError { "`{}` can't be read", former.filename().string() } });
    }
    // This one first, as group_with_main() does: if its sidecar can't be
    // marked, nothing has changed.
    if (auto const marked = mark(recording_fit, Chosen); !marked) {
        return std::unexpected(StorageError { marked.error() });
    }
    std::vector<RecordingInfo> group { *chosen, *former_info };
    group[0].chosen = true;
    for (auto const &other : recordings_of(former)) {
        if (other == recording_fit) {
            continue;
        }
        if (auto const info = recording_info(storage, other); info) {
            group.push_back(*info);
        }
    }
    for (auto &other : group | std::views::drop(1)) {
        if (other.separate || other.chosen) {
            if (auto const cleared = mark(other.fit, { }); !cleared) {
                return std::unexpected(StorageError { cleared.error() });
            }
        }
        other.separate = other.chosen = false;
    }
    return group_recordings(storage, group);
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
