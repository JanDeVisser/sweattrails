/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstdlib>
#include <system_error>

#include <FileIO.h>
#include <Logging.h>

#include <storage/Activity.h>
#include <storage/StravaTitles.h>
#include <storage/Stubs.h>

namespace ST {

namespace {

// The old app wrote the start time in some sidecars.
constexpr std::string_view StartTimeKey = "start_time";

fs::path fit_of(fs::path const &sidecar)
{
    return fs::path { sidecar }.replace_extension(".fit");
}

bool is_stub_or_unreadable(fs::path const &fit)
{
    auto const stub = is_stub_file(fit);
    if (!stub) {
        warning(Storage, "Can't tell whether `{}` is only a summary: {}", fit.string(), stub.error().to_string());
    }
    return stub.value_or(false);
}

// The activities filed within StravaMatchSeconds of `start`, nearest first.
std::vector<StoredStart> filed_near(Storage const &storage, i64 start)
{
    auto const               date = DateTime::from_timestamp(static_cast<i32>(start));
    Month const              month { static_cast<size_t>(date.year), date.month };
    std::vector<StoredStart> ret;
    for (auto const step : { -1, 0, 1 }) {
        auto const starts = storage.month_starts(month.plus(step));
        if (!starts) {
            warning(Storage, "Skipping `{}`: {}", month.plus(step).dir_name(), starts.error().to_string());
            continue;
        }
        for (auto const &stored : *starts) {
            if (std::abs(stored.start - start) <= StravaMatchSeconds) {
                ret.push_back(stored);
            }
        }
    }
    std::ranges::sort(ret, { }, [start](StoredStart const &stored) { return std::abs(stored.start - start); });
    return ret;
}

// Renames `old_fit` to `kept`, and writes `fit` as `fit_path`, which may
// be `old_fit`. Nothing is overwritten: if `kept` or `fit_path` is there,
// or `fit` can't be written, `old_fit` is put back.
Error<StorageError> swap_in(fs::path const &old_fit, fs::path const &kept, fs::path const &fit_path, std::string_view fit)
{
    if (TRY_EVAL(path_exists(kept))) {
        return std::unexpected(StorageError { LibCError { "`{}` already exists", kept.string() } });
    }
    std::error_code ec { };
    fs::rename(old_fit, kept, ec);
    if (ec) {
        return std::unexpected(StorageError { LibCError { ec } });
    }
    auto const restore = [&](LibCError const &error) -> Error<StorageError> {
        std::error_code restore_ec { };
        fs::rename(kept, old_fit, restore_ec);
        if (restore_ec) {
            warning(Storage, "Couldn't put `{}` back: {}", old_fit.string(), LibCError { restore_ec }.to_string());
        }
        return std::unexpected(StorageError { error });
    };
    if (auto const taken = path_exists(fit_path); !taken || *taken) {
        return restore(taken ? LibCError { "`{}` already exists", fit_path.string() } : taken.error());
    }
    if (auto const written = write_file_atomically(fit_path, fit); !written) {
        return restore(written.error());
    }
    return { };
}

bool has_value(FITDataRecord const &rec, u8 num)
{
    auto const field = rec.get_field(num);
    return field && field->value;
}

// A serial number of 0 is none, as RecordingInfo has it.
bool has_serial(FITDataRecord const &rec)
{
    auto const field = rec.get_field(3);
    return field && field->value
        && (field->type == FITBaseType::uint32 || field->type == FITBaseType::uint32z)
        && field->value->uint32 != 0;
}

}

std::expected<bool, FITError> is_stub(std::string_view fit)
{
    FITFile file { fit };
    auto    found = false;
    TRY(file.read_until([&found](FITDataRecord const &rec) {
        found = rec.mesg_num == mesg_num::record;
        return found;
    }));
    return !found;
}

std::expected<Recorded, FITError> what_is_recorded(std::string_view fit)
{
    FITFile  file { fit };
    Recorded ret;
    TRY(file.read_until([&ret](FITDataRecord const &rec) {
        if (rec.mesg_num == mesg_num::record) {
            ret.records = true;
            // Both, as the activity reads them: with one, there's no position.
            ret.positions = ret.positions || (has_value(rec, 0) && has_value(rec, 1));
        } else if (rec.mesg_num == mesg_num::file_id || rec.mesg_num == mesg_num::device_info) {
            ret.serial = ret.serial || has_serial(rec);
        }
        // A device's file with a serial has no positions until GPS has a
        // fix, so only a position ends it.
        return ret.positions;
    }));
    return ret;
}

bool lost_track_of(RecordingInfo const &rewrite, RecordingInfo const &original)
{
    auto const distance = std::abs(rewrite.distance - original.distance);
    return rewrite.start == original.start && distance <= SameRecordingDistance * std::max(rewrite.distance, original.distance);
}

StorageResult<bool> is_stub_file(fs::path const &fit)
{
    auto const contents = TRY_EVAL(read_file_by_name(fit.string()));
    return TRY_EVAL(is_stub(contents));
}

Slot slot_for(Storage const &storage, i64 start)
{
    auto const near = filed_near(storage, start);
    if (!near.empty() && near.front().start == start && !is_stub_or_unreadable(fit_of(near.front().sidecar))) {
        return Slot { .kind = Slot::Kind::Taken };
    }
    for (auto const &stored : near) {
        if (is_stub_or_unreadable(fit_of(stored.sidecar))) {
            return Slot { .kind = Slot::Kind::Stub, .stub = stored };
        }
    }
    return Slot { .kind = storage.has_activity(start) ? Slot::Kind::Taken : Slot::Kind::Free };
}

bool wants_activity(Storage const &storage, i64 start)
{
    return slot_for(storage, start).kind != Slot::Kind::Taken;
}

Error<StorageError> replace_stub(Storage const &storage, StoredStart const &stub, std::string_view fit, i64 start, std::optional<JSONValue> const &incoming)
{
    auto const sidecar = storage.sidecar_of(start);
    auto const dir = TRY_EVAL(ensure_directory(sidecar.parent_path()));
    auto const original = fit_of(sidecar);
    auto const kept = fs::path { original } += StubSuffix;
    TRY(swap_in(fit_of(stub.sidecar), kept, original, fit));

    // The original is in place: the sidecar is worth a warning at most.
    if (stub.start != start && path_exists(stub.sidecar).value_or(false)) {
        if (path_exists(sidecar).value_or(true)) {
            warning(Storage, "Leaving sidecar `{}`: `{}` is there", stub.sidecar.string(), sidecar.string());
            return { };
        }
        std::error_code ec { };
        fs::rename(stub.sidecar, sidecar, ec);
        if (ec) {
            warning(Storage, "Couldn't move sidecar `{}`: {}", stub.sidecar.string(), LibCError { ec }.to_string());
            return { };
        }
    }
    auto const updated = update_sidecar(sidecar, [&](JSONValue &json) {
        if (json.has(StartTimeKey)) {
            json.set(StartTimeKey, JSONValue { start });
        }
        if (incoming && incoming->is_object()) {
            for (auto it = incoming->obj_begin(); it != incoming->obj_end(); ++it) {
                if (!json.has(it->first)) {
                    json.set(it->first, it->second);
                }
            }
        }
    });
    if (!updated) {
        warning(Storage, "Couldn't update sidecar `{}`: {}", sidecar.string(), updated.error().to_string());
    }
    return { };
}

Error<StorageError> replace_in_place(fs::path const &stored, std::string_view fit)
{
    return swap_in(stored, fs::path { stored } += StubSuffix, stored, fit);
}

}
