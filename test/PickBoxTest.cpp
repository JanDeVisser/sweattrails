/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * PickBox, list_box()'s box: picking one of a list of entries.
 */

#include <memory>
#include <optional>
#include <string>

#include <Modal.h>

#include <TestApp.h>

using namespace ST;

namespace {

struct Target {
    std::optional<int> picked { };
};

using pTarget = std::shared_ptr<Target>;

auto const submit = [](pTarget const &target, int payload) {
    target->picked = payload;
};

using Box = PickBox<Target, int, decltype(submit)>;

// A box as list_box() opens it, but without measuring anything.
std::shared_ptr<Box> make_box(pTarget const &target)
{
    auto box = std::make_shared<Box>(target, "Layout", Box::Entries { { "running", 3 }, { "alps", 1 }, { "cycling-basic", 2 } }, submit);
    box->sort();
    box->filter();
    box->selection = 0;
    return box;
}

void test_submit_picks_the_selected_payload()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = make_box(target);
    // Sorted by text.
    CHECK_EQ(box->selected().text, "alps");
    box->submit();
    CHECK_EQ(target->picked.value_or(0), 1);
}

void test_select()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = make_box(target);
    box->select("running");
    CHECK_EQ(box->selected().text, "running");
    box->submit();
    CHECK_EQ(target->picked.value_or(0), 3);
    // An entry that isn't there leaves the selection.
    box->select("nope");
    CHECK_EQ(box->selected().text, "running");
}

}

int main()
{
    test_submit_picks_the_selected_payload();
    test_select();
    return ST::Test::test_result();
}
