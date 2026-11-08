/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the pop-up menu: where it opens, choosing with the keys, which
 * skip greyed-out items, and which item is under the mouse.
 */

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <PopupMenu.h>

#include <TestApp.h>

using namespace ST;

namespace {

// A menu of four items, the second greyed out, that records which one ran.
struct TestMenu {
    std::vector<std::string>   ran { };
    std::shared_ptr<PopupMenu> menu;

    explicit TestMenu(std::vector<bool> const &enabled = { true, false, true, true })
    {
        std::vector<MenuItem> items;
        for (auto ix = 0uz; ix < enabled.size(); ++ix) {
            auto title = std::format("Item {}", ix);
            items.push_back({ .title = title, .enabled = enabled[ix], .action = [this, title]() { ran.push_back(title); } });
        }
        // Not Widget::make(): initialize() measures text, which needs fonts.
        menu = std::make_shared<PopupMenu>(Vector2 { 100, 100 }, std::move(items));
        menu->select_next(1);
        menu->make_visible();
    }

    // Not to leave it open for the next test.
    ~TestMenu()
    {
        App::the()->pop_modal(menu);
    }

    void press(int key)
    {
        menu->process_key(KModNone, key);
    }
};

void test_opens_on_the_first_item_that_can_be_chosen()
{
    TestMenu t { { false, false, true } };
    CHECK(t.menu->selection == 2uz);
}

void test_up_and_down_skip_greyed_out_items()
{
    TestMenu t;
    CHECK(t.menu->selection == 0uz);
    t.press(KEY_DOWN);
    CHECK(t.menu->selection == 2uz);
    t.press(KEY_DOWN);
    t.press(KEY_DOWN); // At the end it stays
    CHECK(t.menu->selection == 3uz);
    t.press(KEY_UP);
    t.press(KEY_UP);
    CHECK(t.menu->selection == 0uz);
    t.press(KEY_UP);
    CHECK(t.menu->selection == 0uz);
}

void test_enter_runs_the_selected_item()
{
    TestMenu t;
    t.press(KEY_DOWN);
    t.press(KEY_ENTER);
    std::vector<std::string> const expected { "Item 2" };
    CHECK(t.ran == expected);
    CHECK(t.menu->status == ModalStatus::Submitted);
    CHECK(App::the()->modals.empty());
}

void test_esc_runs_nothing()
{
    TestMenu t;
    t.press(KEY_ESCAPE);
    CHECK(t.ran.empty());
    CHECK(t.menu->status == ModalStatus::Dismissed);
    CHECK(App::the()->modals.empty());
}

void test_nothing_to_choose()
{
    TestMenu t { { false, false } };
    CHECK(!t.menu->selection.has_value());
    t.press(KEY_DOWN);
    t.press(KEY_ENTER);
    CHECK(t.ran.empty());
    CHECK(t.menu->status == ModalStatus::Active);
}

void test_a_greyed_out_item_cant_be_chosen()
{
    TestMenu t;
    t.menu->choose(1);
    CHECK(t.ran.empty());
    CHECK(t.menu->status == ModalStatus::Active);
}

// The menu at (100, 100), 100 wide, rows 20 high under 6 pixels of
// padding.
void test_item_at()
{
    TestMenu t;
    t.menu->viewport = Rect<float> { 100.0f, 100.0f, 100.0f, 92.0f };
    t.menu->row_height = 20;
    CHECK(t.menu->item_at({ 150, 110 }) == 0uz);
    CHECK(!t.menu->item_at({ 150, 130 }).has_value()); // Greyed out
    CHECK(t.menu->item_at({ 150, 170 }) == 3uz);
    CHECK(!t.menu->item_at({ 150, 103 }).has_value()); // The padding
    CHECK(!t.menu->item_at({ 250, 110 }).has_value()); // Outside
}

// The menu at (100, 100), as in test_item_at().
void test_the_mouse()
{
    TestMenu t;
    t.menu->viewport = Rect<float> { 100.0f, 100.0f, 100.0f, 92.0f };
    t.menu->row_height = 20;
    t.menu->handle_mouse({ 150, 170 }, false, false);
    CHECK(t.menu->selection == 0uz); // Resting: Up and Down's stays
    t.menu->handle_mouse({ 150, 170 }, true, false);
    CHECK(t.menu->selection == 3uz);
    t.menu->handle_mouse({ 150, 130 }, true, true); // Greyed out
    CHECK(t.ran.empty());
    CHECK(t.menu->status == ModalStatus::Active);
    t.menu->handle_mouse({ 150, 150 }, false, true);
    std::vector<std::string> const expected { "Item 2" };
    CHECK(t.ran == expected);
    CHECK(t.menu->status == ModalStatus::Submitted);
}

void test_a_click_outside_closes_the_menu()
{
    TestMenu t;
    t.menu->viewport = Rect<float> { 100.0f, 100.0f, 100.0f, 92.0f };
    t.menu->row_height = 20;
    t.menu->handle_mouse({ 150, 103 }, false, true); // The padding
    CHECK(t.menu->status == ModalStatus::Active);
    t.menu->handle_mouse({ 300, 300 }, false, true);
    CHECK(t.ran.empty());
    CHECK(t.menu->status == ModalStatus::Dismissed);
}

void test_menu_position()
{
    Rectangle const screen { .x = 0, .y = 0, .width = 800, .height = 600 };
    Vector2 const   size { .x = 200, .y = 150 };
    auto const      check = [&](Vector2 point, Vector2 expected) {
        auto const at = menu_position(point, size, screen);
        CHECK_EQ(at.x, expected.x);
        CHECK_EQ(at.y, expected.y);
    };
    check({ 100, 100 }, { 100, 100 }); // Right of and below the click
    check({ 700, 100 }, { 500, 100 }); // Left of it, at the right edge
    check({ 100, 500 }, { 100, 350 }); // Above it, at the bottom
    check({ 700, 500 }, { 500, 350 });
    // A screen too small for it either way: its top left corner stays on.
    check({ 150, 100 }, { 150, 100 });
    auto const tiny = menu_position({ 50, 50 }, size, Rectangle { .x = 0, .y = 0, .width = 100, .height = 100 });
    CHECK_EQ(tiny.x, 0.0f);
    CHECK_EQ(tiny.y, 0.0f);
}

// Reviewer: the menu is closed when the action runs, so that an action
// that opens a modal of its own, such as a message, has it on top, and
// it stays open.
void test_the_action_runs_after_the_menu_closed()
{
    std::vector<MenuItem>      items;
    auto                       menu_open = true;
    auto const                 other = std::make_shared<Modal>("Another");
    std::shared_ptr<PopupMenu> menu;
    items.push_back({ .title = "Item", .action = [&]() {
                         menu_open = std::ranges::find(App::the()->modals, std::static_pointer_cast<Widget>(menu)) != App::the()->modals.end();
                         other->make_visible();
                     } });
    menu = std::make_shared<PopupMenu>(Vector2 { 100, 100 }, std::move(items));
    menu->select_next(1);
    menu->make_visible();
    menu->process_key(KModNone, KEY_ENTER);
    CHECK(!menu_open);
    CHECK(App::the()->modals.size() == 1 && App::the()->modals.back() == other);
    App::the()->pop_modal(other);
}

// Reviewer: with a modifier, the keys aren't the menu's: Ctrl+Down
// doesn't move, and Shift+Enter doesn't choose.
void test_keys_with_a_modifier()
{
    TestMenu t;
    CHECK(!t.menu->process_key(KModControl, KEY_DOWN));
    CHECK(t.menu->selection == 0uz);
    CHECK(!t.menu->process_key(KModShift, KEY_ENTER));
    CHECK(t.ran.empty());
    CHECK(t.menu->status == ModalStatus::Active);
}

// Reviewer: Esc closes a menu with nothing to choose, and an item without
// an action closes it too.
void test_closing_without_actions()
{
    TestMenu nothing { { false, false } };
    nothing.press(KEY_ESCAPE);
    CHECK(nothing.menu->status == ModalStatus::Dismissed);
    CHECK(App::the()->modals.empty());

    auto menu = std::make_shared<PopupMenu>(Vector2 { 100, 100 }, std::vector<MenuItem> { { .title = "No action" } });
    menu->select_next(1);
    menu->make_visible();
    menu->process_key(KModNone, KEY_ENTER);
    CHECK(menu->status == ModalStatus::Submitted);
    CHECK(App::the()->modals.empty());
}

// Reviewer: a frame without a click or a key leaves the menu open, and
// runs nothing.
void test_a_frame_without_input()
{
    TestMenu t;
    App::the()->process_input();
    CHECK(t.menu->status == ModalStatus::Active);
    CHECK(App::the()->modals.size() == 1);
    CHECK(t.ran.empty());
}

// Reviewer: the edges of the rows: the last row's bottom, and the padding
// under it.
void test_item_at_the_bottom()
{
    TestMenu t;
    t.menu->viewport = Rect<float> { 100.0f, 100.0f, 100.0f, 92.0f };
    t.menu->row_height = 20;
    CHECK(t.menu->item_at({ 150, 185.9f }) == 3uz);
    CHECK(!t.menu->item_at({ 150, 187 }).has_value());
    CHECK(t.menu->item_at({ 100, 110 }) == 0uz);
    CHECK(!t.menu->item_at({ 200, 110 }).has_value());
}

// Reviewer: a menu that just fits stays right of and below the click.
void test_menu_position_just_fitting()
{
    Rectangle const screen { .x = 0, .y = 0, .width = 800, .height = 600 };
    auto const      at = menu_position({ 600, 450 }, { 200, 150 }, screen);
    CHECK_EQ(at.x, 600.0f);
    CHECK_EQ(at.y, 450.0f);
}

}

int main()
{
    Test::ScopedTestApp app;
    test_opens_on_the_first_item_that_can_be_chosen();
    test_up_and_down_skip_greyed_out_items();
    test_enter_runs_the_selected_item();
    test_esc_runs_nothing();
    test_nothing_to_choose();
    test_a_greyed_out_item_cant_be_chosen();
    test_item_at();
    test_menu_position();
    test_the_mouse();
    test_a_click_outside_closes_the_menu();
    test_the_action_runs_after_the_menu_closed();
    test_keys_with_a_modifier();
    test_closing_without_actions();
    test_a_frame_without_input();
    test_item_at_the_bottom();
    test_menu_position_just_fitting();
    return Test::test_result();
}
