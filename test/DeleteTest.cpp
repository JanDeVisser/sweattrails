/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Deleting an activity, or one of its recordings: where its files go, what
 * becomes of the others, and that the downloads don't fetch it again. See
 * specs/54-delete-activity.md.
 */

#include <filesystem>
#include <format>
#include <string>

#include <FileIO.h>
#include <JSON.h>

#include <storage/Recordings.h>
#include <storage/Storage.h>
#include <storage/Stubs.h>

#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>

using namespace ST;
using FIT::manufacturer;

namespace fs = std::filesystem;

namespace {

constexpr u16 Garmin = 1;
constexpr u16 Wahoo = 32;
constexpr u16 Zwift = 260;

i64 unix_start(u32 offset)
{
    return static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET + offset;
}

std::string month_of(i64 start)
{
    auto const date = DateTime::from_timestamp(static_cast<i32>(start));
    return std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1);
}

// A recording that starts `offset` seconds after Test::Start, in the inbox.
void record(Test::TempStore const &store, u16 device, u32 offset, u32 seconds)
{
    Test::FITBuilder fit;
    Test::add_file_id(fit, device);
    Test::add_session(fit, seconds, 0, Test::NoSubSport, offset);
    MUST(write_file_by_name((store.storage.inbox_dir / std::format("{}-{}.fit", device, offset)).string(), std::string_view { fit.file() }));
}

// The inbox, as the app runs it: grouping what it files.
void file(Test::TempStore &store)
{
    auto const filed = store.storage.process_inbox({ }, [&store](std::string_view, Storage::InboxFiling const &filing) {
        if (filing.filed()) {
            CHECK(group_new(store.storage, filing.start).has_value());
        }
    });
    CHECK(filed.has_value());
}

// Where the inbox files the activity starting `offset` seconds after
// Test::Start, or where deleting it puts it.
fs::path fit_of(fs::path const &root, u32 offset)
{
    return root / month_of(unix_start(offset)) / std::format("{}.fit", unix_start(offset));
}

fs::path filed(Test::TempStore const &store, u32 offset)
{
    return fit_of(store.storage.activity_dir, offset);
}

fs::path in_deleted(Test::TempStore const &store, u32 offset)
{
    return fit_of(store.storage.deleted_dir, offset);
}

fs::path in_folder(fs::path const &main, u32 offset)
{
    return recordings_dir(main) / std::format("{}.fit", unix_start(offset));
}

size_t listed(Test::TempStore const &store)
{
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start(0)));
    return store.storage.month_starts(Month { static_cast<size_t>(date.year), date.month }).value_or(std::vector<StoredStart> { }).size();
}

void write_sidecar(fs::path const &fit, std::string_view json)
{
    MUST(write_file_by_name(fs::path { fit }.replace_extension(".json").string(), json));
}

std::string title_of(fs::path const &fit)
{
    return sidecar_own_title(fs::path { fit }.replace_extension(".json")).value_or("");
}

// Three recordings of one ride: the Garmin's, the longest, from 0, the
// Wahoo's from 60, and Zwift's, the shortest, from 120. The Garmin's is the
// main one, named "Morning Ride".
void three_recordings(Test::TempStore &store)
{
    record(store, Garmin, 0, 3600);
    record(store, Wahoo, 60, 3500);
    record(store, Zwift, 120, 3000);
    file(store);
    CHECK_EQ(listed(store), 1uz);
    CHECK(fs::exists(in_folder(filed(store, 0), 60)));
    CHECK(fs::exists(in_folder(filed(store, 0), 120)));
    write_sidecar(filed(store, 0), R"({ "title": "Morning Ride" })");
}

// Puts a file in deleted/, as deleting the activity that starts at
// `start` would have, in `dir`, its month by default.
void deleted(Test::TempStore const &store, i64 start, std::string dir = "")
{
    if (dir.empty()) {
        dir = month_of(start);
    }
    fs::create_directories(store.storage.deleted_dir / dir);
    MUST(write_file_by_name((store.storage.deleted_dir / dir / std::format("{}.fit", start)).string(), std::string_view { "fit" }));
}

// The downloads, which ask has_activity(), treat a deleted start as one
// the store has; fit_at() doesn't find it.
void test_deleted_start_is_had()
{
    Test::TempStore store;
    auto const      start = unix_start(0);
    CHECK(!store.storage.was_deleted(start));
    CHECK(!store.storage.has_activity(start));
    CHECK(wants_activity(store.storage, start));
    deleted(store, start);
    CHECK(store.storage.was_deleted(start));
    CHECK(store.storage.has_activity(start));
    CHECK(!wants_activity(store.storage, start));
    CHECK(!store.storage.fit_at(start).has_value());
    CHECK(!store.storage.was_deleted(start + 1));
}

