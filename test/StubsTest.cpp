/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests recognising activities that are only a summary, and replacing
 * them with their recordings: see specs/50-stub-activities.md.
 */

#include <filesystem>
#include <format>
#include <string>

#include <IO.h>
#include <JSON.h>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <storage/Stubs.h>

#include <TempStore.h>
#include <TestApp.h>
#include <TestFit.h>

using namespace ST;

namespace fs = std::filesystem;

namespace {

constexpr u16 Wahoo = 32;
constexpr u16 Development = 255;

i64 unix_start(i64 offset)
{
    return static_cast<i64>(Test::Start) + DateTime::FIT_TIMESTAMP_OFFSET + offset;
}

// A run's summary, as the old app made them: a session, no records.
std::string stub_fit(u32 offset = 0)
{
    Test::FITBuilder fit;
    Test::add_file_id(fit, Development);
    Test::add_session(fit, 600, 10, Test::NoSubSport, offset);
    return fit.file();
}

// What the device recorded: the session and a record a second.
std::string recorded_fit(u32 offset = 0)
{
    Test::FITBuilder fit;
    Test::add_file_id(fit, Wahoo);
    Test::add_records(fit, 600, 140, 200, offset);
    Test::add_session(fit, 600, 10, Test::NoSubSport, offset);
    return fit.file();
}

fs::path month_dir(Test::TempStore const &store, i64 offset = 0)
{
    return store.storage.sidecar_of(unix_start(offset)).parent_path();
}

fs::path fit_at(Test::TempStore const &store, i64 offset, std::string_view suffix = "")
{
    return month_dir(store, offset) / std::format("{}.fit{}", unix_start(offset), suffix);
}

fs::path sidecar_at(Test::TempStore const &store, i64 offset)
{
    return store.storage.sidecar_of(unix_start(offset));
}

// Files `contents` in the store as starting `offset` seconds after
// Test::Start, with `sidecar` next to it, if any.
void file(Test::TempStore const &store, i64 offset, std::string_view contents, std::string_view sidecar = "")
{
    fs::create_directories(month_dir(store, offset));
    MUST(write_file_by_name(fit_at(store, offset).string(), contents));
    if (!sidecar.empty()) {
        MUST(write_file_by_name(sidecar_at(store, offset).string(), sidecar));
    }
}

std::string contents(fs::path const &path)
{
    return read_file_by_name(path.string()).value_or("");
}

JSONValue json_at(fs::path const &path)
{
    return JSONValue::deserialize(contents(path)).value_or(JSONValue { });
}

StoredStart stored(Test::TempStore const &store, i64 offset)
{
    return StoredStart { .start = unix_start(offset), .sidecar = sidecar_at(store, offset) };
}

void test_is_stub()
{
    CHECK_EQ(is_stub(stub_fit()).value_or(false), true);
    CHECK_EQ(is_stub(recorded_fit()).value_or(true), false);
    CHECK(!is_stub("not a FIT file").has_value());
    CHECK(!is_stub("").has_value());
}

void test_is_stub_file()
{
    Test::TempStore store;
    file(store, 0, stub_fit());
    CHECK_EQ(is_stub_file(fit_at(store, 0)).value_or(false), true);
    CHECK(!is_stub_file(fit_at(store, 1)).has_value());
}

void test_slot_free()
{
    Test::TempStore store;
    CHECK(slot_for(store.storage, unix_start(0)).kind == Slot::Kind::Free);
    file(store, 0, recorded_fit());
    CHECK(slot_for(store.storage, unix_start(30)).kind == Slot::Kind::Free);
}

void test_slot_taken()
{
    Test::TempStore store;
    file(store, 0, recorded_fit());
    CHECK(slot_for(store.storage, unix_start(0)).kind == Slot::Kind::Taken);
    CHECK(!wants_activity(store.storage, unix_start(0)));
    CHECK(wants_activity(store.storage, unix_start(1)));
}

// What can't be read is never replaced.
void test_slot_unreadable()
{
    Test::TempStore store;
    store.add_activity(unix_start(0));
    CHECK(slot_for(store.storage, unix_start(0)).kind == Slot::Kind::Taken);
    CHECK(slot_for(store.storage, unix_start(10)).kind == Slot::Kind::Free);
}

void test_slot_stub()
{
    Test::TempStore store;
    file(store, 0, stub_fit());
    for (auto const offset : { 0, 30, -30, 60, -60 }) {
        auto const slot = slot_for(store.storage, unix_start(offset));
        CHECK(slot.kind == Slot::Kind::Stub);
        CHECK_EQ(slot.stub.start, unix_start(0));
        CHECK(slot.stub.sidecar == sidecar_at(store, 0));
    }
    CHECK(wants_activity(store.storage, unix_start(0)));
    CHECK(slot_for(store.storage, unix_start(61)).kind == Slot::Kind::Free);
    CHECK(slot_for(store.storage, unix_start(-61)).kind == Slot::Kind::Free);
}

// A real activity at the start wins over a stub next to it, and a real
// one next to the start doesn't.
void test_slot_real_and_stub()
{
    Test::TempStore store;
    file(store, 0, recorded_fit());
    file(store, 10, stub_fit());
    CHECK(slot_for(store.storage, unix_start(0)).kind == Slot::Kind::Taken);
    auto const slot = slot_for(store.storage, unix_start(5));
    CHECK(slot.kind == Slot::Kind::Stub);
    CHECK_EQ(slot.stub.start, unix_start(10));
}

void test_slot_nearest_stub()
{
    Test::TempStore store;
    file(store, 0, stub_fit());
    file(store, 40, stub_fit());
    CHECK_EQ(slot_for(store.storage, unix_start(25)).stub.start, unix_start(40));
    CHECK_EQ(slot_for(store.storage, unix_start(15)).stub.start, unix_start(0));
}

// A stub filed in the month before, as one filed in another time zone can
// be.
void test_slot_month_before()
{
    Test::TempStore store;
    auto const      other = DateTime::from_timestamp(static_cast<i32>(unix_start(0)));
    Month const     month { static_cast<size_t>(other.year), other.month };
    auto const      dir = store.storage.activity_dir / month.plus(-1).dir_name();
    fs::create_directories(dir);
    MUST(write_file_by_name((dir / std::format("{}.fit", unix_start(0))).string(), std::string_view { stub_fit() }));
    auto const slot = slot_for(store.storage, unix_start(20));
    CHECK(slot.kind == Slot::Kind::Stub);
    CHECK(slot.stub.sidecar == dir / std::format("{}.json", unix_start(0)));
}

// A recording in another's .recordings folder is a real activity.
void test_slot_recording()
{
    Test::TempStore store;
    file(store, 0, recorded_fit());
    auto const folder = recordings_dir(fit_at(store, 0));
    fs::create_directories(folder);
    MUST(write_file_by_name((folder / std::format("{}.fit", unix_start(20))).string(), std::string_view { recorded_fit(20) }));
    CHECK(slot_for(store.storage, unix_start(20)).kind == Slot::Kind::Taken);
}

void test_replace_same_start()
{
    Test::TempStore store;
    auto const      sidecar = R"({ "strava": { "id": 13742418979, "name": "Bermuda Railway Trail" }, "title": "Mine" })";
    file(store, 0, stub_fit(), sidecar);
    CHECK(replace_stub(store.storage, stored(store, 0), recorded_fit(), unix_start(0)).has_value());
    CHECK_EQ(contents(fit_at(store, 0)), recorded_fit());
    CHECK_EQ(contents(fit_at(store, 0, ".stub")), stub_fit());
    CHECK_EQ(contents(sidecar_at(store, 0)), sidecar);
}

void test_replace_other_start()
{
    Test::TempStore store;
    file(store, 0, stub_fit(), R"({ "start_time": 1, "title": "Mine" })");
    CHECK(replace_stub(store.storage, stored(store, 0), recorded_fit(5), unix_start(5)).has_value());
    CHECK_EQ(contents(fit_at(store, 5)), recorded_fit(5));
    CHECK_EQ(contents(fit_at(store, 5, ".stub")), stub_fit());
    CHECK(!fs::exists(fit_at(store, 0)));
    CHECK(!fs::exists(sidecar_at(store, 0)));
    auto const json = json_at(sidecar_at(store, 5));
    CHECK_EQ(json.try_get<i64>("start_time").value_or(0), unix_start(5));
    CHECK_EQ(json.try_get<std::string>("title").value_or(""), "Mine");
}

// A sidecar without a start time doesn't get one.
void test_replace_no_start_time()
{
    Test::TempStore store;
    file(store, 0, stub_fit(), R"({ "title": "Mine" })");
    CHECK(replace_stub(store.storage, stored(store, 0), recorded_fit(5), unix_start(5)).has_value());
    CHECK(!json_at(sidecar_at(store, 5)).has("start_time"));
}

void test_replace_with_incoming_sidecar()
{
    Test::TempStore store;
    file(store, 0, stub_fit(), R"({ "name": "Stub's", "title": "Mine" })");
    auto const incoming = JSONValue::deserialize(R"({ "name": "Wahoo's", "id": 42 })");
    CHECK(replace_stub(store.storage, stored(store, 0), recorded_fit(), unix_start(0), *incoming).has_value());
    auto const json = json_at(sidecar_at(store, 0));
    CHECK_EQ(json.try_get<std::string>("name").value_or(""), "Stub's");
    CHECK_EQ(json.try_get<std::string>("title").value_or(""), "Mine");
    CHECK_EQ(json.try_get<int>("id").value_or(0), 42);
}

void test_replace_without_stub_sidecar()
{
    Test::TempStore store;
    file(store, 0, stub_fit());
    auto const incoming = JSONValue::deserialize(R"({ "name": "Wahoo's" })");
    CHECK(replace_stub(store.storage, stored(store, 0), recorded_fit(3), unix_start(3), *incoming).has_value());
    CHECK_EQ(json_at(sidecar_at(store, 3)).try_get<std::string>("name").value_or(""), "Wahoo's");
    CHECK(!fs::exists(sidecar_at(store, 0)));
}

// A stub kept already, and an activity at the original's start, are
// never overwritten, and the store stays as it was.
void test_replace_never_overwrites()
{
    Test::TempStore store;
    file(store, 0, stub_fit(), R"({ "title": "Mine" })");
    MUST(write_file_by_name(fit_at(store, 0, ".stub").string(), std::string_view { "an older stub" }));
    CHECK(!replace_stub(store.storage, stored(store, 0), recorded_fit(), unix_start(0)).has_value());
    CHECK_EQ(contents(fit_at(store, 0)), stub_fit());
    CHECK_EQ(contents(fit_at(store, 0, ".stub")), "an older stub");

    Test::TempStore other;
    file(other, 0, stub_fit());
    file(other, 5, "someone else's");
    CHECK(!replace_stub(other.storage, stored(other, 0), recorded_fit(5), unix_start(5)).has_value());
    CHECK_EQ(contents(fit_at(other, 0)), stub_fit());
    CHECK_EQ(contents(fit_at(other, 5)), "someone else's");
    CHECK(!fs::exists(fit_at(other, 5, ".stub")));
}

// The kept stub isn't an activity: not listed, and not a start.
void test_kept_stub_not_listed()
{
    Test::TempStore store;
    file(store, 0, stub_fit());
    CHECK(replace_stub(store.storage, stored(store, 0), recorded_fit(), unix_start(0)).has_value());
    auto const  date = DateTime::from_timestamp(static_cast<i32>(unix_start(0)));
    Month const month { static_cast<size_t>(date.year), date.month };
    auto const  starts = store.storage.month_starts(month);
    CHECK_EQ(starts.value_or(std::vector<StoredStart> { }).size(), 1uz);
    Activities listed { store.storage, month };
    CHECK(listed.list().has_value());
    CHECK_EQ(listed.activities.size(), 1uz);
    CHECK(slot_for(store.storage, unix_start(0)).kind == Slot::Kind::Taken);
}

// Puts `contents` in the inbox as `name`.fit, with `sidecar` as its
// `name`.json, if any.
void to_inbox(Test::TempStore const &store, std::string_view name, std::string_view contents, std::string_view sidecar = "")
{
    MUST(write_file_by_name((store.storage.inbox_dir / std::format("{}.fit", name)).string(), contents));
    if (!sidecar.empty()) {
        MUST(write_file_by_name((store.storage.inbox_dir / std::format("{}.json", name)).string(), sidecar));
    }
}

struct Filed {
    std::vector<i64>                 filed { };
    std::vector<std::pair<i64, i64>> replaced { };
    u32                              count { 0 };
};

Filed process(Test::TempStore &store)
{
    Filed      ret;
    auto const count = store.storage.process_inbox(
        { },
        [&ret](i64 start) { ret.filed.push_back(start); },
        [&ret](i64 stub_start, i64 start) { ret.replaced.emplace_back(stub_start, start); });
    CHECK(count.has_value());
    ret.count = count.value_or(0);
    return ret;
}

void test_inbox_replaces_stub()
{
    Test::TempStore store;
    file(store, 0, stub_fit(), R"({ "title": "Mine" })");
    to_inbox(store, "wahoo-1", recorded_fit(4), R"({ "id": 1, "title": "Wahoo's" })");
    auto const filed = process(store);
    CHECK_EQ(filed.count, 1u);
    CHECK(filed.filed == std::vector { unix_start(4) });
    CHECK((filed.replaced == std::vector { std::pair { unix_start(0), unix_start(4) } }));
    CHECK_EQ(contents(fit_at(store, 4)), recorded_fit(4));
    CHECK_EQ(contents(fit_at(store, 4, ".stub")), stub_fit());
    auto const json = json_at(sidecar_at(store, 4));
    CHECK_EQ(json.try_get<std::string>("title").value_or(""), "Mine");
    CHECK_EQ(json.try_get<int>("id").value_or(0), 1);
    CHECK(store.inbox().empty());
}

// One that's only a summary too doesn't replace a stub: it's filed next
// to it, as before.
void test_inbox_stub_over_stub()
{
    Test::TempStore store;
    file(store, 0, stub_fit());
    to_inbox(store, "other", stub_fit(4));
    auto const filed = process(store);
    CHECK_EQ(filed.count, 1u);
    CHECK(filed.replaced.empty());
    CHECK_EQ(contents(fit_at(store, 0)), stub_fit());
    CHECK_EQ(contents(fit_at(store, 4)), stub_fit(4));
    CHECK(!fs::exists(fit_at(store, 0, ".stub")));

    // At the same start, it's a duplicate.
    to_inbox(store, "again", stub_fit());
    CHECK_EQ(process(store).count, 0u);
    CHECK(fs::exists(store.storage.inbox_dir / "duplicates" / "again.fit"));
}

void test_inbox_real_is_duplicate()
{
    Test::TempStore store;
    file(store, 0, recorded_fit());
    file(store, 10, stub_fit());
    to_inbox(store, "wahoo-1", recorded_fit());
    auto const filed = process(store);
    CHECK_EQ(filed.count, 0u);
    CHECK(fs::exists(store.storage.inbox_dir / "duplicates" / "wahoo-1.fit"));
    CHECK_EQ(contents(fit_at(store, 10)), stub_fit());
}

}

int main()
{
    test_is_stub();
    test_is_stub_file();
    test_slot_free();
    test_slot_taken();
    test_slot_unreadable();
    test_slot_stub();
    test_slot_real_and_stub();
    test_slot_nearest_stub();
    test_slot_month_before();
    test_slot_recording();
    test_replace_same_start();
    test_replace_other_start();
    test_replace_no_start_time();
    test_replace_with_incoming_sidecar();
    test_replace_without_stub_sidecar();
    test_replace_never_overwrites();
    test_kept_stub_not_listed();
    test_inbox_replaces_stub();
    test_inbox_stub_over_stub();
    test_inbox_real_is_duplicate();
    return ST::Test::test_result();
}
