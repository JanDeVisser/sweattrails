/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <deque>

#include <raylib.h>

#include <Options.h>
#include <Widget.h>

namespace ST {

struct App : public Layout {
    using Draw = std::function<void(pWidget const &target)>;
    struct DrawFloating {
        pWidget target;
        Draw    draw;

        DrawFloating(pWidget target, Draw draw)
            : target(std::move(target))
            , draw(std::move(draw))
        {
        }
    };

    std::deque<std::string>   arguments;
    int                       monitor { 0 };
    std::optional<Font>       font { };
    std::string               font_path { };
    int                       font_size { 20 };
    pWidget                   focus { nullptr };
    Vector2                   char_size { 20.0, 20.0 };
    std::string               last_key;
    bool                      quit { false };
    double                    time { 0.0 };
    std::vector<pWidget>      modals { };
    std::mutex                commands_mutex { };
    size_t                    frame_count { 0 };
    std::vector<DrawFloating> floatings;
    std::string               title_string { "Sweattrails" };
    std::string               icon_file { "sweattrails.png" };
    Vector2                   cell;

    App();

    virtual void        on_start() { };
    virtual void        on_terminate() { };
    virtual char const *window_title() const
    {
        return title_string.c_str();
    }

    void start();
    void draw() override;
    void process_input() override;
    void on_resize() override;
    void on_process_input() override;
    void draw_floating(pWidget const &target, Draw const &draw);
    void set_font(std::string_view const &path, int font_size);
    void handle_keyboard(pWidget const &focus);
    void push_modal(pWidget const &modal);
    void pop_modal();
    void change_font_size(int increment);

    virtual bool query_close()
    {
        return true;
    }

    template<class AppClass>
        requires std::derived_from<AppClass, App>
    static std::shared_ptr<AppClass> create(int argc, char const **argv)
    {
        auto                 app_args = ST::parse_options(argc, argv);
        std::shared_ptr<App> app = Widget::make<AppClass>();
        for (auto ix = app_args; ix < argc; ++ix) {
            app->arguments.emplace_back(argv[ix]);
        }
        app->time = GetTime();

        //        SetTraceLogLevel(LOG_FATAL);
        InitWindow(static_cast<int>(app->viewport.width), static_cast<int>(app->viewport.height), app->window_title());
        app->viewport.width = static_cast<float>(GetScreenWidth());
        app->viewport.height = static_cast<float>(GetScreenHeight());
        SetWindowMonitor(app->monitor);
        SetWindowState(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED | FLAG_VSYNC_HINT);
        Image icon = LoadImage(app->icon_file.c_str());
        SetWindowIcon(icon);
        SetMouseCursor(MOUSE_CURSOR_IBEAM);
        SetExitKey(KEY_NULL);
        SetTargetFPS(60);
        MaximizeWindow();
        s_app = app;
        s_app->initialize();
        return dynamic_pointer_cast<AppClass>(s_app);
    }

    static std::shared_ptr<App> the()
    {
        assert(s_app != nullptr);
        return s_app;
    }

private:
    static std::shared_ptr<App> s_app;
    std::set<int>               m_pressed_keys;
};

}
