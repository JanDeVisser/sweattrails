/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdlib>
#include <ctime>
#include <variant>

#include <config.h>
#include <raylib.h>
#include <unistd.h>

#include <Expected.h>
#include <IO.h>
#include <JSON.h>
#include <Logging.h>
#include <Options.h>

#include <App.h>
#include <Modal.h>
#include <Widget.h>

#include <Http.h>
#include <OAuth.h>
#include <SweatTrails.h>
#include <storage/Activity.h>
#include <storage/Garmin.h>
#include <storage/Storage.h>
#include <storage/Types.h>
#include <storage/Wahoo.h>
#include <widget/Activity.h>
#include <widget/ActivitySelector.h>

namespace ST {

namespace fs = std::filesystem;

namespace {

// How long the Wahoo login waits for the browser to come back.
constexpr std::chrono::minutes WahooLoginTimeout { 5 };

// The certificate the Wahoo login's server answers with: Wahoo only sends
// the browser back to an https URL. The user makes it with mkcert.
fs::path certificate_file(fs::path const &config_dir)
{
    return config_dir / "localhost.pem";
}

fs::path certificate_key_file(fs::path const &config_dir)
{
    return config_dir / "localhost-key.pem";
}

SourceOutcome failed(SourceOutcome outcome, std::string message)
{
    outcome.kind = SourceOutcome::Kind::Failed;
    outcome.message = std::move(message);
    return outcome;
}

SourceOutcome download_from_garmin(pSweatTrails const &the, int days)
{
    SourceOutcome outcome { .source = "Garmin Connect" };
    auto const   *variable = std::getenv(GarminKeyVariable);
    auto const    key = read_api_key(variable ? std::optional<std::string_view> { variable } : std::nullopt, garmin_key_file());
    if (!key) {
        outcome.kind = SourceOutcome::Kind::NotSetUp;
        outcome.message = std::format(
            "There's no Garmin Connect API key. Create one in Garmin Connect's settings, under Personal API, and save it in {}.",
            garmin_key_file().string());
        return outcome;
    }
    GarminConnect garmin { .fetch = http_fetch };
    if (auto const token = garmin.obtain_token(*key); !token) {
        return failed(outcome, token.error().to_string());
    }
    auto const on_activity = [&the](size_t index, size_t count) {
        the->notify_latest<NotificationType::Downloading>(Downloading { "Garmin Connect", index, count });
    };
    auto const count = download_new_activities(garmin, the->storage, garmin_start_date(days, std::time(nullptr)), on_activity);
    if (!count) {
        return failed(outcome, count.error().to_string());
    }
    info(ST, "Downloaded {} activities from Garmin Connect", *count);
    outcome.count = *count;
    return outcome;
}

SourceOutcome download_from_wahoo(pSweatTrails const &the, std::optional<std::string> const &client_id, int days)
{
    SourceOutcome outcome { .source = "Wahoo" };
    if (!client_id) {
        outcome.kind = SourceOutcome::Kind::NotSetUp;
        outcome.message = "Wahoo isn't set up. Register an app with Wahoo, and put its client ID in the settings, as "
                          "\"wahoo\": { \"client_id\": \"...\" }. The README says how.";
        return outcome;
    }
    auto const tokens_file = wahoo_tokens_file(the->user_config_dir);
    Wahoo      wahoo {
        .fetch = http_fetch,
        .client_id = *client_id,
        .tokens_file = tokens_file,
        .tokens = read_tokens(tokens_file),
    };
    auto const on_workout = [&the](size_t index, size_t count) {
        the->notify_latest<NotificationType::Downloading>(Downloading { "Wahoo", index, count });
    };
    auto const since = static_cast<i64>(std::time(nullptr)) - static_cast<i64>(days) * 24 * 60 * 60;
    auto const count = download_new_workouts(wahoo, the->storage, since, on_workout);
    if (!count && count.error().kind == WahooError::Kind::NotLoggedIn) {
        outcome.kind = SourceOutcome::Kind::NeedsLogin;
        return outcome;
    }
    if (!count) {
        return failed(outcome, count.error().to_string());
    }
    info(ST, "Downloaded {} workouts from Wahoo", *count);
    outcome.count = *count;
    return outcome;
}

// Downloads from the sources the request asks for, one after the other,
// and tells the UI what they brought in. If anything came in, the inbox is
// processed and the newest activity shown.
void run_downloads(pSweatTrails const &the, DownloadRequest const &request)
{
    std::vector<SourceOutcome> outcomes;
    if (request.garmin) {
        outcomes.push_back(download_from_garmin(the, request.garmin_days));
    }
    if (request.wahoo) {
        outcomes.push_back(download_from_wahoo(the, request.wahoo_client_id, request.wahoo_days));
    }
    // What was downloaded before an error is in the inbox too.
    auto const brought_any = std::ranges::any_of(outcomes, [](SourceOutcome const &outcome) {
        return outcome.kind == SourceOutcome::Kind::Failed || (outcome.kind == SourceOutcome::Kind::Downloaded && outcome.count > 0);
    });
    if (brought_any) {
        the->schedule<JobType::ProcessInbox>(std::monostate { });
        the->schedule<JobType::FindNewestActivity>(std::monostate { });
    }
    the->notify<NotificationType::DownloadDone>(download_report(outcomes, request.quiet));
}

}

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

void SweatTrails::job_process_inbox(Job::Payload const &)
{
    // Files are processed faster than the UI handles notifications, so each
    // update replaces the previous one if the UI hasn't got to it yet.
    auto const on_file = [](std::string_view const &file_name, size_t index, size_t count) {
        trace(ST, "Processing inbox file {}", file_name);
        SweatTrails::the()->notify_latest<NotificationType::ImportingFile>(
            ImportingFile { std::string { file_name }, index, count });
    };
    auto res = SweatTrails::the()->storage.process_inbox(on_file);
    SweatTrails::the()->notify_latest<NotificationType::ImportingFile>(ImportingFile { });
    if (!res) {
        SweatTrails::the()->notify<NotificationType::Message>(
            std::format("Error processing inbox: {}", res.error().to_string()));
        return;
    }
    info(ST, "Processed {} inbox files", res.value());
}

void SweatTrails::job_download_tiles(Job::Payload const &data)
{
    auto const &download = std::get<pTileDownload>(data);
    auto const &storage = SweatTrails::the()->storage;
    auto const  failed = fetch_tiles(
        *download,
        [&storage](Tile const &tile) {
            return tile.get_tile(storage);
        },
        [&download](size_t index, std::string png) {
            SweatTrails::the()->notify<NotificationType::MapTile>(
                MapTile { download, index, std::move(png) });
        });
    if (!download->cancelled) {
        SweatTrails::the()->notify<NotificationType::MapTilesDone>(MapTilesDone { download, failed });
    }
}

void SweatTrails::job_download(Job::Payload const &data)
{
    run_downloads(SweatTrails::the(), std::get<DownloadRequest>(data));
}

void SweatTrails::job_wahoo_login(Job::Payload const &data)
{
    auto const &login = std::get<WahooLogin>(data);
    auto const  the = SweatTrails::the();
    Wahoo       wahoo {
        .fetch = http_fetch,
        .client_id = login.client_id,
        .tokens_file = wahoo_tokens_file(the->user_config_dir),
    };
    if (auto const exchanged = wahoo.exchange_code(login.code, login.verifier); !exchanged) {
        the->notify<NotificationType::WahooLoginEnded>(WahooLoginEnded { exchanged.error().to_string() });
        return;
    }
    info(ST, "Logged in to Wahoo");
    the->notify<NotificationType::WahooLoginEnded>(WahooLoginEnded { });
    // What the user logged in for. The report says how it went.
    run_downloads(the, DownloadRequest { .wahoo = true, .wahoo_days = login.days, .wahoo_client_id = login.client_id });
}

pSweatTrails SweatTrails::the()
{
    return std::dynamic_pointer_cast<SweatTrails>(App::the());
}

SweatTrails::SweatTrails()
    : Main<Job, Notification>(SWEATTRAILS_ID)
{
    system_config_dir = fs::path { SWEATTRAILS_DATADIR };
#ifndef __APPLE__
    // macOS windows have no icon: the Dock and Finder show the bundle's.
    icon_file = SWEATTRAILS_DATADIR "/icons/sweattrails.png";
#endif
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
    // Not waiting for a browser that's not coming back.
    wahoo_login.request_stop();
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

// Downloads new activities from the sources asked for. Arguments: `days`,
// how far back to look, instead of the settings, and `quiet`, as at
// startup.
void start_download(pSweatTrails const &sweattrails, JSONValue const &arguments, bool garmin, bool wahoo)
{
    // Not under a modal, as in cmd_quit, and not while a download runs:
    // another would only list what this one is downloading.
    if (!sweattrails->modals.empty() || sweattrails->downloading) {
        return;
    }
    sweattrails->downloading = true;
    sweattrails->schedule<JobType::Download>(make_download_request(sweattrails->settings, arguments, garmin, wahoo));
}

void cmd_download(pSweatTrails const &sweattrails, JSONValue const &arguments)
{
    start_download(sweattrails, arguments, true, true);
}

void cmd_garmin_download(pSweatTrails const &sweattrails, JSONValue const &arguments)
{
    start_download(sweattrails, arguments, true, false);
}

void cmd_wahoo_download(pSweatTrails const &sweattrails, JSONValue const &arguments)
{
    start_download(sweattrails, arguments, false, true);
}

// Logs in to Wahoo: opens the browser at Wahoo's login page, and waits, on a
// thread of its own, for the browser to come back with the code. Waiting as
// a job would hold up the other jobs, such as the inbox, for as long as the
// user takes to log in.
void cmd_wahoo_login(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (!sweattrails->modals.empty() || sweattrails->wahoo_logging_in) {
        return;
    }
    auto const client_id = wahoo_client_id(sweattrails->settings);
    if (!client_id) {
        message_box("Wahoo isn't set up. Register an app with Wahoo, and put its client ID in the settings, as "
                    "\"wahoo\": { \"client_id\": \"...\" }. The README says how.");
        return;
    }
    auto const &config_dir = sweattrails->user_config_dir;
    auto        tls = TlsContext::load(certificate_file(config_dir), certificate_key_file(config_dir));
    if (!tls) {
        warning(ST, "No certificate for the Wahoo login: {}", tls.error().to_string());
        message_box(std::format(
            "Logging in to Wahoo needs a certificate for localhost. Make one with mkcert: run `mkcert -install` once, "
            "and then, in {}, `mkcert -cert-file localhost.pem -key-file localhost-key.pem localhost 127.0.0.1 ::1`.",
            config_dir.string()));
        return;
    }
    auto server = LoopbackServer::listen(WahooCallbackPort, std::move(*tls));
    if (!server) {
        message_box(std::format("Could not wait for the Wahoo login: {}. Is another program using port {}?",
            server.error().to_string(), WahooCallbackPort));
        return;
    }
    auto const days = make_download_request(sweattrails->settings, JSONValue { }, false, true).wahoo_days;
    auto const pkce = Pkce::make();
    auto const state = random_token(16);
    auto const url = Wahoo { .client_id = *client_id }.authorize_url(pkce.challenge, state);
    if (auto const opened = open_browser(url); !opened) {
        message_box(std::format("Could not open the browser for the Wahoo login: {}", opened.error().to_string()));
        return;
    }
    sweattrails->wahoo_logging_in = true;
    sweattrails->set_status("Waiting for the Wahoo login in your browser");
    sweattrails->wahoo_login = std::jthread {
        [server = std::move(*server), client_id = *client_id, pkce, state, days](std::stop_token const &stop) mutable {
            auto const redirect = server.wait_for_redirect(WahooCallbackPath, state, WahooLoginTimeout, stop);
            // The app is quitting, and may be gone already: whatever the
            // browser brought, leave the app alone.
            if (stop.stop_requested()) {
                return;
            }
            auto const the = SweatTrails::the();
            auto const code = login_code(redirect);
            if (!code) {
                the->notify<NotificationType::WahooLoginEnded>(WahooLoginEnded { code.error() });
                return;
            }
            the->schedule<JobType::WahooLogin>(WahooLogin {
                .client_id = client_id,
                .code = *code,
                .verifier = pkce.verifier,
                .days = days,
            });
        }
    };
}

void cmd_list_month(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (!sweattrails->modals.empty()) {
        // Not while any modal is open, as in cmd_quit. In particular, Ctrl-M
        // with the list already open shouldn't open another.
        return;
    }
    auto activity_selected = [sweattrails](std::shared_ptr<ActivitySelector> const &selector, ActivityID const &id) -> void {
        auto activity = MUST_EVAL(id.load(selector->storage, LoadingDepth::Deep));
        sweattrails->show_activity(*activity);
        selector->status = ModalStatus::Dismissed;
    };
    auto const &selector = Widget::make<ActivitySelector>(
        activity_selected,
        sweattrails->last_activity.month,
        sweattrails->storage);
    selector->show();
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

void SweatTrails::notif_importing_file(Notification::Payload const &data)
{
    auto const &file = std::get<ImportingFile>(data);
    SweatTrails::the()->set_status(file.file_name.empty()
            ? std::string { }
            : std::format("Importing {} of {}: {}", file.index + 1, file.count, file.file_name));
}

void SweatTrails::notif_downloading(Notification::Payload const &data)
{
    auto const &download = std::get<Downloading>(data);
    SweatTrails::the()->set_status(std::format("Downloading {} of {} from {}", download.index + 1, download.count, download.source));
}

void SweatTrails::notif_download_done(Notification::Payload const &data)
{
    auto const &report = std::get<DownloadReport>(data);
    auto const  the = SweatTrails::the();
    the->downloading = false;
    the->set_status("");
    // Not while a login is waiting for the browser already.
    if (report.ask_login && !the->wahoo_logging_in) {
        auto const log_in = [](pSweatTrails const &sweattrails, QueryOption selection) {
            if (selection == QueryOption::QueryOptionYes) {
                sweattrails->bus.submit("st-wahoo-login", JSONValue { });
            }
        };
        query_box(the, std::format("{} Log in to Wahoo now?", report.text), log_in, QueryOptionYesNo);
        return;
    }
    if (!report.text.empty()) {
        message_box(report.text);
    }
}

void SweatTrails::notif_wahoo_login_ended(Notification::Payload const &data)
{
    auto const &ended = std::get<WahooLoginEnded>(data);
    auto const  the = SweatTrails::the();
    the->wahoo_logging_in = false;
    the->set_status("");
    if (!ended.message.empty()) {
        message_box(ended.message);
    }
}

void SweatTrails::notif_map_tile(Notification::Payload const &data)
{
    SweatTrails::the()->activity_display->map->add_tile(std::get<MapTile>(data));
}

void SweatTrails::notif_map_tiles_done(Notification::Payload const &data)
{
    SweatTrails::the()->activity_display->map->tiles_done(std::get<MapTilesDone>(data));
}

void SweatTrails::show_activity(Activity const &activity)
{
    trace(ST, "Showing activity {}", activity.id);
    last_activity = activity.id;
    auto display = widget_stack->activate<ActivityDisplay>();
    assert(display != nullptr);
    display->set_activity(activity);
}

void SweatTrails::initialize()
{
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
    storage = MUST_EVAL(Storage::init(app_name));

    widget_stack = add_widget<WidgetStack>();
    activity_display = widget_stack->add_widget<ActivityDisplay>();
    // A status line at the bottom of the window, hidden while it's empty.
    status = add_widget<Label>("");
    status->policy = SizePolicy::Hide;
    status->background = DARKGRAY;

    bus.add_task<SweatTrails>("st-force-quit", self(), cmd_force_quit)
        .bind(KeyCombo { KEY_Q, KModControl | KModShift });
    bus.add_task<SweatTrails>("st-quit", self(), cmd_quit)
        .bind(KeyCombo { KEY_Q, KModControl });
    bus.add_task<SweatTrails>("st-list-month", self(), cmd_list_month)
        .bind(KeyCombo { KEY_M, KModControl });
    bus.add_task<SweatTrails>("st-download", self(), cmd_download)
        .bind(KeyCombo { KEY_G, KModControl });
    bus.add_task<SweatTrails>("st-garmin-download", self(), cmd_garmin_download);
    bus.add_task<SweatTrails>("st-wahoo-download", self(), cmd_wahoo_download);
    // No key: Ctrl+G offers it when it's needed.
    bus.add_task<SweatTrails>("st-wahoo-login", self(), cmd_wahoo_login);
    bus.add_task<SweatTrails>("st-message", self(), cmd_message);

    // Jobs run one at a time in the order they're scheduled: the newest
    // activity shows before Garmin Connect or Wahoo is asked for anything,
    // which can take a while, and again after the download if it brought
    // anything.
    bus.schedule(make_job<JobType::ProcessInbox>(std::monostate { }));
    bus.schedule(make_job<JobType::FindNewestActivity>(std::monostate { }));
    auto quiet = JSONValue::object();
    quiet["quiet"] = JSONValue { true };
    bus.submit("st-download", quiet);
}

EError SweatTrails::read_settings()
{
    if (!settings.is_null()) {
        return { };
    }
    settings = JSONValue::object();
    settings["appearance"] = JSONValue::object();

    auto merge_settings = [this](fs::path const &dir, std::string const &file = "settings.json") -> EError {
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

// TODO: This, like initialize(), read_settings() and load_font(), uses
// std::filesystem calls that throw, which end the app on e.g. an unreadable
// font directory. Use the error_code overloads, as the job thread code in
// Storage does.
StringList SweatTrails::get_font_dirs()
{
    if (font_dirs.empty()) {
        auto &appearance = settings["appearance"];
        auto &directories = appearance["font_directories"];

        auto const home = home_dir().string();
        auto const append_dir = [&home, this](std::string_view const &dir) -> void {
            std::string d { dir };
            replace_all(d, "${HOME}", home);
            replace_all(d, "${sweattrails_DATADIR}", SWEATTRAILS_DATADIR);
            if (std::find(font_dirs.begin(), font_dirs.end(), d) == font_dirs.end()) {
                if (!fs::is_directory(d)) {
                    return;
                }
                font_dirs.push_back(d);
                for (auto const &dir_entry : fs::recursive_directory_iterator(d)) {
                    if (dir_entry.is_directory()) {
                        font_dirs.push_back(dir_entry.path().string());
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

// Must be called on the UI thread; jobs send a notification instead.
void SweatTrails::set_status(std::string_view const &text)
{
    status->text = text;
    auto const policy = text.empty() ? SizePolicy::Hide : SizePolicy::Characters;
    if (status->policy != policy) {
        status->policy = policy;
        resize();
    }
}

}

int main(int argc, char const **argv)
{
    auto sweattrails = ST::App::create<ST::SweatTrails>(argc, argv);
    sweattrails->start();
    return 0;
}
