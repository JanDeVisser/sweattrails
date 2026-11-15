/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Activities that are only a summary: a FIT file without records, made by
 * a program from the totals, not by the device. The store replaces them
 * with their real recordings when those come in. See
 * specs/50-stub-activities.md. Also replaces a rewrite that kept the
 * records but lost the track: see specs/55-tracks-from-strava.md.
 */

#pragma once

#include <expected>
#include <optional>
#include <string_view>

#include <JSON.h>

#include <fit/FIT.h>
#include <storage/Recordings.h>
#include <storage/Storage.h>

namespace ST {

// Next to the original that replaced it: `<start>.fit.stub`.
constexpr std::string_view StubSuffix = ".stub";

// Whether a FIT file has no record messages. Reads until the first one,
// so a real recording is quick.
std::expected<bool, FITError> is_stub(std::string_view fit);
StorageResult<bool>           is_stub_file(fs::path const &fit);

// What a FIT file has, as far as replacing it goes.
struct Recorded {
    bool records { false };
    bool positions { false }; // A record with a position
    bool serial { false };    // file_id's or a device_info's serial number

    // A program's rewrite that kept the records but lost the track: a
    // device's own recording has a serial number.
    [[nodiscard]] bool lost_track() const { return records && !positions && !serial; }
};

// What `fit` has. Reads until that's known, so most files stop at their
// first record with a position.
std::expected<Recorded, FITError> what_is_recorded(std::string_view fit);

// Whether `original` is the recording the rewrite `rewrite` lost the track
// of: the same start second, and distances within SameRecordingDistance.
[[nodiscard]] bool lost_track_of(RecordingInfo const &rewrite, RecordingInfo const &original);

// What the store has where an activity starting at `start`, a Unix time,
// would go.
struct Slot {
    enum class Kind {
        Free,  // Nothing
        Stub,  // A stub within StravaMatchSeconds, and no real activity at `start`
        Taken, // A real activity at `start`, also one in a .recordings folder
    };
    Kind        kind { Kind::Free };
    StoredStart stub { }; // For Stub: the nearest one
};

// Looks in the month of `start` and the months around it, as
// has_activity() does, not in the whole store: the downloads ask for each
// workout they list. A FIT file that can't be read counts as real: what
// can't be looked at is never replaced.
[[nodiscard]] Slot slot_for(Storage const &storage, i64 start);

// Whether a download should fetch the activity starting at `start`: there's
// nothing there, or only a stub.
[[nodiscard]] bool wants_activity(Storage const &storage, i64 start);

// Replaces the stub with `fit`, its original, which starts at `start`. The
// original is filed as `<start>.fit`, the stub kept next to it as
// `<start>.fit.stub`, and the stub's sidecar moves along, with its
// `"start_time"` updated, if the start is another. `incoming`, the
// original's own sidecar from a download, adds what the stub's sidecar
// doesn't have. Nothing is overwritten: if the stub's file can't be kept,
// or the original can't be written, the store is as it was.
Error<StorageError> replace_stub(Storage const &storage, StoredStart const &stub, std::string_view fit, i64 start, std::optional<JSONValue> const &incoming = { });

// Replaces the FIT file `stored`, a listed activity's or a recording's in
// a folder, with `fit`, which starts in the same second: `stored` is kept
// next to it as `<stored>.stub`, and the sidecar stays as it is. Nothing
// is overwritten: if `stored` can't be kept, or `fit` can't be written,
// the store is as it was.
Error<StorageError> replace_in_place(fs::path const &stored, std::string_view fit);

}
