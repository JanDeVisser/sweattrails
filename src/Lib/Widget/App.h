/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <raylib.h>

#include <Options.h>
#include <Widget.h>

namespace ST {

namespace fs = std::filesystem;

struct App : public Layout {
    using Draw = std::function<void(pWidget const &target)>;
    using OptFont = std::optional<Font>;
    using Fonts = std::array<OptFont, static_cast<size_t>(FontSize::XLarge) + 1>;
    using FontSizes = std::array<int, static_cast<size_t>(FontSize::XLarge) + 1>;
    using CharSizes = std::array<Vector2, static_cast<size_t>(FontSize::XLarge) + 1>;
    constexpr static FontSizes def_font_sizes = { 12, 15, 20, 25, 30 };

    struct DrawFloating {
        pWidget target;
        Draw    draw;

        DrawFloating(pWidget target, Draw draw)
            : target(std::move(target))
            , draw(std::move(draw))
        {
        }
    };

    std::deque<std::string>     arguments;
    int                         monitor { 0 };
    Fonts                       fonts { };
    std::string                 font_path { };
    FontSizes                   font_sizes = def_font_sizes;
    pWidget                     focus { nullptr };
    CharSizes                   char_sizes { };
    std::string                 last_key;
    bool                        quit { false };
    double                      time { 0.0 };
    std::vector<pWidget>        modals { };
    size_t                      frame_count { 0 };
    std::vector<DrawFloating>   floatings;
    std::string                 title_string { "Sweattrails" };
    std::string                 icon_file { "sweattrails.png" };
    CharSizes                   cells;
    static std::shared_ptr<App> s_app;
    std::set<int>               m_pressed_keys;

    App();
    ~App();

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
    void set_fonts(std::string_view const &path);
    void handle_keyboard(pWidget const &focus);
    void push_modal(pWidget const &modal);
    void pop_modal();
    // void change_font_size(int increment, FontSize size = FontSize::Medium);

    virtual bool query_close()
    {
        return true;
    }

    Vector2 char_size(FontSize font_size = FontSize::Medium) const
    {
        return char_sizes[static_cast<size_t>(font_size)];
    }

    Vector2 cell(FontSize font_size = FontSize::Medium) const
    {
        return cells[static_cast<size_t>(font_size)];
    }

    std::optional<Font> font(FontSize font_size = FontSize::Medium)
    {
        return fonts[static_cast<size_t>(font_size)];
    }

    static Vector2 measure_text(char const *text, FontSize size)
    {
        return MeasureTextEx(*(the()->font(size)), text, the()->font_sizes[static_cast<size_t>(size)], 2);
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
};

enum AppStateItem {
    ASMonitor = 0,
    ASCount,
};

struct AppState {
    int state[ASCount] = { 0 };

    AppState()
    {
        memset(&state, 0, sizeof(state));
    }

    void              read();
    void              write();
    [[nodiscard]] int monitor() const
    {
        return state[ASMonitor];
    }

    void monitor(int the_monitor)
    {
        state[ASMonitor] = the_monitor;
        write();
    }
};

template<typename Job, typename Notification>
struct Main : public App {
    AppState   app_state { };
    fs::path   system_config_dir { };
    fs::path   user_config_dir { };
    JSONValue  settings;
    StringList font_dirs;

    using Bus = AppBus<Job, Notification>;
    using J = Job;
    using N = Notification;

    Bus bus { };

    Main()
        : App()
    {
        app_state.read();
        monitor = app_state.monitor();
        bus.start();
    }

    void initialize() override
    {
    }

    void process_input() override
    {
        if (monitor != app_state.monitor()) {
            app_state.monitor(monitor);
        }
        bus.handle_one();
        App::process_input();
        bus.handle_keys(m_pressed_keys);
    }
};

}
