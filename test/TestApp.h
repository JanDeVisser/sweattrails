/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Support for testing widgets without a window. See test/README.md.
 */

#pragma once

#include <memory>
#include <print>

#include <App.h>

namespace ST::Test {

// An App that never opens a window. Widgets reach the app through
// App::the(), which asserts that there is one, so a test has to install
// one before it makes a widget. Nothing here initializes raylib: anything
// that draws, measures text or loads fonts (draw(), resize(), App::start())
// doesn't work, and the app has no fonts, so char_size() is { 0, 0 }.
struct TestApp : App {
    TestApp()
        : App("sweattrails-test")
    {
    }
};

// Installs a TestApp as App::the() for as long as it lives.
struct ScopedTestApp {
    std::shared_ptr<TestApp> app { std::make_shared<TestApp>() };

    ScopedTestApp()
    {
        App::s_app = app;
    }

    ~ScopedTestApp()
    {
        App::s_app.reset();
    }

    ScopedTestApp(ScopedTestApp const &) = delete;
    ScopedTestApp &operator=(ScopedTestApp const &) = delete;
};

inline int failures { 0 };

// Reports a failed check and carries on, so that one run shows all of them.
// A test's main() returns test_result().
#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::println(stderr, "{}:{}: CHECK failed: {}", __FILE__, __LINE__, #cond); \
            ++ST::Test::failures;                                                       \
        }                                                                               \
    } while (0)

// The two sides can have different types, as long as they compare with ==
// and both have a std::formatter, e.g. CHECK_EQ(name, "d swim").
#define CHECK_EQ(actual, expected)                                                                                      \
    do {                                                                                                                \
        auto const &a_ = (actual);                                                                                      \
        auto const &e_ = (expected);                                                                                    \
        if (!(a_ == e_)) {                                                                                              \
            std::println(stderr, "{}:{}: CHECK_EQ failed: {} is {}, expected {}", __FILE__, __LINE__, #actual, a_, e_); \
            ++ST::Test::failures;                                                                                       \
        }                                                                                                               \
    } while (0)

inline int test_result()
{
    if (failures > 0) {
        std::println(stderr, "{} check(s) failed", failures);
        return 1;
    }
    return 0;
}

}
