/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the summary file: reading and writing it, which files an update
 * reads, and reading a FIT file's summary.
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>

#include <Date.h>
#include <IO.h>

#include <storage/Summaries.h>

#include <TempDir.h>
#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>

using namespace ST;

namespace fs = std::filesystem;

namespace {

ActivitySummary summary(std::string const &file, i64 start)
{
    return ActivitySummary {
        .source = SummaryFile { .file = file, .size = 100, .modified = 1000 },
        .start = start,
        .sport = FIT::sport::Cycling,
        .sub_sport = FIT::sub_sport::virtual_activity,
        .distance = 42310.5,
        .moving = 5530,
        .ascent = 412,
        .np = 231,
        .hr_load = 1.2e8,
    };
}

void test_round_trip()
{
    Test::TempDir dir { "summaries" };
    auto const    path = dir.path / "summaries.json";
    auto          unreadable = summary("2025/03/1741000100.fit", 1741000100);
    unreadable.sport.reset();
    unreadable.sub_sport.reset();
    unreadable.np.reset();
    unreadable.hr_load.reset();
    ActivitySummaries const written { summary("2025/03/1741000000.fit", 1741000000), unreadable };
    CHECK(write_summaries(path, written).has_value());
    CHECK(read_summaries(path) == written);
}

void test_read_sorts_by_start()
{
    Test::TempDir dir { "summaries" };
    auto const    path = dir.path / "summaries.json";
    CHECK(write_summaries(path, { summary("b.fit", 20), summary("a.fit", 10) }).has_value());
    auto const read = read_summaries(path);
    CHECK_EQ(read.size(), 2uz);
    if (read.size() == 2) {
        CHECK_EQ(read[0].start, 10);
        CHECK_EQ(read[1].start, 20);
    }
}

// Missing, broken, or of another version: none, and it's built again.
void test_no_file()
{
    Test::TempDir dir { "summaries" };
    auto const    path = dir.path / "summaries.json";
    CHECK(read_summaries(path).empty());
    MUST(write_file_by_name(path.string(), std::string_view { "{ not json" }));
    CHECK(read_summaries(path).empty());
    MUST(write_file_by_name(path.string(), std::string_view { R"({ "version": 2, "activities": [] })" }));
    CHECK(read_summaries(path).empty());
    MUST(write_file_by_name(path.string(), std::string_view { R"({ "version": 2, "activities": [ { "file": "a.fit" } ] })" }));
    CHECK(read_summaries(path).empty());
    // Version 1 had no NP or heart rate.
    MUST(write_file_by_name(path.string(), std::string_view { R"({ "version": 1, "activities": [ { "file": "a.fit", "size": 1, "modified": 1, "start": 1, "distance": 0, "moving": 0, "ascent": 0 } ] })" }));
    CHECK(read_summaries(path).empty());
}

void test_plan_update()
{
    ActivitySummaries const summaries {
        summary("2025/03/1.fit", 1),
        summary("2025/03/2.fit", 2),
        summary("2025/03/3.fit", 3),
    };
    std::vector<SummaryFile> const files {
        summaries[0].source,                                                    // Unchanged
        SummaryFile { .file = "2025/03/2.fit", .size = 101, .modified = 1000 }, // Grew
        SummaryFile { .file = "2025/04/4.fit", .size = 100, .modified = 1000 }, // New
        // 3.fit is gone.
    };
    auto const work = plan_update(summaries, files);
    CHECK(work.kept == ActivitySummaries { summaries[0] });
    CHECK(work.to_read == (std::vector { files[1], files[2] }));

    auto touched = files;
    touched[0].modified = 2000;
    CHECK(plan_update(summaries, { touched[0] }).kept.empty());
}

// The first build: no summaries, every file to read.
void test_plan_from_nothing()
{
    std::vector<SummaryFile> const files { { .file = "a.fit" }, { .file = "b.fit" } };
    auto const                     work = plan_update({ }, files);
    CHECK(work.kept.empty());
    CHECK(work.to_read == files);
}

void test_merge_summaries()
{
    ActivitySummaries into { summary("a.fit", 10), summary("c.fit", 30) };
    merge_summaries(into, { summary("b.fit", 20) });
    CHECK_EQ(into.size(), 3uz);
    if (into.size() == 3) {
        CHECK_EQ(into[1].start, 20);
    }
}

// A ride as the inbox would file it, and a file that isn't one.
// The NP and Σ HR² × seconds from the records: 120 records a second
// apart, at 150 bpm and 200 W, have 119 seconds between them.
void test_read_records()
{
    Test::TempStore  store;
    Test::FITBuilder fit;
    Test::add_file_id(fit, 1);
    Test::add_records(fit, 120, 150, 200);
    Test::add_session(fit, 120, 0);
    auto const unix_start = static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET;
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start));
    auto const dir = store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1);
    fs::create_directories(dir);
    MUST(write_file_by_name((dir / std::format("{}.fit", unix_start)).string(), std::string_view { fit.file() }));
    auto const files = summary_files(store.storage);
    if (!files || files->size() != 1) {
        CHECK_EQ(files ? files->size() : 0uz, 1uz);
        return;
    }
    auto const ride = read_summary(store.storage, (*files)[0]);
    // NP is worked out in floats, which can round 200 W down.
    CHECK(ride.np && (*ride.np == 199 || *ride.np == 200));
    CHECK(ride.hr_load == 150.0 * 150 * 119);
}

