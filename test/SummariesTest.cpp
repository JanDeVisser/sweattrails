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
#include <cmath>
#include <filesystem>
#include <format>

#include <Date.h>
#include <FileIO.h>

#include <storage/PaceCurve.h>
#include <storage/PowerCurve.h>
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
        .power = { 812, 790, 702, 455 },
        .best_times = { 81, 170, 215 },
    };
}

// A FIT file with one session has one summary.
ActivitySummary read_one(Storage &storage, SummaryFile const &file)
{
    auto const summaries = read_file_summaries(storage, file);
    CHECK_EQ(summaries.size(), 1uz);
    return summaries.empty() ? ActivitySummary { } : summaries.front();
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
    unreadable.power.clear();
    unreadable.best_times.clear();
    auto leg = summary("2025/03/1741000200.fit", 1741000200);
    leg.leg = 2;
    leg.leg_name = "Bike";
    ActivitySummaries const written { summary("2025/03/1741000000.fit", 1741000000), unreadable, leg };
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
    MUST(write_file_by_name(path.string(), std::string_view { R"({ "version": 5, "activities": [ { "file": "a.fit" } ] })" }));
    CHECK(read_summaries(path).empty());
    // Version 2 had no power curves.
    MUST(write_file_by_name(path.string(), std::string_view { R"({ "version": 2, "activities": [ { "file": "a.fit", "size": 1, "modified": 1, "start": 1, "distance": 0, "moving": 0, "ascent": 0 } ] })" }));
    CHECK(read_summaries(path).empty());
    MUST(write_file_by_name(path.string(), std::string_view { R"({ "version": 5, "activities": [ { "file": "a.fit", "size": 1, "modified": 1, "start": 1, "distance": 0, "moving": 0, "ascent": 0, "power": [ "x" ] } ] })" }));
    CHECK(read_summaries(path).empty());
    // Version 4 counted a multisport file as its last leg.
    MUST(write_file_by_name(path.string(), std::string_view { R"({ "version": 4, "activities": [ { "file": "a.fit", "size": 1, "modified": 1, "start": 1, "distance": 0, "moving": 0, "ascent": 0 } ] })" }));
    CHECK(read_summaries(path).empty());
    // Version 3 had no best times.
    MUST(write_file_by_name(path.string(), std::string_view { R"({ "version": 3, "activities": [ { "file": "a.fit", "size": 1, "modified": 1, "start": 1, "distance": 0, "moving": 0, "ascent": 0 } ] })" }));
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

// A multisport file's legs stay, all of them, or are read again.
void test_plan_update_legs()
{
    auto first = summary("2025/03/1.fit", 1);
    first.leg = 0;
    auto second = summary("2025/03/1.fit", 2);
    second.leg = 1;
    ActivitySummaries const summaries { first, second };
    CHECK(plan_update(summaries, { first.source }).kept == summaries);
    auto touched = first.source;
    touched.modified = 2000;
    auto const work = plan_update(summaries, { touched });
    CHECK(work.kept.empty());
    CHECK(work.to_read == std::vector { touched });
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
    auto const ride = read_one(store.storage, (*files)[0]);
    // NP is worked out in floats, which can round 200 W down.
    CHECK(ride.np && (*ride.np == 199 || *ride.np == 200));
    CHECK(ride.hr_load == 150.0 * 150 * 119);
    // Two minutes at 200 W: every duration up to 2 minutes.
    CHECK_EQ(ride.power.size(), 13uz);
    CHECK(std::ranges::all_of(ride.power, [](u16 watts) { return watts == 200; }));
}

// A record a second for `count` seconds from `start` after Start, at a
// heart rate and a power, and at `speed` m/s from `from` metres.
void add_leg_records(Test::FITBuilder &fit, u32 start, u32 count, u8 heart_rate, u16 power, float speed, float from)
{
    fit.definition(5, 20,
        {
            { .num = 253, .size = 4, .base_type = Test::Uint32 },
            { .num = 3, .size = 1, .base_type = Test::Uint8 },
            { .num = 5, .size = 4, .base_type = Test::Uint32 },
            { .num = 7, .size = 2, .base_type = Test::Uint16 },
        });
    for (auto at = 0u; at < count; ++at) {
        fit.message(5);
        fit.u32_le(Test::Start + start + at);
        fit.byte(heart_rate);
        fit.u32_le(static_cast<u32>(std::lround((from + speed * static_cast<float>(at)) * 100)));
        fit.u16_le(power);
    }
}

// A duathlon: 5 minutes' run at 4 m/s, a transition of 30 s, 10 minutes'
// ride at 7.5 m/s and 200 W, and 5 minutes' run. Each leg is summarized
// from its own records: the runs' bests aren't the ride's.
void test_read_multisport_by_leg()
{
    Test::TempStore  store;
    Test::FITBuilder fit;
    Test::add_file_id(fit, 1);
    add_leg_records(fit, 0, 300, 150, 0, 4, 0);
    add_leg_records(fit, 300, 30, 130, 0, 2, 1200);
    add_leg_records(fit, 330, 600, 140, 200, 7.5, 1260);
    add_leg_records(fit, 930, 300, 160, 0, 4, 5760);
    Test::add_session(fit, 300, 10, Test::NoSubSport, 0, 1);
    Test::add_session(fit, 30, 0xFFFF, Test::NoSubSport, 300, 3);
    Test::add_session(fit, 600, 100, Test::NoSubSport, 330, 2);
    Test::add_session(fit, 300, 20, Test::NoSubSport, 930, 1);
    auto const start = static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET;
    // Filed by its last leg's start.
    auto const date = DateTime::from_timestamp(static_cast<i32>(start + 930));
    auto const dir = store.storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(date.year), static_cast<u8>(date.month) + 1);
    fs::create_directories(dir);
    MUST(write_file_by_name((dir / std::format("{}.fit", start + 930)).string(), std::string_view { fit.file() }));
    auto const files = summary_files(store.storage);
    if (!files || files->size() != 1) {
        CHECK_EQ(files ? files->size() : 0uz, 1uz);
        return;
    }
    auto const legs = read_file_summaries(store.storage, (*files)[0]);
    if (legs.size() != 4) {
        CHECK_EQ(legs.size(), 4uz);
        return;
    }
    std::vector<u32> const run_bests { 100, 200, 250 };
    for (auto ix = 0u; ix < 4; ++ix) {
        CHECK(legs[ix].source == (*files)[0]);
        CHECK(legs[ix].leg == ix);
    }

    auto const &first_run = legs[0];
    CHECK_EQ(first_run.leg_name, "Run");
    CHECK_EQ(first_run.start, start);
    CHECK(first_run.sport == FIT::sport::Running);
    CHECK_EQ(first_run.moving, 300u);
    CHECK_EQ(first_run.ascent, 10u);
    CHECK(!first_run.np);
    CHECK(first_run.power.empty());
    CHECK(first_run.hr_load == 150.0 * 150 * 299);
    CHECK(first_run.best_times == run_bests);

    auto const &transition = legs[1];
    CHECK_EQ(transition.leg_name, "T1");
    CHECK_EQ(transition.start, start + 300);
    CHECK(transition.sport == FIT::sport::Transition);
    CHECK_EQ(transition.ascent, 0u);
    CHECK(transition.hr_load == 130.0 * 130 * 29);
    CHECK(transition.best_times.empty());

    auto const &ride = legs[2];
    CHECK_EQ(ride.leg_name, "Bike");
    CHECK_EQ(ride.start, start + 330);
    CHECK(ride.sport == FIT::sport::Cycling);
    CHECK_EQ(ride.moving, 600u);
    CHECK_EQ(ride.ascent, 100u);
    CHECK(ride.np && (*ride.np == 199 || *ride.np == 200));
    CHECK(ride.hr_load == 140.0 * 140 * 599);
    // Ten minutes at 200 W: every duration up to 10 minutes.
    CHECK_EQ(ride.power.size(), 18uz);
    CHECK(ride.best_times.empty());

    auto const &second_run = legs[3];
    CHECK_EQ(second_run.leg_name, "Run");
    CHECK_EQ(second_run.start, start + 930);
    CHECK(second_run.sport == FIT::sport::Running);
    CHECK_EQ(second_run.ascent, 20u);
    CHECK(second_run.hr_load == 160.0 * 160 * 299);
    // Not the ride's 400 m in 53 s.
    CHECK(second_run.best_times == run_bests);
}

