/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests that ActivitySelector, the month's activity list (Ctrl-M), submits
 * the activity that is selected, also while searching. The selector reads
 * the month from storage when it's made; the storage here points at a
 * directory that doesn't exist, unique to this run, so the month is empty,
 * and the test adds the entries itself.
 */

#include <filesystem>
#include <format>
#include <optional>
#include <string>

#include <unistd.h>

#include <Modal.h>

#include <storage/Storage.h>
#include <widget/ActivitySelector.h>

#include <TestApp.h>

using namespace ST;

namespace {

struct Fixture {
    Storage                           storage { };
    Month                             month { 2026, DateTime::Month::April };
    std::optional<ActivityID>         submitted { };
    std::shared_ptr<ActivitySelector> selector { };

    explicit Fixture(std::vector<std::string> const &names)
    {
        storage.activity_dir = std::filesystem::temp_directory_path() / std::format("sweattrails-test-{}-no-such-dir", getpid());
        CHECK(!std::filesystem::exists(storage.activity_dir));
        selector = std::make_shared<ActivitySelector>(
            [this](std::shared_ptr<ActivitySelector> const &, ActivityID const &id) {
                submitted = id;
            },
            month, storage);
        for (auto const &name : names) {
            selector->entries.emplace_back(name, ActivityID { month, name, FileTypes::one(ActivityFile::Fit) });
        }
        selector->scroll.lines = 20;
        selector->status = ModalStatus::Active;
    }

    void type(std::string_view const &text)
    {
        for (auto ch : text) {
            selector->character(ch);
        }
    }

    void key(int key)
    {
        selector->status = ModalStatus::Active;
        selector->process_key(KModNone, key);
    }

    [[nodiscard]] std::string submitted_name() const
    {
        return submitted ? submitted->name : std::string { "<none>" };
    }
};

void test_submit_without_search()
{
    Fixture f { { "a run", "b ride", "c run", "d swim" } };
    CHECK(f.selector->entries.size() == 4);
    f.key(KEY_DOWN);
    f.key(KEY_ENTER);
    CHECK_EQ(f.submitted_name(), "b ride");
}

void test_submit_search_match()
{
    Fixture f { { "a run", "b ride", "c run", "d swim" } };
    f.type("swim");
    f.key(KEY_ENTER);
    CHECK_EQ(f.submitted_name(), "d swim");
}

void test_submit_second_search_match()
{
    Fixture f { { "a run", "b ride", "c run", "d swim" } };
    f.type("run");
    f.key(KEY_DOWN);
    f.key(KEY_ENTER);
    CHECK_EQ(f.submitted_name(), "c run");
}

void test_no_match_submits_nothing()
{
    Fixture f { { "a run", "b ride" } };
    f.type("swim");
    f.key(KEY_ENTER);
    CHECK(!f.submitted);
}

}

int main()
{
    Test::ScopedTestApp app;
    test_submit_without_search();
    test_submit_search_match();
    test_submit_second_search_match();
    test_no_match_submits_nothing();
    return Test::test_result();
}
