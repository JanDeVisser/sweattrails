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

// A search that leaves fewer entries than fit takes the scrollbar away:
// the wheel and the track don't scroll the matches.
void test_scrolling_after_search(TestList &list)
{
    list.setup(numbered(60));
    place_rows(list);
    list.key(KEY_PAGE_DOWN, 3);
    list.handle_scrolling(ScrollInput { .mouse = { .x = 50, .y = 50 } });
    CHECK_EQ(list.scroll.top_line, 40);
    list.type("e5");
    list.handle_scrolling(ScrollInput { .mouse = { .x = 50, .y = 50 }, .wheel = -1 });
    CHECK(!list.scroll.overflows());
    CHECK_EQ(list.scroll.top_line, 0);
    list.handle_scrolling(ScrollInput { .mouse = { .x = 195, .y = 150 }, .pressed = true, .down = true });
    CHECK_EQ(list.scroll.top_line, 0);
    CHECK(list.selection_ok());
}

// Page Down after the wheel scrolled away from the selection brings it
// back on screen.
void test_page_down_after_wheel(TestList &list)
{
    list.setup(numbered(60));
    place_rows(list);
    list.key(KEY_DOWN, 3);
    list.handle_scrolling(ScrollInput { .mouse = { .x = 50, .y = 50 }, .wheel = -25 });
    CHECK_EQ(list.scroll.top_line, 25);
    list.key(KEY_PAGE_DOWN);
    CHECK_EQ(list.selection, 23);
    CHECK(list.selection_ok());
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

// A list whose payload isn't its text, as an activity's isn't, with a
// prefix and columns, unsorted.
struct RowList : ListBox<int, ListBoxConfig { .search = true, .sort = false }> {
    RowList()
        : ListBox("rows")
    {
    }

    void add(std::string_view text, int payload, std::string_view prefix, std::vector<std::string> columns)
    {
        auto &e = entries.emplace_back(text, payload);
        e.prefix = prefix;
        e.columns = std::move(columns);
    }

    void setup()
    {
        entries.clear();
        add("Morning ride", 1, "Tue 14  09:32", { "42.3 km", "1:32:10" });
        add("Intervals", 2, "Thu 16  18:05", { "8.1 km", "41:07" });
        add("Zwift", 3, "Sat 4  10:00", { "", "1:00:00" });
        add("Long one", 4, "Sun 5  08:00", { "120 km", "4:12:00" });
        sort();
        search = { };
        filter();
        selection = 0;
        scroll.top_line = 0;
        scroll.lines = 2;
        status = ModalStatus::Active;
    }

    void submit() override
    {
    }
};

// Searching matches the text, not the prefix or the columns.
void test_search_ignores_prefix_and_columns()
{
    auto list = std::make_shared<RowList>();
    list->setup();
    list->search = "km";
    list->filter();
    CHECK_EQ(list->shown_count(), 0);
    list->search = "Tue";
    list->filter();
    CHECK_EQ(list->shown_count(), 0);
    list->search = "inter";
    list->filter();
    CHECK_EQ(list->shown_count(), 1);
    // Not sorted: in the order they were added.
    list->search = { };
    list->filter();
    CHECK_EQ(list->entries[2].text, "Zwift");
}

// select() finds the payload among the entries on display, and scrolls to
// it; one that isn't shown leaves the selection.
void test_select()
{
    auto list = std::make_shared<RowList>();
    list->setup();
    list->select(4);
    CHECK_EQ(list->selection, 3);
    CHECK(list->scroll.top_line <= 3 && 3 < list->scroll.top_line + list->scroll.lines);
    list->select(7);
    CHECK_EQ(list->selection, 3);
    list->search = "inter";
    list->filter();
    list->select(4);
    CHECK_EQ(list->selection, 0);
    list->select(2);
    CHECK_EQ(list->selection, 0);
}

// The prefix and each column are as wide as the widest on display.
void test_widths()
{
    auto list = std::make_shared<RowList>();
    list->setup();
    auto const w = list->widths();
    CHECK_EQ(w.prefix, 13uz);
    CHECK_EQ(w.columns, (std::vector<size_t> { 7uz, 7uz }));
    CHECK(!w.icon);
    list->search = "zwift";
    list->filter();
    CHECK_EQ(list->widths().columns, (std::vector<size_t> { 0uz, 7uz }));
}

// Cut by characters, not bytes, so that an é isn't cut in two.
void test_first_chars()
{
    CHECK_EQ(first_chars("Col du Télégraphe", 8), "Col du T");
    CHECK_EQ(first_chars("Col du Télégraphe", 9), "Col du Té");
    CHECK_EQ(first_chars("abc", 10), "abc");
    CHECK_EQ(first_chars("abc", 0), "");
    CHECK_EQ(char_count("Télégraphe"), 10uz);
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
    test_scrolling_after_search(*list);
    test_page_down_after_wheel(*list);
    test_empty_list(*list);
    test_search_ignores_prefix_and_columns();
    test_select();
    test_widths();
    test_first_chars();
    return Test::test_result();
}
