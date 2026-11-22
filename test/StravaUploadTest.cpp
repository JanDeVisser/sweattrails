/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests uploading to Strava, against a FakeStrava: see
 * specs/66-strava-upload.md.
 */

#include <chrono>
#include <format>
#include <string>

#include <FakeStrava.h>
#include <FileIO.h>
#include <JSON.h>

#include <storage/Activity.h>
#include <storage/StravaUpload.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;

namespace {

constexpr u64 OldId = 21088108244;
constexpr u64 NewId = 21300000001;

// The answer to the upload, a 201, or to a poll, a 200.
HttpResponse processing(long status = 201)
{
    return HttpResponse { .status = status, .body = R"({"id":42,"error":null,"status":"Your activity is still being processed.","activity_id":null})" };
}

HttpResponse ready(u64 activity_id = NewId)
{
    return HttpResponse { .status = 200, .body = std::format(R"({{"id":42,"error":null,"status":"Your activity is ready.","activity_id":{}}})", activity_id) };
}

HttpResponse failed_with(std::string_view error)
{
    auto json = JSONValue::object();
    json.set("id", JSONValue { 42 });
    json.set("error", JSONValue { std::string { error } });
    return HttpResponse { .status = 200, .body = json.serialize() };
}

// An activity of two recordings, the one on display with its gaps filled,
// in a folder of its own.
struct Upload {
    TempDir       dir;
    FakeStrava    fake { };
    Strava        strava;
    UploadRequest request;
    fs::path      main_sidecar;
    fs::path      unrelated_sidecar;
    int           waits { 0 };

    explicit Upload(std::string_view name)
        : dir(name)
        , strava(logged_in(fake, dir))
        , request {
            .fit = dir.path / "1793275500.fit",
            .sidecar = dir.path / "1793275500.json",
            .name = "Morning ride",
            .description = "Windy",
        }
        , main_sidecar(dir.path / "1793275400.json")
        , unrelated_sidecar(dir.path / "1793275600.json")
    {
        MUST(write_file_by_name<char>(request.fit.string(), std::string { "\x0e\x10\0\x01FIT", 7 }));
        MUST(write_file_by_name<char>((dir.path / "1793275500.fit.original").string(), std::string { "original" }));
        request.others = { main_sidecar, unrelated_sidecar };
    }

    void on_strava(fs::path const &sidecar, u64 id)
    {
        MUST(save_sidecar_title(sidecar, "Mine"));
        MUST(save_sidecar_strava(sidecar, { .id = id, .name = "Morning ride", .description = "Windy" }));
    }

    StravaResult<UploadOutcome> run(int polls = 60)
    {
        return upload_to_strava(strava, request, UploadPacing { .polls = polls, .wait = [this](std::chrono::seconds pause) {
                                                                   CHECK_EQ(pause, std::chrono::seconds { 1 });
                                                                   ++waits;
                                                               } });
    }