// One filed in another time zone's month is found too, as in activity/.
void test_deleted_in_the_month_next_to_it()
{
    Test::TempStore store;
    auto const      start = unix_start(0);
    auto const      date = DateTime::from_timestamp(static_cast<i32>(start));
    auto const      next = Month { static_cast<size_t>(date.year), date.month }.plus(1);
    deleted(store, start, next.dir_name());
    CHECK(store.storage.was_deleted(start));
    CHECK(store.storage.has_activity(start));
}

// A file put in the inbox at a deleted start is filed, and the deleted
// copy stays.
void test_inbox_files_a_deleted_start()
{
    Test::TempStore store;
    auto const      start = unix_start(0);
    deleted(store, start);
    record(store, Garmin, 0, 3600);
    auto const filed = store.storage.process_inbox();
    CHECK_EQ(filed.value_or(0), 1u);
    CHECK(store.storage.fit_at(start).has_value());
    CHECK(store.storage.was_deleted(start));
    CHECK(!fs::exists(store.storage.inbox_dir / "duplicates"));
}

// All of an activity's files go into deleted/, in its month, keeping
// their names.
void test_delete_an_activity()
{
    Test::TempStore store;
    record(store, Garmin, 0, 3600);
    file(store);
    auto const fit = filed(store, 0);
    write_sidecar(fit, R"({ "title": "Morning Ride" })");
    for (auto const *ext : { ".fit.original", ".fit.stub", ".strava" }) {
        MUST(write_file_by_name(fs::path { fit }.replace_extension(ext).string(), std::string_view { "more" }));
    }
    auto const deleted = delete_recording(store.storage, fit);
    CHECK(deleted.has_value());
    if (!deleted) {
        return;
    }
    CHECK_EQ(deleted->to.generic_string(), in_deleted(store, 0).generic_string());
    CHECK(!deleted->main.has_value());
    for (auto const *ext : { ".fit", ".json", ".fit.original", ".fit.stub", ".strava" }) {
        CHECK(fs::exists(fs::path { in_deleted(store, 0) }.replace_extension(ext)));
        CHECK(!fs::exists(fs::path { fit }.replace_extension(ext)));
    }
    CHECK_EQ(title_of(deleted->to), "Morning Ride");
    CHECK_EQ(listed(store), 0uz);
    CHECK(store.storage.has_activity(unix_start(0)));
    // It's gone: deleting it again is an error.
    CHECK(!delete_recording(store.storage, fit).has_value());
}

// Deleted, put back through the inbox, and deleted again: both copies are
// kept.
void test_delete_twice()
{
    Test::TempStore store;
    record(store, Garmin, 0, 3600);
    file(store);
    CHECK(delete_recording(store.storage, filed(store, 0)).has_value());
    record(store, Garmin, 0, 3600);
    file(store);
    CHECK(fs::exists(filed(store, 0)));
    auto const again = delete_recording(store.storage, filed(store, 0));
    CHECK(again.has_value());
    auto const second = in_deleted(store, 0).parent_path() / std::format("{}-1.fit", unix_start(0));
    CHECK_EQ(again.value_or(Deleted { }).to.generic_string(), second.generic_string());
    CHECK(fs::exists(in_deleted(store, 0)));
    CHECK(fs::exists(second));
    CHECK(!fs::exists(filed(store, 0)));
}

// Another recording goes flat into deleted/, in its activity's month; the
// activity keeps its main one and its other recording.
void test_delete_another_recording()
{
    Test::TempStore store;
    three_recordings(store);
    auto const main = filed(store, 0);
    auto const deleted = delete_recording(store.storage, in_folder(main, 60));
    CHECK(deleted.has_value());
    CHECK_EQ(deleted.value_or(Deleted { }).to.generic_string(), in_deleted(store, 60).generic_string());
    CHECK(deleted.value_or(Deleted { }).main == main);
    CHECK(!fs::exists(in_folder(main, 60)));
    CHECK(fs::exists(in_folder(main, 120)));
    CHECK_EQ(title_of(main), "Morning Ride");
    CHECK_EQ(listed(store), 1uz);
    CHECK(store.storage.was_deleted(unix_start(60)));

    // The last other one: the folder goes.
    CHECK(delete_recording(store.storage, in_folder(main, 120)).has_value());
    CHECK(fs::exists(in_deleted(store, 120)));
    CHECK(!fs::exists(recordings_dir(main)));
    CHECK(fs::exists(main));
    CHECK_EQ(listed(store), 1uz);
}