// A run of 5 minutes at 4 m/s: its best times, up to 1 km. A ride of
// the same has none.
void test_read_best_times()
{
    // A treadmill run (sub-sport 1) has them too: the pace curve leaves
    // it out, so that a change to that rule needs no rebuild.
    struct Case {
        u8 sport;
        u8 sub_sport;
    };
    for (auto const [sport, sub_sport] : { Case { 1, Test::NoSubSport }, Case { 2, Test::NoSubSport }, Case { 1, 1 } }) {
        Test::TempStore  store;
        Test::FITBuilder fit;
        Test::add_file_id(fit, 1);
        Test::add_distance_records(fit, 301, 4);
        Test::add_session(fit, 300, 0, sub_sport, 0, sport);
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
        auto const summary = read_one(store.storage, (*files)[0]);
        if (sport == 1) {
            CHECK(summary.best_times == (std::vector<u32> { 100, 200, 250 }));
        } else {
            CHECK(summary.best_times.empty());
        }
    }
}

// Without power: no NP. A pause of 41 s between two minutes of records
// isn't in the heart rate's sum: 59 s and 59 s at 150 bpm.
void test_read_records_with_a_pause()
{
    Test::TempStore  store;
    Test::FITBuilder fit;
    Test::add_file_id(fit, 1);
    Test::add_records(fit, 60, 150, 0);
    Test::add_records(fit, 60, 150, 0, 100);
    Test::add_session(fit, 160, 0);
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
    auto const run = read_one(store.storage, (*files)[0]);
    CHECK(run.sport.has_value());
    CHECK(!run.np);
    CHECK(run.power.empty());
    CHECK(run.hr_load == 150.0 * 150 * 118);
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

    auto const ride = read_one(store.storage, (*files)[0]);
    CHECK(ride.source == (*files)[0]);
    CHECK_EQ(ride.start, unix_start);
    CHECK(ride.sport == FIT::sport::Cycling);
    CHECK(ride.sub_sport == FIT::sub_sport::virtual_activity);
    CHECK_EQ(ride.moving, 3600u);
    CHECK_EQ(ride.ascent, 250u);

    auto const broken = read_one(store.storage, (*files)[1]);
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

// Noon UTC, so that the date is the same in the time zone the tests run in.
constexpr i64 Nov12Noon = 1794484800; // Thu 12 Nov 2026, 12:00 UTC

ActivitySummaries numbered(size_t count)
{
    ActivitySummaries ret;
    for (auto ix = 0uz; ix < count; ++ix) {
        auto const start = Nov12Noon - static_cast<i64>(ix) * 3600;
        ret.push_back(summary(std::format("2026/11/{}.fit", start), start));
    }
    std::ranges::reverse(ret);
    return ret;
}

ActivitySummary unreadable(i64 start)
{
    auto ret = summary(std::format("2026/11/{}.fit", start), start);
    ret.sport.reset();
    ret.sub_sport.reset();
    return ret;
}

void test_store_summary_empty()
{
    CHECK_EQ(store_summary({ }), "No activities yet");
    // Only files that can't be read: the activity list shows none.
    CHECK_EQ(store_summary({ unreadable(Nov12Noon) }), "No activities yet");
}

void test_store_summary_one()
{
    CHECK_EQ(store_summary(numbered(1)), "1 activity, the newest on Thu 12 Nov 2026");
}

void test_store_summary_thousands()
{
    CHECK_EQ(store_summary(numbered(1234)), "1,234 activities, the newest on Thu 12 Nov 2026");
    CHECK_EQ(store_summary(numbered(123)), "123 activities, the newest on Thu 12 Nov 2026");
}

// A newer file that can't be read is neither counted nor the newest.
void test_store_summary_skips_unreadable()
{
    auto summaries = numbered(2);
    summaries.push_back(unreadable(Nov12Noon + 2 * 86400));
    CHECK_EQ(store_summary(summaries), "2 activities, the newest on Thu 12 Nov 2026");
}

// A multisport file's legs are one activity, as the activity list has it.
void test_store_summary_counts_files()
{
    auto summaries = numbered(2);
    for (auto ix = 0u; ix < 3; ++ix) {
        auto leg = summary("2026/11/duathlon.fit", Nov12Noon - 86400 + ix * 600);
        leg.leg = ix;
        summaries.push_back(leg);
    }
    merge_summaries(summaries, { });
    CHECK_EQ(store_summary(summaries), "3 activities, the newest on Thu 12 Nov 2026");
}

// The newest by its start, wherever it is in the list.
void test_store_summary_out_of_order()
{
    ActivitySummaries summaries { summary("2026/11/b.fit", Nov12Noon), summary("2026/10/a.fit", Nov12Noon - 30 * 86400) };
    CHECK_EQ(store_summary(summaries), "2 activities, the newest on Thu 12 Nov 2026");
}

Month month(size_t year, DateTime::Month m)
{
    return Month { year, m };
}

// Readable files count, one that can't be read doesn't, nor does it count
// again from the files. A multisport file is one activity, and a file
// without a summary yet counts.
void test_month_counts()
{
    using enum DateTime::Month;
    auto broken = summary("2025/03/3.fit", 3);
    broken.sport.reset();
    ActivitySummaries summaries { summary("2024/11/1.fit", 1), summary("2025/03/2.fit", 2), broken };
    for (auto ix = 0u; ix < 3; ++ix) {
        auto leg = summary("2025/03/4.fit", 4 + ix);
        leg.leg = ix;
        summaries.push_back(leg);
    }
    std::vector<std::string> const files { "2024/11/1.fit", "2025/03/2.fit", "2025/03/3.fit", "2025/03/4.fit", "2025/05/5.fit" };
    auto const                     counts = month_counts(summaries, files);
    CHECK_EQ(counts.size(), 3uz);
    CHECK_EQ(counts.contains(month(2024, November)) ? counts.at(month(2024, November)) : 0uz, 1uz);
    CHECK_EQ(counts.contains(month(2025, March)) ? counts.at(month(2025, March)) : 0uz, 2uz);
    CHECK_EQ(counts.contains(month(2025, May)) ? counts.at(month(2025, May)) : 0uz, 1uz);
}

// A month with only a file that can't be read isn't there at all.
void test_month_counts_leaves_out_empty_months()
{
    auto broken = summary("2025/03/3.fit", 3);
    broken.sport.reset();
    auto const counts = month_counts({ broken }, { "2025/03/3.fit" });
    CHECK(counts.empty());
}

// From the store: a FIT file counts, a .strava file alone doesn't, as the
// activity list doesn't list it.
void test_month_counts_from_the_store()
{
    Test::TempStore store;
    store.add_activity(Nov12Noon);
    auto const dir = store.storage.activity_dir / "2026/09";
    fs::create_directories(dir);
    MUST(write_file_by_name((dir / "1789000000.strava").string(), std::string_view { "{}" }));
    auto const counts = month_counts(store.storage, { });
    CHECK(counts.has_value());
    if (counts) {
        CHECK_EQ(counts->size(), 1uz);
        CHECK(counts->contains(month(2026, DateTime::Month::November)));
    }
}

MonthCounts some_counts()
{
    using enum DateTime::Month;
    return MonthCounts { { month(2021, May), 3 }, { month(2023, January), 1 }, { month(2023, June), 2 }, { month(2024, February), 5 } };
}

// Skipping 2022, which has nothing, and nothing past the ends.
void test_year_next_to()
{
    auto const counts = some_counts();
    CHECK(year_next_to(counts, 2021, 1) == std::optional<size_t> { 2023 });
    CHECK(year_next_to(counts, 2023, -1) == std::optional<size_t> { 2021 });
    CHECK(year_next_to(counts, 2023, 1) == std::optional<size_t> { 2024 });
    CHECK(!year_next_to(counts, 2024, 1));
    CHECK(!year_next_to(counts, 2021, -1));
    CHECK(!year_next_to({ }, 2021, 1));
}

void test_month_to_select()
{
    using enum DateTime::Month;
    auto const counts = some_counts();
    // The month on display, in that year.
    CHECK(month_to_select(counts, 2023, month(2023, January)) == std::optional { month(2023, January) });
    // Else the year's latest: on display in another year, in a month
    // without activities, or nothing on display.
    CHECK(month_to_select(counts, 2023, month(2021, May)) == std::optional { month(2023, June) });
    CHECK(month_to_select(counts, 2023, month(2023, March)) == std::optional { month(2023, June) });
    CHECK(month_to_select(counts, 2023, { }) == std::optional { month(2023, June) });
    CHECK(month_to_select(counts, 2021, { }) == std::optional { month(2021, May) });
    CHECK(!month_to_select(counts, 2022, { }));
}

// A file with longer curves than there are durations and distances, as
// one edited by hand: they're cut to them.
void test_curves_no_longer_than_their_lists()
{
    Test::TempDir dir { "summaries" };
    auto const    path = dir.path / "summaries.json";
    std::string   power;
    for (auto ix = 0uz; ix < PowerDurations.size() + 5; ++ix) {
        power += std::format("{}{}", ix == 0 ? "" : ", ", 300);
    }
    std::string times;
    for (auto ix = 0uz; ix < PaceDistances.size() + 5; ++ix) {
        times += std::format("{}{}", ix == 0 ? "" : ", ", 100 + ix);
    }
    MUST(write_file_by_name(path.string(),
        std::format(R"({{ "version": {}, "activities": [ {{ "file": "a.fit", "size": 1, "modified": 1, "start": 1, "distance": 0, "moving": 0, "ascent": 0, "power": [ {} ], "best_times": [ {} ] }} ] }})",
            SummariesVersion, power, times)));
    auto const read = read_summaries(path);
    CHECK_EQ(read.size(), 1uz);
    if (read.size() == 1) {
        CHECK_EQ(read[0].power.size(), PowerDurations.size());
        CHECK_EQ(read[0].best_times.size(), PaceDistances.size());
    }
}

}

int main()
{
    test_round_trip();
    test_read_sorts_by_start();
    test_no_file();
    test_plan_update();
    test_plan_update_legs();
    test_plan_from_nothing();
    test_merge_summaries();
    test_read_records();
    test_read_multisport_by_leg();
    test_read_records_with_a_pause();
    test_read_best_times();
    test_read_from_the_store();
    test_second_start_reads_nothing();
    test_empty_store();
    test_update_in_chunks();
    test_update_reads_what_came_in_between();
    test_update_that_cant_write();
    test_summary_activity();
    test_month_counts();
    test_month_counts_leaves_out_empty_months();
    test_month_counts_from_the_store();
    test_year_next_to();
    test_month_to_select();
    test_curves_no_longer_than_their_lists();
    test_store_summary_empty();
    test_store_summary_one();
    test_store_summary_thousands();
    test_store_summary_skips_unreadable();
    test_store_summary_out_of_order();
    test_store_summary_counts_files();
    return Test::test_result();
}
