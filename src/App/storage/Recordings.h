/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * One activity recorded on several devices: the recordings are kept
 * together, the others in a folder next to the main one, so that the
 * activity is listed and counted once. See specs/31-multiple-recordings.md.
 */

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <JSON.h>

#include <fit/FIT.h>
#include <fit/Profile.h>
#include <storage/Types.h>

namespace ST {

namespace fs = std::filesystem;

struct Storage;

// Recordings overlap for at least this much of the shorter one's time.
constexpr double SameActivityOverlap = 0.8;

struct RecordingInfo {
    fs::path                         fit { };
    i64                              start { 0 };   // A Unix time
    u32                              elapsed { 0 }; // s
    FIT::sport                       sport { FIT::sport::Generic };
    std::optional<FIT::sub_sport>    sub_sport { };
    std::optional<FIT::manufacturer> manufacturer { };
    bool                             separate { false }; // Split by the user: never grouped again
    bool                             chosen { false };   // Made the main one by the user, with Ctrl+J
};

// What the session of a FIT file in the store says, and its sidecar. None
// if it can't be read.
[[nodiscard]] std::optional<RecordingInfo> recording_info(Storage &storage, fs::path const &fit);

// Whether two recordings are of one activity: the same sport, as the
// totals count it, overlapping for most of the shorter one's time.
[[nodiscard]] bool same_activity(RecordingInfo const &a, RecordingInfo const &b);

// The FIT file of the main recording of the one at `fit`: its own for a
// listed activity, else the one whose folder it's in.
[[nodiscard]] fs::path main_of(fs::path const &fit);

// The IDs of the recordings of the activity `id` is a recording of, as
// Tab steps through them: the main one first, then the others by their
// start. Just the main one's for an activity of one recording.
[[nodiscard]] std::vector<ActivityID> recordings_in_order(Storage const &storage, ActivityID const &id);

// The main one of `recordings`: the one the user chose, else the one from
// the first of the preferred devices any is from, else, or among several
// from one, the longest.
[[nodiscard]] size_t main_recording(std::vector<RecordingInfo> const &recordings, std::vector<FIT::manufacturer> const &prefer);

// The settings' `"recordings": { "prefer": [...] }`, FIT's manufacturer
// names, or what's wrong with it. None without it.
[[nodiscard]] std::expected<std::vector<FIT::manufacturer>, std::string> decode_prefer(std::optional<JSONValue> const &recordings);

// The activity whose FIT file in the store is `fit`,
// `<year>/<month>/<start>.fit`. None for a path that isn't one.
[[nodiscard]] std::optional<ActivityID> activity_id_of(fs::path const &fit);

// What grouping did: the main recording, and the start times of the
// activities that were listed and now are recordings of it.
struct Grouped {
    fs::path         main { };
    std::vector<i64> hidden { };
};

// Groups the recordings of one activity: the main one listed, where the
// inbox would file it, and the others in its folder, with their sidecars
// and originals, and those of a former main one's folder. The user's own
// title goes to the main one, if it has none.
StorageResult<Grouped> group_recordings(Storage &storage, std::vector<RecordingInfo> const &recordings);

// Groups the activity starting at `start`, just filed, with the listed
// activities it's a recording of the same activity as, and their
// recordings. None if there are none.
StorageResult<std::optional<Grouped>> group_new(Storage &storage, i64 start);

// group_new() for each of the month's activities, for the once over the
// store. The groups it made.
StorageResult<std::vector<Grouped>> group_month(Storage &storage, Month const &month);

// For Ctrl+J: the recordings group_with_main() would group with the
// listed activity `main_fit`. The listed activities that are recordings
// of the same activity, also those split by the user, their recordings,
// and its own. An error if `main_fit` isn't a listed activity's.
StorageResult<std::vector<RecordingInfo>> recordings_to_group(Storage &storage, fs::path const &main_fit);

// Groups recordings_to_group() with `main_fit` as the main one, chosen by
// the user, so that it stays the main one. None of them is separate any
// more.
StorageResult<Grouped> group_with_main(Storage &storage, fs::path const &main_fit);

// Moves the main recording's others back to where the inbox would file
// them, as activities of their own, and marks them all as separate. How
// many it moved. An error if `main_fit` isn't a listed activity's.
StorageResult<size_t> split_recordings(Storage &storage, fs::path const &main_fit);

}
