/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "Modal.h"
#include "Options.h"
#include <print>
#include <pwd.h>
#include <raylib.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <config.h>

#include <App.h>
#include <SweatTrails.h>

namespace ST {

namespace fs = std::filesystem;

void AppState::read()
{
    struct passwd *pw = getpwuid(getuid());
    struct stat    sb;
    char const    *datadir = TextFormat("%s/.sweattrails", pw->pw_dir);
    if (stat(datadir, &sb) != 0) {
        mkdir(datadir, 700);
    }

    char const *state_fname = TextFormat("%s/.sweattrails/state", pw->pw_dir);
    if (stat(state_fname, &sb) == 0) {
        int state_fd = open(state_fname, O_RDONLY);
        ::read(state_fd, this, sizeof(AppState));
        close(state_fd);
    } else {
        write();
    }
}

void AppState::write()
{
    struct passwd *pw = getpwuid(getuid());
    char const    *state_fname = TextFormat("%s/.sweattrails/state", pw->pw_dir);
    int            state_fd = open(state_fname, O_RDWR | O_CREAT | O_TRUNC, 0600);
    ::write(state_fd, this, sizeof(AppState));
    close(state_fd);
}

pSweatTrails SweatTrails::the()
{
    return std::dynamic_pointer_cast<SweatTrails>(App::the());
}

SweatTrails::SweatTrails()
    : App()
{
    app_state.read();
    monitor = app_state.monitor();
    passwd *pw = getpwuid(getuid());
    user_config_dir = fs::path { pw->pw_dir } / ".sweattrails";
    system_config_dir = fs::path { SWEATTRAILS_DATADIR };

    if (FT_Init_FreeType(&ft_library)) {
        fatal("Could not initialize freetype");
    }
}

bool SweatTrails::query_close()
{
    submit("aragorn-quit", JSONValue { });
    return false;
}

void SweatTrails::on_start()
{
    monitor = GetCurrentMonitor();
}

void SweatTrails::on_resize()
{
    App::on_resize();

    auto const &appearance = settings.get("appearance").value_or(JSONValue(JSONType::Object));
}

void SweatTrails::process_input()
{
    if (monitor != app_state.monitor()) {
        app_state.monitor(monitor);
    }
    App::process_input();
}

void SweatTrails::on_terminate()
{
    if (font) {
        UnloadFont(*font);
    }
}

void SweatTrails::terminate()
{
    quit = true;
}

void cmd_force_quit(pSweatTrails const &sweattrails, JSONValue const &)
{
    sweattrails->terminate();
}

void cmd_quit(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (!sweattrails->modals.empty()) {
        // User probably clicked the close window button twice
        return;
    }
    auto const *prompt = "Are you sure you want to quit?";
    auto        are_you_sure = [](pSweatTrails const &sweattrails, QueryOption selection) {
        if (selection == QueryOption::QueryOptionYes) {
            sweattrails->terminate();
        }
    };
    query_box(sweattrails, prompt, are_you_sure, QueryOptionYesNo);
}

void SweatTrails::initialize()
{
    info(ST, "initialize!");
    auto res = read_settings();
    if (res.is_error()) {
        std::println("initialize E!");
        fatal("Error reading settings: {}", res.error().to_string());
    }
    load_font();

    std::string project_dir { "." };
    if (!arguments.empty()) {
        auto project_dir_maybe = arguments.front();
        if (fs::is_directory(project_dir_maybe)) {
            project_dir = project_dir_maybe;
            arguments.pop_front();
        }
    }
    add_command<SweatTrails>("st-force-quit", cmd_force_quit)
        .bind(KeyCombo { KEY_Q, KModControl | KModShift });
    add_command<SweatTrails>("st-quit", cmd_quit)
        .bind(KeyCombo { KEY_Q, KModControl });
}

EError SweatTrails::read_settings()
{
    if (!settings.is_null()) {
        return { };
    }
    settings = JSONValue::object();
    settings["appearance"] = JSONValue::object();

    auto merge_settings = [this](fs::path const &dir, std::string const &file = "settings.json") -> EError {
        create_directory(dir);
        auto settings_file = dir / file;
        // std::println("--> {}", settings_file.string());
        if (exists(settings_file)) {
            auto json_maybe = JSONValue::read_file(settings_file.string());
            if (json_maybe.is_error()) {
                switch (json_maybe.error().index()) {
                case 0:
                    return STError { std::get<LibCError>(json_maybe.error()) };
                case 1:
                    return STError { std::get<JSONError>(json_maybe.error()) };
                default:
                    UNREACHABLE();
                }
            }
            settings.merge(json_maybe.value());
        }
        return { };
    };

    if (auto const &e = merge_settings(SWEATTRAILS_DATADIR, SWEATTRAILS_SYSTEM ".json"); e.is_error()) {
        return e;
    }
    if (auto const &e = merge_settings(system_config_dir); e.is_error()) {
        return e;
    }
    if (auto const &e = merge_settings(user_config_dir); e.is_error()) {
        return e;
    }
    if (auto const &e = merge_settings(".sweattrails"); e.is_error()) {
        return e;
    }
    auto appearance = settings.get_with_default("appearance");
    ASSERT_JSON_TYPE(appearance, Object);
    std::string theme_name = "darcula";
    if (auto theme_name_value = appearance.get("theme")) {
        theme_name = theme_name_value.value().to_string();
    }
    return { };
}

StringList SweatTrails::get_font_dirs()
{
    if (font_dirs.empty()) {
        auto &appearance = settings["appearance"];
        auto &directories = appearance["font_directories"];

        struct passwd *pw = getpwuid(getuid());
        auto           append_dir = [pw, this](std::string_view const &dir) -> void {
            std::string d { dir };
            replace_all(d, "${HOME}", pw->pw_dir);
            replace_all(d, "${sweattrails_DATADIR}", SWEATTRAILS_DATADIR);
            if (std::find(font_dirs.begin(), font_dirs.end(), d) == font_dirs.end()) {
                if (!fs::is_directory(d)) {
                    return;
                }
                font_dirs.push_back(d);
                for (auto const &dir_entry : fs::recursive_directory_iterator(d)) {
                    if (dir_entry.is_directory()) {
                        font_dirs.push_back(d);
                    }
                }
            }
        };

        if (directories.is_string()) {
            append_dir(directories.to_string());
        } else if (directories.is_array()) {
            std::vector<std::string> dirs;
            if (!directories.convert(dirs).is_error()) {
                for (auto const &dir : dirs) {
                    append_dir(dir);
                }
            }
        }
    }
    return font_dirs;
}

void SweatTrails::load_font()
{
    auto        appearance = settings["appearance"];
    std::string default_font { "VictorMono-Medium.ttf" };
    std::string font_name { default_font };
    auto const  font_maybe = appearance.try_get<std::string>("font");
    if (font_maybe.has_value()) {
        font_name = font_maybe.value();
    }
    int        font_size = 20;
    auto const font_size_maybe = appearance.try_get<int>("font_size");
    if (font_size_maybe.has_value()) {
        font_size = font_size_maybe.value();
    }
    info(ST, "font: {}", font_name);

    StringList dirs = get_font_dirs();
    auto       find_font = [this, &dirs, font_size](auto const &font) -> bool {
        return std::any_of(dirs.begin(), dirs.end(), [this, font, font_size](auto const &dir) -> bool {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                if (auto const path = fs::path { dir } / font; fs::exists(path) && !fs::is_directory(path)) {
                    set_font(path.string(), font_size);
                    return true;
                }
            }
            return false;
        });
    };

    if (!find_font(font_name) && (font_name != default_font)) {
        assert(find_font(default_font));
    }
}

}

int main(int argc, char const **argv)
{
    ST::parse_options(argc, argv);
    auto sweattrails = ST::App::create<ST::SweatTrails>(argc, argv);
    sweattrails->start();
    return 0;
}
