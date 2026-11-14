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

// Two files without both a serial number are one recording, rewritten,
// when their totals are this close: Garmin Connect's copy of Zwift's
// file starts some seconds off, but keeps its time and distance.
constexpr u32    SameRecordingSeconds = 5;
constexpr double SameRecordingDistance = 0.005;

struct RecordingInfo {
    fs::path                         fit { };
    i64                              start { 0 };   // A Unix time
    u32                              elapsed { 0 }; // s
    FIT::sport                       sport { FIT::sport::Generic };
    std::optional<FIT::sub_sport>    sub_sport { };
    std::optional<FIT::manufacturer> manufacturer { };
    u32                              serial { 0 };       // file_id's: 0 for none
    float32                          distance { 0 };     // m
    bool                             separate { false }; // Split by the user: never grouped again
    bool                             chosen { false };   // Made the main one by the user, with Ctrl+J
};

// What the session of a FIT file in the store says, and its sidecar. None
// if it can't be read.
[[nodiscard]] std::optional<RecordingInfo> recording_info(Storage &storage, fs::path const &fit);

// The same of a FIT file in memory, which has no path: `fit` stays empty.
[[nodiscard]] std::optional<RecordingInfo> recording_info(Storage &storage, std::string_view fit);

// Whether two recordings are of one activity: the same sport, as the
// totals count it, overlapping for most of the shorter one's time.
[[nodiscard]] bool same_activity(RecordingInfo const &a, RecordingInfo const &b);

// Whether two FIT files are one recording: the device's own file and a
// copy rewritten by Garmin Connect or SweatTrails, say. The same when
// they start in the same second, else by the serial number when both
// have one, else by the totals. Only file_id's serial
// counts: two head units paired with one power meter both list its
// serial in their device_info.
[[nodiscard]] bool same_recording(RecordingInfo const &a, RecordingInfo const &b);

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

// Ctrl+J on a recording in a main one's folder: makes it the main one,
// chosen by the user, with its main one and the folder's others as its
// recordings. An error for a FIT file that isn't in a folder.
StorageResult<Grouped> make_main(Storage &storage, fs::path const &recording_fit);

// Moves the main recording's others back to where the inbox would file
// them, as activities of their own, and marks them all as separate. How
// many it moved. An error if `main_fit` isn't a listed activity's.
StorageResult<size_t> split_recordings(Storage &storage, fs::path const &main_fit);

}
