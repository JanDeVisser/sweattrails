/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <vector>

#include <raylib.h>

#include <IO.h>
#include <JSON.h>
#include <Logging.h>
#include <Options.h>
#include <Utf8.h>
#include <Zip.h>

#include <App.h>
#include <Emoji.h>
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

// The characters the app's fonts have: Latin, Greek and Cyrillic, from 32
// to 1023, and General Punctuation, U+2000 to U+206F, for the curly quotes,
// dashes and "…" that text typed elsewhere, such as Strava's descriptions,
// has.
std::vector<int> const &codepoints()
{
    static std::vector<int> const ret = [] {
        std::vector<int> points;
        for (auto ch = 32; ch <= 0x206F; ++ch) {
            if (App::font_has(static_cast<rune>(ch))) {
                points.push_back(ch);
            }
        }
        return points;
    }();
    return ret;
}

bool App::font_has(rune ch)
{
    return (ch >= 32 && ch < 1024) || (ch >= 0x2000 && ch <= 0x206F);
}

void App::set_emoji(std::filesystem::path const &zip)
{
    // Once: the fonts, which call for it, change with the settings.
    if (!emoji_zip.empty()) {
        return;
    }
    auto contents = read_file_by_name<char>(zip.string());
    if (!contents) {
        warning(App, "No emoji: can't read `{}`: {}", zip.string(), contents.error().to_string());
        return;
    }
    auto const names = zip_entry_names(*contents);
    if (!names) {
        warning(App, "No emoji: `{}` isn't a ZIP: {}", zip.string(), names.error().to_string());
        return;
    }
    set_emoji_set(EmojiSet::from_names(*names));
    emoji_zip = std::move(*contents);
}

std::optional<Texture2D> App::emoji_texture(std::string const &image)
{
    if (auto const it = emoji_textures.find(image); it != emoji_textures.end()) {
        return it->second.id != 0 ? std::optional { it->second } : std::nullopt;
    }
    Texture2D texture { };
    if (auto const png = unzip(emoji_zip, image); png) {
        auto picture = LoadImageFromMemory(".png", reinterpret_cast<unsigned char const *>(png->data()), static_cast<int>(png->size()));
        if (picture.data != nullptr) {
            texture = LoadTextureFromImage(picture);
            // Drawn at the text's height, mostly well under its 72 px.
            GenTextureMipmaps(&texture);
            SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
            UnloadImage(picture);
        }
    }
    // One that can't be loaded isn't tried again.
    emoji_textures.emplace(image, texture);
    return texture.id != 0 ? std::optional { texture } : std::nullopt;
}

Vector2 App::measure_text(char const *text, FontSize size)
{
    auto const  font = *the()->font(size);
    auto const  font_size = static_cast<float>(the()->font_sizes[static_cast<size_t>(size)]);
    auto const *set = emoji_set();
    // No emoji is all ASCII.
    auto const ascii = std::ranges::all_of(std::string_view { text }, [](char ch) { return static_cast<unsigned char>(ch) < 0x80; });
    if (set == nullptr || ascii) {
        return MeasureTextEx(font, text, font_size, 2);
    }
    auto const runes = to_wstring(text).value_or(rune_string { });
    auto const runs = split_emoji(runes, *set, font_has);
    if (runs.size() == 1 && runs.front().kind == RunKind::Text) {
        return MeasureTextEx(font, text, font_size, 2);
    }
    auto const measure = [&font, font_size](rune_view piece) {
        return MeasureTextEx(font, to_utf8(piece).value_or(std::string { }).c_str(), font_size, 2).x;
    };
    return Vector2 { .x = runs_width(runes, runs, measure, font_size, 2), .y = MeasureTextEx(font, "Mg", font_size, 2).y };
}

void App::set_fonts(std::string_view const &path)
{
    std::string p { path };
    for (auto ix = static_cast<size_t>(FontSize::XSmall); ix <= static_cast<size_t>(FontSize::XLarge); ++ix) {
        auto sz = clamp(font_sizes[ix], 4, 48);
        font_sizes[ix] = sz;
        trace(App, "Loading font '{}', size {}", p, sz);
        auto const &points = codepoints();
        // raylib takes the characters as a pointer that isn't const.
        auto f = LoadFontEx(p.c_str(), sz, const_cast<int *>(points.data()), static_cast<int>(points.size()));
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
    for (auto const &[image, texture] : emoji_textures) {
        if (texture.id != 0) {
            UnloadTexture(texture);
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

// Every frame, also while a modal has the input: IsWindowResized() is only
// true in the frame after the resize, so one missed is missed for good.
// Tests have no window to ask about.
void App::start_frame()
{
    time = GetTime();
    ++frame_count;
    if (!IsWindowReady()) {
        return;
    }
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
    start_frame();
    if (IsWindowReady() && IsFileDropped()) {
        auto const               dropped = LoadDroppedFiles();
        std::vector<std::string> paths;
        for (auto ix = 0u; ix < dropped.count; ++ix) {
            paths.emplace_back(dropped.paths[ix]);
        }
        UnloadDroppedFiles(dropped);
        files_dropped(paths);
    }
    m_new_keys.clear();
    m_requested_cursor = default_cursor;
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
            m_new_keys.insert(key);
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
    } else {
        pWidget f = focus;
        if (!f) {
            f = self();
        }
        handle_keyboard(f);
        Layout::process_input();
    }

    if (m_requested_cursor != m_cursor) {
        SetMouseCursor(m_requested_cursor);
        m_cursor = m_requested_cursor;
    }
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

void App::pop_modal(pWidget const &modal)
{
    std::erase(modals, modal);
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
