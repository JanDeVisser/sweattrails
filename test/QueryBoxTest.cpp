/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * query_box(): a question with answers such as Yes and No. Enter answers
 * with the selected one, the first unless another is asked for.
 */

#include <memory>
#include <optional>

#include <Modal.h>

#include <TestApp.h>

using namespace ST;

namespace {

struct Target {
    std::optional<QueryOption> answer { };
};

using pTarget = std::shared_ptr<Target>;

auto const answer = [](pTarget const &target, QueryOption option) {
    target->answer = option;
};

// Opens the box and presses Enter in it: what it answered.
std::optional<QueryOption> enter(QueryOption options, std::optional<QueryOption> initial)
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    query_box(target, "Delete it?", answer, options, initial);
    CHECK_EQ(scoped.app->modals.size(), 1uz);
    if (scoped.app->modals.empty()) {
        return { };
    }
    CHECK(scoped.app->modals.back()->process_key(KModNone, KEY_ENTER));
    CHECK(scoped.app->modals.empty());
    return target->answer;
}

void test_first_is_selected()
{
    CHECK(enter(QueryOptionYesNo, { }) == QueryOptionYes);
}

void test_initial_is_selected()
{
    CHECK(enter(QueryOptionYesNo, QueryOptionNo) == QueryOptionNo);
    CHECK(enter(QueryOptionYesNoCancel, QueryOptionCancel) == QueryOptionCancel);
}

// An answer that isn't offered leaves the first selected.
void test_initial_not_offered()
{
    CHECK(enter(QueryOptionYesNo, QueryOptionOK) == QueryOptionYes);
}

}

int main()
{
    test_first_is_selected();
    test_initial_is_selected();
    test_initial_not_offered();
    return ST::Test::test_result();
}
