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

#include <IO.h>

#include <storage/Storage.h>
#include <storage/Stubs.h>

#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>

using namespace ST;

namespace fs = std::filesystem;

namespace {

constexpr u16 Garmin = 1;

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

}

int main()
{
    test_deleted_start_is_had();
    test_deleted_in_the_month_next_to_it();
    test_inbox_files_a_deleted_start();
    return ST::Test::test_result();
}
