/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Added by the Reviewer. Tests text_area_box(), the box an activity's
 * description is edited in: Cmd+Enter (Ctrl+Enter off macOS) saves, Enter
 * starts a new line, Esc on an unchanged text closes it without saving,
 * and the box keeps the keys while it's open.
 *
 * Without a window there are no fonts to measure text with, so the texts
 * here are empty or only line breaks, which the box doesn't measure. Esc
 * on a changed text opens query_box(), which does measure, and isn't
 * tested here.
 */

#include <memory>
#include <optional>
#include <string>

#include <TextAreaBox.h>

#include <TestApp.h>

using namespace ST;

namespace {

#ifdef __APPLE__
constexpr KeyboardModifier Save = KModSuper;
constexpr KeyboardModifier Word = KModAlt;
#else
constexpr KeyboardModifier Save = KModControl;
constexpr KeyboardModifier Word = KModControl;
#endif

struct Target {
    std::optional<std::string> submitted { };
    int                        submits { 0 };
};

using pTarget = std::shared_ptr<Target>;

auto const submit = [](pTarget const &target, std::string const &text) {
    target->submitted = text;
    ++target->submits;
};

pWidget open_box(Test::ScopedTestApp &scoped, pTarget const &target)
{
    text_area_box(target, "Description", submit, "");
    CHECK_EQ(scoped.app->modals.size(), 1uz);
    return scoped.app->modals.empty() ? nullptr : scoped.app->modals.back();
}

// Enter is a line break, and Cmd+Enter saves the text and closes the box.
void test_save_with_cmd_enter()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = open_box(scoped, target);
    if (!box) {
        return;
    }
    CHECK(box->process_key(KModNone, KEY_ENTER));
    CHECK(box->process_key(KModNone, KEY_KP_ENTER));
    CHECK_EQ(target->submits, 0);
    CHECK_EQ(scoped.app->modals.size(), 1uz);
    CHECK(box->process_key(Save, KEY_ENTER));
    CHECK_EQ(target->submits, 1);
    CHECK_EQ(target->submitted.value_or("<none>"), "\n\n");
    CHECK(scoped.app->modals.empty());
    // Closed, it takes no more keys.
    CHECK(!box->process_key(Save, KEY_ENTER));
    CHECK_EQ(target->submits, 1);
}

// Esc on the text as it was closes the box, without saving.
void test_escape_unchanged()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = open_box(scoped, target);
    if (!box) {
        return;
    }
    // Changed and changed back is unchanged.
    CHECK(box->process_key(KModNone, KEY_ENTER));
    CHECK(box->process_key(KModNone, KEY_BACKSPACE));
    CHECK(box->process_key(KModNone, KEY_ESCAPE));
    CHECK_EQ(target->submits, 0);
    CHECK(scoped.app->modals.empty());
}

// Alt+Backspace (Ctrl off macOS) deletes back over the line breaks; a
// control character that comes in as a character isn't typed.
void test_delete_word_and_control_characters()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = open_box(scoped, target);
    if (!box) {
        return;
    }
    CHECK(box->process_key(KModNone, KEY_ENTER));
    CHECK(box->process_key(KModNone, KEY_ENTER));
    CHECK(box->process_key(Word, KEY_BACKSPACE));
    box->character(0x7F);
    box->character(0x08);
    CHECK(box->process_key(Save, KEY_ENTER));
    CHECK_EQ(target->submitted.value_or("<none>"), "");
}

// While it's open, the box keeps the keys it doesn't use from the app,
// such as F2, which renames, and Cmd+Enter with Shift doesn't save.
void test_keeps_the_keys()
{
    Test::ScopedTestApp scoped;
    auto                target = std::make_shared<Target>();
    auto                box = open_box(scoped, target);
    if (!box) {
        return;
    }
    CHECK(box->process_key(KModNone, KEY_F2));
    CHECK(box->process_key(static_cast<KeyboardModifier>(Save | KModShift), KEY_ENTER));
    CHECK_EQ(target->submits, 0);
    CHECK_EQ(scoped.app->modals.size(), 1uz);
}

}

int main()
{
    test_save_with_cmd_enter();
    test_escape_unchanged();
    test_delete_word_and_control_characters();
    test_keeps_the_keys();
    return ST::Test::test_result();
}
