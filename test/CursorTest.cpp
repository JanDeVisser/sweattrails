/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests App::request_cursor(): a widget asks for a cursor in every frame it
 * wants it, and in other frames the app goes back to the default. The tests
 * use modals, which get the input instead of the widgets under them.
 */

#include <memory>

#include <TestApp.h>

using namespace ST;

namespace {

// A modal that asks for the pointing hand while `wants_hand` is set.
struct HandWidget : public Widget {
    bool wants_hand { false };

    HandWidget()
        : Widget(nullptr)
    {
    }

    void process_input() override
    {
        if (wants_hand) {
            App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
        }
    }
};

// A widget under the modal, such as the data panel, that asks for the hand
// in every frame. It doesn't get process_input() while the modal is open.
void test_a_modal_resets_the_cursor()
{
    Test::ScopedTestApp scoped;
    auto               &app = *scoped.app;
    // As after a frame where the data panel asked for the hand.
    app.m_requested_cursor = MOUSE_CURSOR_POINTING_HAND;
    app.m_cursor = MOUSE_CURSOR_IBEAM;
    auto modal = std::make_shared<HandWidget>();
    app.push_modal(modal);
    app.process_input();
    CHECK_EQ(static_cast<int>(app.m_requested_cursor), static_cast<int>(MOUSE_CURSOR_IBEAM));
    CHECK_EQ(static_cast<int>(app.m_cursor), static_cast<int>(MOUSE_CURSOR_IBEAM));
}

void test_the_request_lasts_one_frame()
{
    Test::ScopedTestApp scoped;
    auto               &app = *scoped.app;
    auto                modal = std::make_shared<HandWidget>();
    app.push_modal(modal);

    modal->wants_hand = true;
    app.process_input();
    CHECK_EQ(static_cast<int>(app.m_cursor), static_cast<int>(MOUSE_CURSOR_POINTING_HAND));
    app.process_input();
    CHECK_EQ(static_cast<int>(app.m_cursor), static_cast<int>(MOUSE_CURSOR_POINTING_HAND));

    modal->wants_hand = false;
    app.process_input();
    CHECK_EQ(static_cast<int>(app.m_cursor), static_cast<int>(MOUSE_CURSOR_IBEAM));
}

}

int main()
{
    test_a_modal_resets_the_cursor();
    test_the_request_lasts_one_frame();
    return ST::Test::test_result();
}
