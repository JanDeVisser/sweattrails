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

#include <IO.h>
#include <Options.h>

#include <Widget.h>
#include <Window.h>

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

    std::string                 app_name; // Names the per-user directories
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
    std::string                 icon_file { }; // The window's icon, if any
    CharSizes                   cells;
    static std::shared_ptr<App> s_app;
    std::set<int>               m_pressed_keys;
    std::set<int>               m_new_keys; // Pressed this frame
    MouseCursor                 default_cursor { MOUSE_CURSOR_IBEAM };
    MouseCursor                 m_requested_cursor { MOUSE_CURSOR_IBEAM }; // For this frame
    MouseCursor                 m_cursor { MOUSE_CURSOR_IBEAM };           // Shown now

    explicit App(std::string_view const &app_name);
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
    void start_frame();
    void draw_floating(pWidget const &target, Draw const &draw);
    void set_fonts(std::string_view const &path);
    void handle_keyboard(pWidget const &focus);
    void push_modal(pWidget const &modal);
    void pop_modal();
    // Closes `modal`, which needn't be on top: its submit function may have
    // opened another.
    void pop_modal(pWidget const &modal);

    // A widget that wants another mouse cursor than the default asks for it
    // in process_input(), in every frame that it wants it. In other frames
    // the app shows the default cursor, so a widget doesn't have to put it
    // back, which it can't do while a modal gets the input instead of it.
    void request_cursor(MouseCursor cursor)
    {
        m_requested_cursor = cursor;
    }
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

        // raylib logs every texture, font and shader it loads.
        SetTraceLogLevel(LOG_WARNING);
        // raylib draws shapes without smoothing their edges: multisampling
        // does, for everything drawn in the window. It has to be asked for
        // before the window is made. MaximizeWindow() does nothing to a
        // window that isn't resizable, and a hidden one doesn't flash at the
        // wrong size and place before it's maximized.
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIDDEN);
        // Not 0x0: raylib makes that as large as the whole monitor, which on
        // macOS covers the menu bar, the Dock and the other apps. The
        // monitors can only be asked about once there's a window.
        InitWindow(800, 600, app->window_title());
        app->monitor = start_monitor(app->monitor, GetMonitorCount());
        // Where the window goes when it's un-maximized, too.
        auto const restored = restored_window(GetMonitorPosition(app->monitor),
            Vector2 { .x = static_cast<float>(GetMonitorWidth(app->monitor)), .y = static_cast<float>(GetMonitorHeight(app->monitor)) });
        SetWindowSize(static_cast<int>(restored.width), static_cast<int>(restored.height));
        SetWindowPosition(static_cast<int>(restored.x), static_cast<int>(restored.y));
        ClearWindowState(FLAG_WINDOW_HIDDEN);
        // On macOS, the zoom: the screen less the menu bar and the Dock.
        MaximizeWindow();
        app->viewport.width = static_cast<float>(GetScreenWidth());
        app->viewport.height = static_cast<float>(GetScreenHeight());
        SetWindowState(FLAG_VSYNC_HINT);
        if (!app->icon_file.empty()) {
            Image icon = LoadImage(app->icon_file.c_str());
            SetWindowIcon(icon);
            UnloadImage(icon);
        }
        SetMouseCursor(app->m_cursor);
        SetExitKey(KEY_NULL);
        SetTargetFPS(60);
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
    fs::path file;
    int      state[ASCount] = { 0 };

    explicit AppState(std::string_view const &app_name)
        : file(user_state_dir(app_name) / "state")
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
    AppState   app_state;
    fs::path   system_config_dir { };
    fs::path   user_config_dir;
    JSONValue  settings;
    StringList font_dirs;

    using Bus = AppBus<Job, Notification>;
    using J = Job;
    using N = Notification;

    Bus bus { };

    explicit Main(std::string_view const &app_name)
        : App(app_name)
        , app_state(app_name)
        , user_config_dir(ST::user_config_dir(app_name))
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
        // Before the modal handles its keys, and maybe closes.
        auto const modal_was_open = !modals.empty();
        App::process_input();
        // Only the keys pressed this frame: a binding runs once per press,
        // not on every frame its keys are held down.
        bus.handle_keys(m_new_keys, modal_was_open);
    }
};

}
