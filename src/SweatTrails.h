/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <filesystem>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <JSON.h>

#include <App.h>

namespace ST {

namespace fs = std::filesystem;

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

struct SettingsError {
    explicit SettingsError(std::string_view const &e)
        : error(e)
    {
    }

    std::string error;
};

class STError {
public:
    template<class... Ts>
    struct overloaded : Ts... {
        using Ts::operator()...;
    };
    template<class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    template<typename T>
    explicit STError(T const &e)
        : error(e)
    {
    }

    std::variant<LibCError, JSONError, SettingsError> error;

    [[nodiscard]] std::string to_string() const
    {
        return std::visit(
            overloaded {
                [](LibCError const &e) { return e.to_string(); },
                [](JSONError const &e) { return e.to_string(); },
                [](SettingsError const &e) { return e.error; },
            },
            error);
    }
};

using EError = Error<STError>;

using pSweatTrails = std::shared_ptr<struct SweatTrails>;

struct SweatTrails : public App {
    AppState   app_state { };
    fs::path   system_config_dir { };
    fs::path   user_config_dir { };
    FT_Library ft_library { };
    JSONValue  settings;
    StringList font_dirs;

    SweatTrails();
    static pSweatTrails the();
    static void         set_message(std::string_view const &text);

    template<typename... Args>
    static void set_message(std::string_view const &fmt, Args const &...args)
    {
        set_message(std::format(fmt, std::make_format_args<Args...>(args)...));
    }

    void       initialize() override;
    bool       query_close() override;
    void       on_start() override;
    void       on_resize() override;
    void       process_input() override;
    void       on_terminate() override;
    EError     read_settings();
    void       load_font();
    StringList get_font_dirs();
    void       terminate();
};

}