// Deleting the main one makes the longest of the others the main one,
// filed where the inbox files it, with the activity's name.
void test_delete_the_main_one()
{
    Test::TempStore store;
    three_recordings(store);
    auto const deleted = delete_recording(store.storage, filed(store, 0));
    CHECK(deleted.has_value());
    CHECK_EQ(deleted.value_or(Deleted { }).to.generic_string(), in_deleted(store, 0).generic_string());
    auto const main = filed(store, 60);
    CHECK(deleted.value_or(Deleted { }).main == main);
    CHECK(fs::exists(main));
    CHECK(fs::exists(in_folder(main, 120)));
    CHECK(!fs::exists(recordings_dir(filed(store, 0))));
    CHECK_EQ(title_of(main), "Morning Ride");
    CHECK_EQ(title_of(in_deleted(store, 0)), "Morning Ride");
    CHECK_EQ(listed(store), 1uz);
}

// The preferred device's is the new main one, and a name of its own is
// kept.
void test_delete_the_main_one_preferred()
{
    Test::TempStore store;
    three_recordings(store);
    write_sidecar(in_folder(filed(store, 0), 120), R"({ "title": "Zwift's" })");
    store.storage.prefer = { manufacturer::zwift };
    auto const deleted = delete_recording(store.storage, filed(store, 0));
    auto const main = filed(store, 120);
    CHECK(deleted.value_or(Deleted { }).main == main);
    CHECK(fs::exists(in_folder(main, 60)));
    CHECK_EQ(title_of(main), "Zwift's");
}

// The main one of two: the other is an activity of its own.
void test_delete_the_main_one_of_two()
{
    Test::TempStore store;
    record(store, Garmin, 0, 3600);
    record(store, Wahoo, 60, 3500);
    file(store);
    CHECK(fs::exists(in_folder(filed(store, 0), 60)));
    auto const deleted = delete_recording(store.storage, filed(store, 0));
    CHECK(deleted.value_or(Deleted { }).main == filed(store, 60));
    CHECK(fs::exists(filed(store, 60)));
    CHECK(!fs::exists(recordings_dir(filed(store, 0))));
    CHECK(!fs::exists(recordings_dir(filed(store, 60))));
    CHECK_EQ(listed(store), 1uz);
}

// A file that isn't in the store is an error, and nothing moves.
void test_delete_missing()
{
    Test::TempStore store;
    auto const      deleted = delete_recording(store.storage, filed(store, 0));
    CHECK(!deleted.has_value());
    CHECK(!fs::exists(store.storage.deleted_dir));
}

// Deleting the main one when another recording can't be read leaves the
// store as it was: the main one is still there, with its folder.
void test_delete_the_main_one_unreadable_other()
{
    Test::TempStore store;
    three_recordings(store);
    auto const main = filed(store, 0);
    MUST(write_file_by_name(in_folder(main, 120).string(), std::string_view { "not a fit file" }));
    auto const deleted = delete_recording(store.storage, main);
    CHECK(!deleted.has_value());
    CHECK(fs::exists(main));
    CHECK(fs::exists(in_folder(main, 60)));
    CHECK(fs::exists(in_folder(main, 120)));
    CHECK(!fs::exists(in_deleted(store, 0)));
}

// A recording of a start that's in deleted/ already is kept next to it.
void test_delete_another_recording_twice()
{
    Test::TempStore store;
    three_recordings(store);
    auto const main = filed(store, 0);
    deleted(store, unix_start(60));
    auto const result = delete_recording(store.storage, in_folder(main, 60));
    CHECK(result.has_value());
    CHECK(fs::exists(in_deleted(store, 60)));
    CHECK_EQ(result.value_or(Deleted { }).to.filename().string(), std::format("{}-1.fit", unix_start(60)));
    CHECK(fs::exists(result.value_or(Deleted { }).to));
}

}

int main()
{
    test_deleted_start_is_had();
    test_deleted_in_the_month_next_to_it();
    test_inbox_files_a_deleted_start();
    test_delete_an_activity();
    test_delete_twice();
    test_delete_another_recording();
    test_delete_the_main_one();
    test_delete_the_main_one_preferred();
    test_delete_the_main_one_of_two();
    test_delete_missing();
    test_delete_the_main_one_unreadable_other();
    test_delete_another_recording_twice();
    return ST::Test::test_result();
}
