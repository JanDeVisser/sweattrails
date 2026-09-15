/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pwd.h>
#include <raylib.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <config.h>

#include <App.h>
#include <JSON.h>
#include <Logging.h>
#include <Modal.h>
#include <Options.h>
#include <SweatTrails.h>
#include <Widget.h>

#include <storage/Activity.h>
#include <variant>
#include <widget/Activity.h>

namespace ST {

namespace fs = std::filesystem;

template<>
char const *value_to_string(JobType type)
{
    switch (type) {
#undef S
#define S(V, P)      \
    case JobType::V: \
        return #V;
        JOBTYPE(S)
    default:
        UNREACHABLE();
    }
}

template<>
char const *value_to_string(NotificationType type)
{
    switch (type) {
#undef S
#define S(V, P)                   \
    case ST::NotificationType::V: \
        return #V;
        NOTIFICATIONTYPE(S)
    default:
        UNREACHABLE();
    }
}

void SweatTrails::job_find_current_folder(Job::Payload const &)
{
    auto res = SweatTrails::the()->storage.find_current_folder();
    if (!res) {
        SweatTrails::the()->notify<NotificationType::Message>(
            std::format("Error finding current folder: {}", res.error().to_string()));
        return;
    }
    auto activities = res.value();
    if (!activities) {
        SweatTrails::the()->notify<NotificationType::Message>(
            std::string { "No current folder found" });
        return;
    }
    SweatTrails::the()->notify<NotificationType::ActivityList>(*activities);
}

void SweatTrails::job_find_newest_activity(Job::Payload const &)
{
    auto res = SweatTrails::the()->storage.find_newest();
    if (!res) {
        SweatTrails::the()->notify<NotificationType::Message>(
            std::format("Error finding newest activity: {}", res.error().to_string()));
        return;
    }
    auto activity = res.value();
    if (!activity) {
        SweatTrails::the()->notify<NotificationType::Message>(
            std::string { "No activities found" });
        return;
    }
    SweatTrails::the()->notify<NotificationType::Activity>(*activity);
}

pSweatTrails SweatTrails::the()
{
    return std::dynamic_pointer_cast<SweatTrails>(App::the());
}

SweatTrails::SweatTrails()
    : Main<Job, Notification>()
{
    passwd *pw = getpwuid(getuid());
    user_config_dir = fs::path { pw->pw_dir } / ".sweattrails";
    system_config_dir = fs::path { SWEATTRAILS_DATADIR };
}

bool SweatTrails::query_close()
{
    bus.submit("st-quit", JSONValue { });
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

void SweatTrails::on_terminate()
{
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

void cmd_message(pSweatTrails const &, JSONValue const &msg)
{
    message_box(msg.to_string());
}

void SweatTrails::notif_message(Notification::Payload const &data)
{
    message_box(std::get<std::string>(data));
}

void SweatTrails::notif_activity_list(Notification::Payload const &data)
{
    auto activities = std::get<Activities>(data);
    message_box(
        std::format("Got activities for {}-{}",
            static_cast<int>(activities.id.month),
            activities.id.year));
}

void SweatTrails::notif_activity(Notification::Payload const &data)
{
    SweatTrails::the()->show_activity(std::get<Activity>(data));
}

void SweatTrails::show_activity(Activity const &activity)
{
    auto display = widget_stack->activate<ActivityDisplay>();
    assert(display != nullptr);
    display->set_activity(activity);
}

void SweatTrails::initialize()
{
    std::println("initialize!");
    MUST(read_settings());
    load_font();

    std::string project_dir { "." };
    if (!arguments.empty()) {
        auto project_dir_maybe = arguments.front();
        if (fs::is_directory(project_dir_maybe)) {
            project_dir = project_dir_maybe;
            arguments.pop_front();
        }
    }
    storage = MUST_EVAL(Storage::init());

    widget_stack = add_widget<WidgetStack>();
    widget_stack->add_widget<ActivityDisplay>();

    bus.add_task<SweatTrails>("st-force-quit", self(), cmd_force_quit)
        .bind(KeyCombo { KEY_Q, KModControl | KModShift });
    bus.add_task<SweatTrails>("st-quit", self(), cmd_quit)
        .bind(KeyCombo { KEY_Q, KModControl });
    bus.add_task<SweatTrails>("st-message", self(), cmd_message);

    bus.schedule(make_job<JobType::FindNewestActivity>(std::monostate { }));
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
            if (!json_maybe) {
                switch (json_maybe.error().index()) {
                case 0:
                    return std::unexpected(STError { std::get<LibCError>(json_maybe.error()) });
                case 1:
                    return std::unexpected(STError { std::get<JSONError>(json_maybe.error()) });
                default:
                    UNREACHABLE();
                }
            }
            settings.merge(json_maybe.value());
        }
        return { };
    };

    if (auto const &e = merge_settings(SWEATTRAILS_DATADIR, SWEATTRAILS_SYSTEM ".json"); !e) {
        return std::unexpected(e.error());
    }
    if (auto const &e = merge_settings(system_config_dir); !e) {
        return std::unexpected(e.error());
    }
    if (auto const &e = merge_settings(user_config_dir); !e) {
        return std::unexpected(e.error());
    }
    if (auto const &e = merge_settings(".sweattrails"); !e) {
        return std::unexpected(e.error());
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
            if (directories.convert(dirs)) {
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
    font_sizes[static_cast<size_t>(FontSize::XSmall)] = font_size / 2;
    font_sizes[static_cast<size_t>(FontSize::Small)] = (3 * font_size) / 4;
    font_sizes[static_cast<size_t>(FontSize::Medium)] = font_size;
    font_sizes[static_cast<size_t>(FontSize::Large)] = (3 * font_size) / 2;
    font_sizes[static_cast<size_t>(FontSize::XLarge)] = font_size * 2;
    info(ST, "font: {}", font_name);

    StringList dirs = get_font_dirs();
    auto       find_font = [this, &dirs](auto const &font) -> bool {
        return std::any_of(dirs.begin(), dirs.end(), [this, font](auto const &dir) -> bool {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                if (auto const path = fs::path { dir } / font; fs::exists(path) && !fs::is_directory(path)) {
                    set_fonts(path.string());
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

void SweatTrails::set_message(std::string_view const &text)
{
    notify<NotificationType::Message>(std::string { text });
}

}

int main(int argc, char const **argv)
{
    auto sweattrails = ST::App::create<ST::SweatTrails>(argc, argv);
    sweattrails->start();
    return 0;
}