void test_read_from_the_store()
{
    Test::TempStore  store;
    Test::FITBuilder fit;
    Test::add_file_id(fit, 1);
    Test::add_session(fit, 3600, 250, 58); // Virtual
    auto const unix_start = static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET;
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start));
    auto const dir = store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1);
    fs::create_directories(dir);
    MUST(write_file_by_name((dir / std::format("{}.fit", unix_start)).string(), std::string_view { fit.file() }));
    store.add_activity(unix_start + 86400); // Not a FIT file

    auto const files = summary_files(store.storage);
    CHECK(files.has_value());
    if (!files || files->size() != 2) {
        CHECK_EQ(files ? files->size() : 0uz, 2uz);
        return;
    }
    CHECK_EQ((*files)[0].file, std::format("{:04}/{:02}/{}.fit", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1, unix_start));
    CHECK_EQ((*files)[0].size, static_cast<u64>(fit.file().size()));

    auto const ride = read_summary(store.storage, (*files)[0]);
    CHECK(ride.source == (*files)[0]);
    CHECK_EQ(ride.start, unix_start);
    CHECK(ride.sport == FIT::sport::Cycling);
    CHECK(ride.sub_sport == FIT::sub_sport::virtual_activity);
    CHECK_EQ(ride.moving, 3600u);
    CHECK_EQ(ride.ascent, 250u);

    auto const broken = read_summary(store.storage, (*files)[1]);
    CHECK(!broken.sport);
    CHECK_EQ(broken.start, unix_start + 86400);
}

// What the job does for a start, in one chunk. How many files it read.
size_t update(Storage &storage)
{
    auto const done = update_summaries(storage, 1000);
    CHECK_EQ(done.error, "");
    CHECK_EQ(done.left, 0uz);
    return done.read;
}

// Rides a day apart, as the inbox would file them.
void add_rides(Test::TempStore const &store, int count, int first_day = 0)
{
    for (auto day = first_day; day < first_day + count; ++day) {
        Test::FITBuilder fit;
        Test::add_file_id(fit, 1);
        Test::add_session(fit, 3600, 250, Test::NoSubSport, static_cast<u32>(day) * 86400);
        auto const unix_start = static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET + day * 86400;
        auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start));
        auto const dir = store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1);
        fs::create_directories(dir);
        MUST(write_file_by_name((dir / std::format("{}.fit", unix_start)).string(), std::string_view { fit.file() }));
    }
}

// Five files two at a time: three chunks, each writing what it read.
void test_update_in_chunks()
{
    Test::TempStore store;
    add_rides(store, 5);
    auto done = update_summaries(store.storage, 2);
    CHECK_EQ(done.read, 2uz);
    CHECK_EQ(done.left, 3uz);
    CHECK_EQ(done.summaries.size(), 2uz);
    CHECK_EQ(read_summaries(summaries_path(store.storage)).size(), 2uz);
    done = update_summaries(store.storage, 2);
    CHECK_EQ(done.read, 2uz);
    CHECK_EQ(done.left, 1uz);
    done = update_summaries(store.storage, 2);
    CHECK_EQ(done.read, 1uz);
    CHECK_EQ(done.left, 0uz);
    CHECK_EQ(done.summaries.size(), 5uz);
    CHECK(std::ranges::is_sorted(done.summaries, { }, &ActivitySummary::start));
    CHECK_EQ(done.error, "");
}