    [[nodiscard]] size_t uploads() const
    {
        return std::ranges::count_if(fake.requests, [](HttpRequest const &request) { return request.method == "POST"; });
    }
};

std::optional<StravaTitle> strava_of(fs::path const &sidecar)
{
    return sidecar_titles(sidecar).strava;
}

void test_upload_new()
{
    Upload t { "upload-new" };
    t.fake.answer("/api/v3/uploads", processing());
    t.fake.answer("/api/v3/uploads/42", processing(200));
    t.fake.answer("/api/v3/uploads/42", ready());
    auto const outcome = t.run();
    CHECK(outcome.has_value());
    if (outcome) {
        CHECK(outcome->kind == UploadOutcome::Kind::Uploaded);
        CHECK_EQ(outcome->activity_id, NewId);
        CHECK_EQ(outcome->message, "");
    }
    // Nothing to check first: straight to the upload, then two polls.
    CHECK_EQ(t.fake.requests.size(), 3uz);
    CHECK_EQ(t.uploads(), 1uz);
    CHECK_EQ(t.waits, 2);
    // The filled file, not its original.
    CHECK(t.fake.requests.front().body.contains(std::string { "filename=\"1793275500.fit\"" }));
    CHECK(t.fake.requests.front().body.contains(std::string { "\x0e\x10\0\x01FIT", 7 }));
    CHECK((strava_of(t.request.sidecar) == StravaTitle { .id = NewId, .name = "Morning ride", .description = "Windy" }));
}

void test_upload_keeps_the_sidecar()
{
    Upload t { "upload-keeps" };
    MUST(save_sidecar_title(t.request.sidecar, "My ride"));
    MUST(save_sidecar_laps(t.request.sidecar, std::vector<i64> { 1793275800 }));
    t.fake.answer("/api/v3/uploads", processing());
    t.fake.answer("/api/v3/uploads/42", ready());
    CHECK(t.run().has_value());
    auto const titles = sidecar_titles(t.request.sidecar);
    CHECK_EQ(titles.own.value_or(""), "My ride");
    CHECK_EQ(sidecar_laps(t.request.sidecar).value_or(std::vector<i64> { }).size(), 1uz);
    CHECK(titles.strava.has_value() && titles.strava->id == NewId);
}

void test_still_on_strava()
{
    Upload t { "upload-still-there" };
    t.on_strava(t.request.sidecar, OldId);
    t.fake.answer(std::format("/api/v3/activities/{}", OldId), HttpResponse { .status = 200, .body = std::format(R"({{"id":{}}})", OldId) });
    auto const outcome = t.run();
    CHECK(outcome.has_value());
    if (outcome) {
        CHECK(outcome->kind == UploadOutcome::Kind::StillOnStrava);
        CHECK_EQ(outcome->activity_id, OldId);
    }
    CHECK_EQ(t.uploads(), 0uz);
    CHECK(strava_of(t.request.sidecar).value_or(StravaTitle { }).id == OldId);
}

// Deleted on strava.com: Strava answers 404, and the other recordings
// that had it get the new one, but not one that had another.
void test_deleted_on_strava()
{
    Upload t { "upload-deleted" };
    t.on_strava(t.request.sidecar, OldId);
    t.on_strava(t.main_sidecar, OldId);
    t.on_strava(t.unrelated_sidecar, 999);
    t.fake.answer(std::format("/api/v3/activities/{}", OldId), HttpResponse { .status = 404, .body = R"({"message":"Record Not Found","errors":[]})" });
    t.fake.answer("/api/v3/uploads", processing());
    t.fake.answer("/api/v3/uploads/42", ready());
    auto const outcome = t.run();
    CHECK(outcome.has_value() && outcome->kind == UploadOutcome::Kind::Uploaded);
    CHECK_EQ(t.uploads(), 1uz);
    StravaTitle const uploaded { .id = NewId, .name = "Morning ride", .description = "Windy" };
    CHECK(strava_of(t.request.sidecar) == uploaded);
    CHECK(strava_of(t.main_sidecar) == uploaded);
    CHECK_EQ(sidecar_titles(t.main_sidecar).own.value_or(""), "Mine");
    CHECK(strava_of(t.unrelated_sidecar).value_or(StravaTitle { }).id == 999u);
}

// Without a description, none is sent, and none is saved: the background
// fetches what Strava has.
void test_without_description()
{
    Upload t { "upload-no-description" };
    t.request.description.reset();
    t.fake.answer("/api/v3/uploads", processing());
    t.fake.answer("/api/v3/uploads/42", ready());
    CHECK(t.run().has_value());
    CHECK(!t.fake.requests.front().body.contains(std::string { "name=\"description\"" }));
    CHECK((strava_of(t.request.sidecar) == StravaTitle { .id = NewId, .name = "Morning ride" }));
}

void test_duplicate_when_polling()
{
    Upload t { "upload-duplicate" };
    t.fake.answer("/api/v3/uploads", processing());
    t.fake.answer("/api/v3/uploads/42", failed_with("1793275500.fit duplicate of activity 21234316"));
    auto const outcome = t.run();
    CHECK(outcome.has_value());
    if (outcome) {
        CHECK(outcome->kind == UploadOutcome::Kind::Duplicate);
        CHECK_EQ(outcome->activity_id, 21234316u);
    }
    CHECK(!strava_of(t.request.sidecar).has_value());
}

void test_duplicate_at_once()
{
    Upload t { "upload-duplicate-400" };
    t.fake.answer("/api/v3/uploads", HttpResponse { .status = 400, .body = R"({"id":42,"error":"1793275500.fit duplicate of activity 21234316","activity_id":null})" });
    auto const outcome = t.run();
    CHECK(outcome.has_value());
    if (outcome) {
        CHECK(outcome->kind == UploadOutcome::Kind::Duplicate);
        CHECK_EQ(outcome->activity_id, 21234316u);
    }
    CHECK_EQ(t.fake.requests.size(), 1uz);
    CHECK_EQ(t.waits, 0);
}

void test_rejected()
{
    Upload t { "upload-rejected" };
    t.fake.answer("/api/v3/uploads", processing());
    t.fake.answer("/api/v3/uploads/42", failed_with("Improperly formatted data."));
    auto const outcome = t.run();
    CHECK(outcome.has_value());
    if (outcome) {
        CHECK(outcome->kind == UploadOutcome::Kind::Rejected);
        CHECK_EQ(outcome->message, "Improperly formatted data.");
    }
    CHECK(!strava_of(t.request.sidecar).has_value());
}

void test_still_processing()
{
    Upload t { "upload-slow" };
    t.on_strava(t.request.sidecar, OldId);
    t.fake.answer(std::format("/api/v3/activities/{}", OldId), HttpResponse { .status = 404, .body = R"({"message":"Record Not Found","errors":[]})" });
    t.fake.answer("/api/v3/uploads", processing());
    for (auto ix = 0; ix < 3; ++ix) {
        t.fake.answer("/api/v3/uploads/42", processing(200));
    }
    auto const outcome = t.run(3);
    CHECK(outcome.has_value() && outcome->kind == UploadOutcome::Kind::StillProcessing);
    CHECK_EQ(t.waits, 3);
    // The check, the upload, and three polls.
    CHECK_EQ(t.fake.requests.size(), 5uz);
    CHECK(strava_of(t.request.sidecar).value_or(StravaTitle { }).id == OldId);
}

void test_check_fails()
{
    Upload t { "upload-check-fails" };
    t.on_strava(t.request.sidecar, OldId);
    t.strava.retries = 0;
    t.fake.answer(std::format("/api/v3/activities/{}", OldId), HttpResponse { .status = 500, .body = "" });
    auto const failed = t.run();
    CHECK(!failed.has_value() && failed.error().status == 500);
    t.fake.answer(std::format("/api/v3/activities/{}", OldId),
        HttpResponse { .status = 429, .headers = { { "X-ReadRateLimit-Limit", "100,1000" }, { "X-ReadRateLimit-Usage", "101,400" } } });
    auto const limited = t.run();
    CHECK(!limited.has_value() && limited.error().kind == StravaError::Kind::RateLimited);
    CHECK_EQ(t.uploads(), 0uz);
    CHECK(strava_of(t.request.sidecar).value_or(StravaTitle { }).id == OldId);
}

void test_no_write_access()
{
    Upload t { "upload-forbidden" };
    t.fake.answer("/api/v3/uploads", HttpResponse { .status = 401, .body = R"({"message":"Authorization Error"})" });
    auto const outcome = t.run();
    CHECK(!outcome.has_value() && outcome.error().kind == StravaError::Kind::Forbidden);
    CHECK(!strava_of(t.request.sidecar).has_value());
}

// A login that can post but not read the upload's progress, or the
// reverse, is a login without write access too: the spec says a 401 or 403
// to the upload or its poll is Forbidden.
void test_no_write_access_when_polling()
{
    for (auto const status : { 401L, 403L }) {
        Upload t { std::format("upload-forbidden-poll-{}", status) };
        t.fake.answer("/api/v3/uploads", processing());
        t.fake.answer("/api/v3/uploads/42", HttpResponse { .status = status, .body = R"({"message":"Authorization Error"})" });
        auto const outcome = t.run();
        CHECK(!outcome.has_value() && outcome.error().kind == StravaError::Kind::Forbidden);
        CHECK(!strava_of(t.request.sidecar).has_value());
    }
}

void test_unreadable_file()
{
    Upload t { "upload-unreadable" };
    t.request.fit = t.dir.path / "missing.fit";
    auto const outcome = t.run();
    CHECK(!outcome.has_value());
    CHECK_EQ(t.uploads(), 0uz);
    CHECK(!strava_of(t.request.sidecar).has_value());
}

// A POST Strava answered with a server error isn't asked again: it may have
// the file already.
void test_upload_is_not_retried()
{
    Upload t { "upload-not-retried" };
    t.fake.answer("/api/v3/uploads", HttpResponse { .status = 500, .body = "" });
    auto const outcome = t.run();
    CHECK(!outcome.has_value() && outcome.error().kind == StravaError::Kind::Failed);
    CHECK_EQ(t.uploads(), 1uz);
    CHECK(!strava_of(t.request.sidecar).has_value());
}

void test_not_logged_in()
{
    Upload t { "upload-not-logged-in" };
    t.strava.tokens.reset();
    auto const outcome = t.run();
    CHECK(!outcome.has_value() && outcome.error().kind == StravaError::Kind::NotLoggedIn);
    CHECK(t.fake.requests.empty());
}

void test_report()
{
    auto const report = [](UploadOutcome outcome) { return upload_report(outcome); };
    CHECK_EQ(report({ .kind = UploadOutcome::Kind::Uploaded, .activity_id = NewId }).text, "Uploaded to Strava.");
    auto const still = report({ .kind = UploadOutcome::Kind::StillOnStrava, .activity_id = OldId });
    CHECK_EQ(still.text, "Strava still has this activity. Open it to delete it?");
    CHECK_EQ(still.open.value_or(0), OldId);
    auto const duplicate = report({ .kind = UploadOutcome::Kind::Duplicate, .activity_id = 21234316 });
    CHECK_EQ(duplicate.text, "Strava already has this activity, as activity 21234316. Open it to delete it?");
    CHECK_EQ(duplicate.open.value_or(0), 21234316u);
    CHECK_EQ(report({ .kind = UploadOutcome::Kind::Rejected, .message = "Improperly formatted data." }).text, "Strava didn't take the file: Improperly formatted data.");
    CHECK(!report({ .kind = UploadOutcome::Kind::StillProcessing }).open.has_value());
    auto const forbidden = upload_report(std::unexpected(StravaError { .kind = StravaError::Kind::Forbidden, .message = "No write access." }));
    CHECK(forbidden.log_in);
    CHECK(forbidden.text.ends_with("Log in to Strava again to upload?"));
    auto const not_logged_in = upload_report(std::unexpected(StravaError { .kind = StravaError::Kind::NotLoggedIn, .message = "Not logged in." }));
    CHECK(not_logged_in.log_in);
    auto const failed = upload_report(std::unexpected(StravaError { .message = "Could not reach Strava: Timeout was reached" }));
    CHECK_EQ(failed.text, "Couldn't upload to Strava: Could not reach Strava: Timeout was reached");
    CHECK(!failed.log_in && !failed.open);
}

}

int main()
{
    test_upload_new();
    test_upload_keeps_the_sidecar();
    test_still_on_strava();
    test_deleted_on_strava();
    test_without_description();
    test_duplicate_when_polling();
    test_duplicate_at_once();
    test_rejected();
    test_still_processing();
    test_check_fails();
    test_no_write_access();
    test_no_write_access_when_polling();
    test_unreadable_file();
    test_upload_is_not_retried();
    test_not_logged_in();
    test_report();
    return ST::Test::test_result();
}
