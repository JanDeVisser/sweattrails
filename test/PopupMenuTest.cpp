/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests the pop-up menu: where it opens, choosing with the keys, which
 * skip greyed-out items, and which item is under the mouse.
 */

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
    return Test::test_result();
}
