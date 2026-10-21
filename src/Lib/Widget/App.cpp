/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <raylib.h>

#include <IO.h>
#include <JSON.h>
#include <Logging.h>
#include <Options.h>

#include <App.h>
#include <Widget.h>

namespace ST {

std::shared_ptr<App> App::s_app { nullptr };

void App::draw_floating(pWidget const &target, App::Draw const &draw)
{
    assert(target != nullptr);
    assert(draw != nullptr);
    floatings.emplace_back(target, draw);
}

void App::draw()
{
    floatings.clear();
    Layout::draw();
    for (auto &floating : floatings) {
        floating.draw(floating.target);
    }
    for (auto &modal : modals) {
        modal->draw();
    }
}

int codepoints[992] { };

void App::set_fonts(std::string_view const &path)
{
    std::string p { path };
    for (auto ix = static_cast<size_t>(FontSize::XSmall); ix <= static_cast<size_t>(FontSize::XLarge); ++ix) {
        auto sz = clamp(font_sizes[ix], 4, 48);
        font_sizes[ix] = sz;
        std::println("Loading font '{:}', size {:}", p, sz);
        if (codepoints[0] != 32) {
            for (auto ix = 0; ix < 992; ++ix) {
                codepoints[ix] = ix + 32;
            }
        }
        auto f = LoadFontEx(p.c_str(), sz, codepoints, 992);
        if (f.baseSize == 0) {
            return;
        }
        if (fonts[ix]) {
            UnloadFont(*fonts[ix]);
        }
        fonts[ix] = f;
        if (font_path != path) {
            font_path = std::string { path };
        }
    }
    resize();
}

void App::on_resize()
{
    for (auto ix = static_cast<size_t>(FontSize::XSmall); ix <= static_cast<size_t>(FontSize::XLarge); ++ix) {
        if (fonts[ix]) {
            auto const measurements = MeasureTextEx(*fonts[ix],
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
                static_cast<float>(font_sizes[ix]), 2);
            char_sizes[ix].x = measurements.x / 52.0f;
            char_sizes[ix].y = measurements.y;
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = static_cast<float>(GetScreenWidth());
            viewport.height = static_cast<float>(GetScreenHeight());
            cells[ix].x = char_sizes[ix].x * 1.05;
            cells[ix].y = char_sizes[ix].y * 1.05;
        }
    }
}

App::App(std::string_view const &app_name)
    : Layout(nullptr)
    , app_name(app_name)
{
}

App::~App()
{
    for (auto ix = static_cast<size_t>(FontSize::XSmall); ix <= static_cast<size_t>(FontSize::XLarge); ++ix) {
        if (auto f = fonts[ix]; f) {
            UnloadFont(*f);
        }
    }
}

void App::start()
{
    on_start();
    resize();
    while (!quit) {
        if (WindowShouldClose()) {
            quit = query_close();
        } else {
            process_input();
        }
        BeginDrawing();
        draw();
        EndDrawing();
    }
    on_terminate();
    CloseWindow();
}

void App::on_process_input()
{
    time = GetTime();
    ++frame_count;
    if (IsWindowResized()) {
        viewport.width = static_cast<float>(GetScreenWidth());
        viewport.height = static_cast<float>(GetScreenHeight());
        resize();
    }
    monitor = GetCurrentMonitor();
}

void App::handle_keyboard(pWidget const &focus)
{
}

void App::process_input()
{
    KeyboardModifier modifier = modifier_current();
    auto             handle_keyboard = [this, &modifier](pWidget const &f) -> bool {
        for (int ch = GetCharPressed(); ch != 0; ch = GetCharPressed()) {
            for (auto w = f; w != nullptr; w = w->parent) {
                if (w->character(ch)) {
                    break;
                }
            }
        }
        std::erase_if(m_pressed_keys, [](auto const &key) {
            return IsKeyUp(key);
        });
        std::vector<int> keys;
        std::for_each(m_pressed_keys.begin(), m_pressed_keys.end(), [&keys](auto const &key) {
            if (IsKeyPressedRepeat(key)) {
                keys.emplace_back(key);
            }
        });
        for (int key = GetKeyPressed(); key != 0; key = GetKeyPressed()) {
            keys.emplace_back(key);
            m_pressed_keys.insert(key);
        }
        for (auto const key : keys) {
            f->bubble_up([key, modifier](pWidget const &w) {
                return w->process_key(modifier, key);
            });
        }
        return false;
    };

    if (!modals.empty()) {
        pWidget modal = modals.back();
        handle_keyboard(modal);
        modal->process_input();
        return;
    }

    pWidget f = focus;
    if (!f) {
        f = self();
    }
    handle_keyboard(f);
    Layout::process_input();
}

void App::push_modal(pWidget const &modal)
{
    modals.push_back(modal);
}

void App::pop_modal()
{
    if (!modals.empty()) {
        modals.pop_back();
    }
}

// void App::change_font_size(int increment, FontSize size)
// {
//     set_font(font_path, font_sizes[static_cast<size_t>(size)] + increment, size);
// }

void AppState::read()
{
    auto const exists_maybe = path_exists(file);
    if (!exists_maybe) {
        warning(App, "Could not check app state file `{}`: {}", file.string(), exists_maybe.error().to_string());
        return;
    }
    if (!*exists_maybe) {
        write();
        return;
    }
    auto contents = read_file_by_name(file.string());
    if (!contents) {
        warning(App, "Could not read app state from `{}`: {}", file.string(), contents.error().to_string());
        return;
    }
    if (contents->size() != sizeof(state)) {
        warning(App, "App state file `{}` has size {}, expected {}", file.string(), contents->size(), sizeof(state));
        return;
    }
    memcpy(&state, contents->data(), sizeof(state));
}

void AppState::write()
{
    if (auto res = ensure_directory(file.parent_path()); !res) {
        warning(App, "Could not create directory for app state: {}", res.error().to_string());
        return;
    }
    if (auto res = write_file_by_name(file.string(), std::string_view { reinterpret_cast<char const *>(&state), sizeof(state) }); !res) {
        warning(App, "Could not write app state to `{}`: {}", file.string(), res.error().to_string());
    }
}

}
