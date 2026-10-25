/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests ListBox's selection, scrolling and search, by feeding it keys and
 * characters. resize() and draw() need raylib, so the tests set
 * `scroll.lines`, the number of entries on a page, themselves, and where
 * draw() would have put the rows and the scrollbar.
 */

#include <format>
#include <string>
#include <vector>

#include <Modal.h>

#include <TestApp.h>

using namespace ST;

namespace {

struct TestList : ListBox<std::string> {
    std::string submitted { };

    TestList()
        : ListBox("test")
    {
    }

    // So this tests ListBox's selected(), not the submit() of a real ListBox
    // such as ActivitySelector.
    void submit() override
    {
        submitted = selected().payload;
    }

    // Does what refresh() does, except for resize().
    void setup(std::vector<std::string> const &items, int page = 20)
    {
        entries.clear();
        for (auto const &item : items) {
            entries.emplace_back(item, item);
        }
        sort();
        search = { };
        filter();
        selection = 0;
        scroll.top_line = 0;
        scroll.lines = page;
        submitted = { };
        status = ModalStatus::Active;
    }

    void type(std::string_view const &text)
    {
        for (auto ch : text) {
            character(ch);
        }
    }

    // process_key() pops the modal and leaves the status Submitted or
    // Dismissed after Enter or Escape; make it Active again for every key.
    void key(int key, int times = 1)
    {
        for (auto ix = 0; ix < times; ++ix) {
            status = ModalStatus::Active;
            process_key(KModNone, key);
        }
    }

    // The selection is an entry on display, and on screen.
    [[nodiscard]] bool selection_ok() const
    {
        if (shown_count() == 0) {
            return selection == 0 && scroll.top_line == 0;
        }
        return selection >= 0 && selection < shown_count() && selection >= scroll.top_line && selection < scroll.top_line + scroll.lines;
    }
};

std::vector<std::string> numbered(int count)
{
    std::vector<std::string> ret;
    for (auto ix = 0; ix < count; ++ix) {
        ret.push_back(std::format("e{:02}", ix));
    }
    return ret;
}

void test_submit_search_match(TestList &list)
{
    list.setup({ "a run", "b ride", "c run", "d swim" });
    list.type("swim");
    list.key(KEY_ENTER);
    CHECK_EQ(list.submitted, "d swim");
}

void test_page_down_short_list(TestList &list)
{
    list.setup({ "a", "b", "c", "d", "e" });
    list.key(KEY_PAGE_DOWN);
    CHECK_EQ(list.selection, 4);
    CHECK(list.selection_ok());
    list.key(KEY_UP);
    CHECK_EQ(list.selection, 3);
    list.key(KEY_ENTER);
    CHECK_EQ(list.submitted, "d");
}

void test_no_matches(TestList &list)
{
    list.setup({ "a", "b" });
    list.type("zz");
    list.key(KEY_DOWN, 3);
    list.key(KEY_PAGE_DOWN);
    CHECK(list.selection_ok());
    list.key(KEY_ENTER);
    CHECK(list.submitted.empty());
}

void test_search_after_scrolling(TestList &list)
{
    list.setup(numbered(60));
    list.key(KEY_DOWN, 45);
    CHECK_EQ(list.selection, 45);
    CHECK(list.selection_ok());
    list.type("e5");
    CHECK_EQ(list.shown_count(), 10);
    CHECK_EQ(list.scroll.top_line, 0);
    CHECK(list.selection_ok());
    list.key(KEY_DOWN, 3);
    CHECK_EQ(list.selected().payload, "e53");
    list.key(KEY_BACKSPACE, 2);
    CHECK_EQ(list.shown_count(), 60);
    CHECK_EQ(list.selection, 0);
    CHECK(list.selection_ok());
    list.key(KEY_BACKSPACE);
    CHECK(list.search.empty());
}

void test_paging(TestList &list)
{
    list.setup(numbered(60));
    list.key(KEY_PAGE_DOWN, 2);
    CHECK_EQ(list.selection, 40);
    CHECK(list.selection_ok());
    list.key(KEY_PAGE_UP, 2);
    CHECK_EQ(list.selection, 0);
    CHECK_EQ(list.scroll.top_line, 0);
    list.key(KEY_PAGE_DOWN, 5);
    CHECK_EQ(list.selection, 59);
    CHECK(list.selection_ok());
    list.key(KEY_DOWN);
    CHECK_EQ(list.selection, 59);
}

// Where draw() would put 20 rows, and the scrollbar next to them.
void place_rows(TestList &list)
{
    list.rows_area = Rectangle { .x = 0, .y = 0, .width = 190, .height = 200 };
    list.track = Rectangle { .x = 190, .y = 0, .width = 10, .height = 200 };
}

void test_wheel_keeps_selection(TestList &list)
{
    list.setup(numbered(60));
    place_rows(list);
    list.key(KEY_DOWN, 3);
    list.handle_scrolling(ScrollInput { .mouse = { .x = 50, .y = 50 }, .wheel = -5 });
    CHECK_EQ(list.scroll.top_line, 5);
    CHECK_EQ(list.selection, 3);
    // The keys bring the selection back on screen.
    list.key(KEY_DOWN);
    CHECK_EQ(list.selection, 4);
    CHECK(list.selection_ok());
}

void test_scrollbar_pages(TestList &list)
{
    list.setup(numbered(60));
    place_rows(list);
    // Below the thumb, which is a third of the track at the top.
    list.handle_scrolling(ScrollInput { .mouse = { .x = 195, .y = 150 }, .pressed = true, .down = true });
    CHECK_EQ(list.scroll.top_line, 20);
    CHECK_EQ(list.selection, 0);
}

void test_empty_list(TestList &list)
{
    list.setup({ });
    for (auto key : { KEY_DOWN, KEY_UP, KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_BACKSPACE, KEY_ENTER }) {
        list.key(key);
        CHECK(list.selection_ok());
    }
    CHECK(list.submitted.empty());
}

}

int main()
{
    Test::ScopedTestApp app;
    auto                list = std::make_shared<TestList>();
    test_submit_search_match(*list);
    test_page_down_short_list(*list);
    test_no_matches(*list);
    test_search_after_scrolling(*list);
    test_paging(*list);
    test_wheel_keeps_selection(*list);
    test_scrollbar_pages(*list);
    test_empty_list(*list);
    return Test::test_result();
}