// The inbox files a ride between two chunks: the next chunk reads it.
void test_update_reads_what_came_in_between()
{
    Test::TempStore store;
    add_rides(store, 3);
    auto done = update_summaries(store.storage, 2);
    CHECK_EQ(done.left, 1uz);
    add_rides(store, 1, 10);
    done = update_summaries(store.storage, 2);
    CHECK_EQ(done.read, 2uz);
    CHECK_EQ(done.left, 0uz);
    CHECK_EQ(done.summaries.size(), 4uz);
}

// A summary file that can't be written stops the update, saying why:
// the next chunk would read the same files again.
void test_update_that_cant_write()
{
    Test::TempStore store;
    add_rides(store, 3);
    // A directory where the file goes.
    fs::create_directories(summaries_path(store.storage));
    auto const done = update_summaries(store.storage, 2);
    CHECK(done.error.starts_with("Couldn't save the totals"));
    CHECK_EQ(done.summaries.size(), 2uz);
}

// The first start reads every file, the next one none, so that the job
// doesn't go round reading the same files; a changed file is read again,
// and one that's gone drops out.
void test_second_start_reads_nothing()
{
    Test::TempStore  store;
    Test::FITBuilder fit;
    Test::add_file_id(fit, 1);
    Test::add_session(fit, 3600, 250);
    auto const unix_start = static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET;
    auto const date = DateTime::from_timestamp(static_cast<i32>(unix_start));
    auto const dir = store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1);
    fs::create_directories(dir);
    auto const ride = dir / std::format("{}.fit", unix_start);
    MUST(write_file_by_name(ride.string(), std::string_view { fit.file() }));
    store.add_activity(unix_start + 86400); // Not a FIT file

    CHECK_EQ(update(store.storage), 2uz);
    auto const first = read_summaries(summaries_path(store.storage));
    CHECK_EQ(first.size(), 2uz);
    CHECK_EQ(update(store.storage), 0uz);
    CHECK(read_summaries(summaries_path(store.storage)) == first);

    fs::last_write_time(ride, fs::last_write_time(ride) + std::chrono::seconds { 10 });
    CHECK_EQ(update(store.storage), 1uz);
    CHECK_EQ(read_summaries(summaries_path(store.storage)).size(), 2uz);

    fs::remove(dir / std::format("{}.fit", unix_start + 86400));
    CHECK_EQ(update(store.storage), 0uz);
    auto const last = read_summaries(summaries_path(store.storage));
    CHECK_EQ(last.size(), 1uz);
    if (last.size() == 1) {
        CHECK(last[0].sport == FIT::sport::Cycling);
    }
}

// An empty store has no files and an empty summary file.
void test_empty_store()
{
    Test::TempStore store;
    auto const      files = summary_files(store.storage);
    CHECK(files.has_value() && files->empty());
    CHECK_EQ(update(store.storage), 0uz);
    CHECK(read_summaries(summaries_path(store.storage)).empty());
}

void test_summary_activity()
{
    auto const id = summary_activity(summary("2025/03/1741000000.fit", 1741000000));
    CHECK(id.has_value());
    if (id) {
        CHECK((id->month == Month { 2025, DateTime::Month::March }));
        CHECK_EQ(id->name, "1741000000");
        CHECK(id->files.contains(ActivityFile::Fit));
    }
    // "08" and "09", which aren't octal.
    auto const august = summary_activity(summary("2024/08/1723000000.fit", 1723000000));
    CHECK(august && august->month == (Month { 2024, DateTime::Month::August }));
    CHECK(!summary_activity(summary("1741000000.fit", 1741000000)));
    CHECK(!summary_activity(summary("2025/13/1741000000.fit", 1741000000)));
    CHECK(!summary_activity(summary("x/2025/03/1741000000.fit", 1741000000)));
}

}

int main()
{
    test_round_trip();
    test_read_sorts_by_start();
    test_no_file();
    test_plan_update();
    test_plan_from_nothing();
    test_merge_summaries();
    test_read_records();
    test_read_from_the_store();
    test_second_start_reads_nothing();
    test_empty_store();
    test_update_in_chunks();
    test_update_reads_what_came_in_between();
    test_update_that_cant_write();
    test_summary_activity();
    return Test::test_result();
}
