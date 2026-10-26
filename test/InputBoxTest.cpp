/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests input_box(): the box the activity's name is edited in. It opens as
 * a modal with the text it's given, hands the text, as UTF-8, to its submit
 * function on Enter, and calls its dismiss function, not the submit one, on
 * Esc. Keys it doesn't take, such as F2, are left to the app.
 */

#include <memory>
#include <optional>
#include <string>

#include <Modal.h>

#include <TestApp.h>

using namespace ST;

namespace {

// What the box's functions were called with.
struct Target {
    std::optional<std::string> submitted { };
    int                        submits { 0 };
    int                        dismissals { 0 };
};

using pTarget = std::shared_ptr<Target>;

auto const submit = [](pTarget const &target, std::string const &text) {
    target->submitted = text;
    ++target->submits;
};

// A function rather than a lambda: InputBox's constructor defaults its
// dismiss function to nullptr, which a lambda can't be made from, so a
// lambda doesn't compile (see the spec's review notes).
void dismiss(pTarget const &target)
{
    ++target->dismissals;
}

pWidget open_box(Test::ScopedTestApp &scoped, pTarget const &target, std::string_view def)
{
    input_box(target, "Name", submit, def, &dismiss);
    CHECK_EQ(scoped.app->modals.size(), 1uz);
    return scoped.app->modals.empty() ? nullptr : scoped.app->modals.back();
}

void test_enter_submits_the_text()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = open_box(scoped, target, "Café");
    if (!box) {
        return;
    }
    // The cursor is at the end of the text it was given.
    box->character(L' ');
    for (auto ch : std::wstring_view { L"Rückweg" }) {
        box->character(static_cast<int>(ch));
    }
    // A tab isn't typed.
    box->character('\t');
    CHECK(box->process_key(KModNone, KEY_ENTER));
    CHECK_EQ(target->submits, 1);
    CHECK_EQ(target->submitted.value_or("<none>"), "Café Rückweg");
    CHECK_EQ(target->dismissals, 0);
    CHECK(scoped.app->modals.empty());
}

void test_editing_keys()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = open_box(scoped, target, "Ålesund");
    if (!box) {
        return;
    }
    CHECK(box->process_key(KModNone, KEY_HOME));
    CHECK(box->process_key(KModNone, KEY_DELETE));
    CHECK(box->process_key(KModNone, KEY_END));
    CHECK(box->process_key(KModNone, KEY_BACKSPACE));
    CHECK(box->process_key(KModNone, KEY_KP_ENTER));
    CHECK_EQ(target->submitted.value_or("<none>"), "lesun");
}

void test_escape_dismisses()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = open_box(scoped, target, "Morning Ride");
    if (!box) {
        return;
    }
    box->character('x');
    CHECK(box->process_key(KModNone, KEY_ESCAPE));
    CHECK_EQ(target->dismissals, 1);
    CHECK_EQ(target->submits, 0);
    CHECK(scoped.app->modals.empty());
}

// Without a dismiss function, as before the box was fixed: Esc closes it
// and nothing is submitted.
void test_escape_without_a_dismiss_function()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    input_box(target, "Name", submit, "Morning Ride");
    CHECK_EQ(scoped.app->modals.size(), 1uz);
    if (scoped.app->modals.empty()) {
        return;
    }
    CHECK(scoped.app->modals.back()->process_key(KModNone, KEY_ESCAPE));
    CHECK_EQ(target->submits, 0);
    CHECK(scoped.app->modals.empty());
}

// F2, which opened the box, and keys with modifiers the box doesn't know,
// go on to the app, and leave the box open.
void test_keys_it_leaves()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = open_box(scoped, target, "Ride");
    if (!box) {
        return;
    }
    CHECK(!box->process_key(KModNone, KEY_F2));
    CHECK(!box->process_key(KModShift, KEY_ENTER));
    CHECK(!box->process_key(KModControl, KEY_ESCAPE));
    CHECK_EQ(scoped.app->modals.size(), 1uz);
    CHECK_EQ(target->submits, 0);
    CHECK_EQ(target->dismissals, 0);
}

}

int main()
{
    test_enter_submits_the_text();
    test_editing_keys();
    test_escape_dismisses();
    test_escape_without_a_dismiss_function();
    test_keys_it_leaves();
    return ST::Test::test_result();
}
