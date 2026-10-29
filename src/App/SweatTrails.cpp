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
#include <overlay/LayoutFile.h>
#include <overlay/OverlayRender.h>
#include <overlay/Render.h>
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

constexpr std::string_view WaitingForLoginStatus = "Waiting for the Wahoo login in your browser";

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
    auto        failed = fetch_tiles(
        *download,
        [&storage](Tile const &tile) {
            return tile.get_tile(storage);
        },
        [&download](Tile const &tile, std::string png) {
            SweatTrails::the()->notify<NotificationType::MapTile>(
                MapTile { download, tile, std::move(png) });
        });
    SweatTrails::the()->notify<NotificationType::MapTilesDone>(MapTilesDone { download, std::move(failed) });
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
    the->notify<NotificationType::WahooLoginEnded>(WahooLoginEnded { .downloading = true });
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
    // While there's an OpenGL context to let go of the render texture in.
    // A render that isn't done stops, and its file is removed.
    overlay_render.reset();
}

void SweatTrails::process_input()
{
    Main<Job, Notification>::process_input();
    step_render();
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
    auto const *prompt = sweattrails->overlay_render
        ? "An overlay is rendering, and quitting stops it. Are you sure you want to quit?"
        : "Are you sure you want to quit?";
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
    sweattrails->set_status(WaitingForLoginStatus);
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

// The activity on display, for the keys that act on it, such as the lap
// keys: none under a modal, as in cmd_quit, or without an activity.
std::shared_ptr<DisplayedActivity> activity_for_keys(pSweatTrails const &sweattrails)
{
    if (!sweattrails->modals.empty()) {
        return nullptr;
    }
    return sweattrails->activity_display->activity;
}

void cmd_next_lap(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (auto activity = activity_for_keys(sweattrails)) {
        activity->next_lap();
    }
}

void cmd_previous_lap(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (auto activity = activity_for_keys(sweattrails)) {
        activity->previous_lap();
    }
}

// The activity in full, or why it can't be shown: it can list, but fail
// to load all of it, or be gone since it was listed.
std::expected<Activity, std::string> load_to_show(Storage &storage, ActivityID const &id)
{
    auto const loaded = id.load(storage, LoadingDepth::Deep);
    if (!loaded) {
        return std::unexpected(std::format("`{}` can't be loaded: {}", id.name, loaded.error().to_string()));
    }
    if (!*loaded) {
        return std::unexpected(std::format("`{}` isn't there any more", id.name));
    }
    return **loaded;
}

// The activity before (`step` -1) or after (+1) the one on display, in
// another month if need be, as choosing it in Ctrl+M's list shows it. At
// the first and the last, nothing. One that doesn't load is passed over,
// and named, so that it doesn't stand in the way.
void show_activity_next_to(pSweatTrails const &sweattrails, int step)
{
    auto const activity = activity_for_keys(sweattrails);
    if (activity == nullptr) {
        return;
    }
    std::vector<std::string> skipped;
    auto                     id = activity->activity.id;
    while (true) {
        auto const next = sweattrails->storage.activity_next_to(id, step);
        if (!next) {
            skipped.push_back(std::format("The activities can't be listed: {}", next.error().to_string()));
            break;
        }
        if (!*next) {
            break;
        }
        auto loaded = load_to_show(sweattrails->storage, **next);
        if (loaded) {
            sweattrails->show_activity(*loaded);
            break;
        }
        skipped.push_back(loaded.error());
        id = **next;
    }
    if (!skipped.empty()) {
        message_box(join(skipped, "\n"));
    }
}

void cmd_next_activity(pSweatTrails const &sweattrails, JSONValue const &)
{
    show_activity_next_to(sweattrails, 1);
}

void cmd_previous_activity(pSweatTrails const &sweattrails, JSONValue const &)
{
    show_activity_next_to(sweattrails, -1);
}

void cmd_whole_activity(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (auto activity = activity_for_keys(sweattrails)) {
        activity->whole_activity();
    }
}

// The map, for the map keys: none under a modal, as for the lap keys.
// ActivityMap does nothing without a map of its own.
std::shared_ptr<ActivityMap> map_keys_map(pSweatTrails const &sweattrails)
{
    if (!sweattrails->modals.empty()) {
        return nullptr;
    }
    return sweattrails->activity_display->map;
}

void cmd_map_zoom_in(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (auto map = map_keys_map(sweattrails)) {
        map->zoom_by(1);
    }
}

void cmd_map_zoom_out(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (auto map = map_keys_map(sweattrails)) {
        map->zoom_by(-1);
    }
}

void cmd_map_whole_route(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (auto map = map_keys_map(sweattrails)) {
        map->show_whole_route();
    }
}

// Opens a box with the activity's name. A name that can't be saved keeps
// the title it had, and the status line says why.
void cmd_rename_activity(pSweatTrails const &sweattrails, JSONValue const &)
{
    auto activity = activity_for_keys(sweattrails);
    if (!activity) {
        return;
    }
    auto save = [activity](pSweatTrails const &st, std::string const &name) {
        if (auto const renamed = activity->activity.rename(name); !renamed) {
            st->set_status(std::format("Couldn't save the name: {}", renamed.error().to_string()));
        }
    };
    input_box(sweattrails, "Name", save, activity->activity.title);
}

// The layout the preview shows before the user picks one.
constexpr char const DefaultOverlayLayout[] = "cycling-basic";

fs::path overlay_state_file(pSweatTrails const &sweattrails)
{
    return user_state_dir(sweattrails->app_name) / "overlay.json";
}

// The built-in layouts, and the user's in the data directory.
std::vector<LayoutEntry> overlay_layouts(pSweatTrails const &sweattrails)
{
    return overlay_layouts(fs::path { SWEATTRAILS_DATADIR } / "overlays", sweattrails->storage.overlays_dir);
}

std::optional<LayoutEntry> overlay_layout(pSweatTrails const &sweattrails, std::string_view name)
{
    for (auto const &layout : overlay_layouts(sweattrails)) {
        if (layout.name == name) {
            return layout;
        }
    }
    return { };
}

// The layout picked last, or the default if there's none, or it's gone.
std::optional<LayoutEntry> chosen_overlay_layout(pSweatTrails const &sweattrails)
{
    if (auto const chosen = read_chosen_layout(overlay_state_file(sweattrails)); chosen) {
        if (auto layout = overlay_layout(sweattrails, *chosen); layout) {
            return layout;
        }
    }
    return overlay_layout(sweattrails, DefaultOverlayLayout);
}

void cmd_overlay_preview(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (!activity_for_keys(sweattrails)) {
        return;
    }
    auto const &display = sweattrails->activity_display;
    if (display->previewing()) {
        display->show_preview(false);
        return;
    }
    if (!display->preview->file) {
        if (auto const layout = chosen_overlay_layout(sweattrails); layout) {
            display->preview->set_layout(layout->path);
        }
    }
    display->show_preview(true);
}

// Picking a layout shows it, and it's the one shown after a restart.
void cmd_overlay_layout(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (!activity_for_keys(sweattrails)) {
        return;
    }
    std::vector<std::pair<std::string, std::string>> entries;
    for (auto const &layout : overlay_layouts(sweattrails)) {
        entries.emplace_back(layout.name, layout.name);
    }
    if (entries.empty()) {
        sweattrails->set_status("There are no overlay layouts");
        return;
    }
    auto const current = chosen_overlay_layout(sweattrails);
    auto       pick = [](pSweatTrails const &st, std::string const &name) {
        auto const layout = overlay_layout(st, name);
        if (!layout) {
            return;
        }
        if (auto const saved = save_chosen_layout(overlay_state_file(st), name); !saved) {
            st->set_status(std::format("Couldn't keep the choice of layout: {}", saved.error().to_string()));
        }
        st->activity_display->preview->set_layout(layout->path);
        st->activity_display->show_preview(true);
    };
    list_box(sweattrails, "Overlay layout", entries, pick, current ? current->name : "");
}

// The overlay layout the preview shows, or would show, loaded.
std::expected<LayoutFile, std::string> overlay_to_render(pSweatTrails const &sweattrails)
{
    auto const &preview = sweattrails->activity_display->preview;
    // The preview only looks for changes while it's showing.
    if (preview->file) {
        auto file = *preview->file;
        file.reload_if_changed();
        return file;
    }
    auto const chosen = chosen_overlay_layout(sweattrails);
    if (!chosen) {
        return std::unexpected("There are no overlay layouts");
    }
    LayoutFile file { chosen->path };
    file.reload_if_changed();
    return file;
}

// Where ffmpeg is: an app opened from the Finder gets a PATH without
// Homebrew's directories.
std::optional<fs::path> find_ffmpeg()
{
    auto const *path = std::getenv("PATH");
    return find_program("ffmpeg", (path != nullptr) ? path : "", { "/opt/homebrew/bin", "/usr/local/bin" });
}

void start_render(pSweatTrails const &sweattrails, RenderJob job)
{
    auto render = OverlayRender::start(
        std::move(job),
        [](std::string_view name) { return SweatTrails::the()->find_font(name); },
        SweatTrails::image_finder(),
        [](Tile const &tile) { return tile.get_cached_tile(SweatTrails::the()->storage); },
        [](pTileDownload const &download) { SweatTrails::the()->schedule<JobType::DownloadTiles>(download); });
    if (!render) {
        sweattrails->set_status(std::format("Couldn't render the overlay: {}", render.error()));
        return;
    }
    sweattrails->overlay_render = std::move(*render);
    sweattrails->render_percent = -1;
    sweattrails->step_render();
}

// A range longer than this asks first, with the file's size.
constexpr double LongRender = 10 * 60;

// Renders the activity's overlay to video, for the range the user picks,
// starting with the graph's selection or lap. During a render, asks
// whether to stop it.
void cmd_overlay_render(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (sweattrails->overlay_render) {
        if (!sweattrails->modals.empty()) {
            return;
        }
        auto const prompt = std::format("Stop rendering {}?", sweattrails->overlay_render->output().filename().string());
        auto const stop = [](pSweatTrails const &st, QueryOption selection) {
            if (selection == QueryOption::QueryOptionYes) {
                st->stop_render();
            }
        };
        query_box(sweattrails, prompt, stop, QueryOptionYesNo);
        return;
    }
    auto activity = activity_for_keys(sweattrails);
    if (!activity) {
        return;
    }
    auto const file = overlay_to_render(sweattrails);
    if (!file) {
        sweattrails->set_status(file.error());
        return;
    }
    if (!file->layout) {
        sweattrails->set_status(std::format("The overlay layout {} has an error: the preview (Ctrl+O) shows it", file->path.filename().string()));
        return;
    }
    auto const settings = decode_render_settings(sweattrails->settings.get("overlay"), home_dir());
    if (!settings) {
        sweattrails->set_status(std::format("Can't render the overlay: in the settings, {}", settings.error()));
        return;
    }
    auto const ffmpeg = find_ffmpeg();
    if (!ffmpeg) {
        sweattrails->set_status("Rendering the overlay needs ffmpeg, which isn't installed: brew install ffmpeg");
        return;
    }
    auto const clock = activity_clock(activity->activity);
    auto const initial = initial_render_range(activity->activity, activity->selection());
    RenderJob  job {
        .activity = activity,
        .layout = *file->layout,
        .layout_file = file->path,
        .settings = *settings,
        .range = initial,
        .output = { },
        .ffmpeg = *ffmpeg,
    };
    auto picked = [job, clock](pSweatTrails const &st, std::string const &text) mutable {
        auto const range = parse_range(text, clock);
        if (!range) {
            st->set_status(range.error());
            return;
        }
        job.range = *range;
        job.output = output_path(job.settings.folder, clock, *range, [](fs::path const &p) { return fs::exists(p); });
        if (range->to - range->from <= LongRender) {
            start_render(st, std::move(job));
            return;
        }
        auto const prompt = std::format("{} will be about {:.0f} GB. Render it?", job.output.filename().string(),
            std::ceil(estimated_gigabytes(job.settings.resolution, *range)));
        auto const render = [job](pSweatTrails const &st, QueryOption selection) {
            if (selection == QueryOption::QueryOptionYes) {
                start_render(st, job);
            }
        };
        query_box(st, prompt, render, QueryOptionYesNo);
    };
    input_box(sweattrails, "Range", picked, format_range(initial));
}

void cmd_list_month(pSweatTrails const &sweattrails, JSONValue const &)
{
    if (!sweattrails->modals.empty()) {
        // Not while any modal is open, as in cmd_quit. In particular, Ctrl-M
        // with the list already open shouldn't open another.
        return;
    }
    auto const months = sweattrails->storage.months_with_activities();
    if (!months) {
        message_box(std::format("The activities can't be listed: {}", months.error().to_string()));
        return;
    }
    if (months->empty()) {
        message_box("There are no activities yet");
        return;
    }
    auto activity_selected = [sweattrails](std::shared_ptr<ActivitySelector> const &selector, ActivityID const &id) -> void {
        selector->status = ModalStatus::Dismissed;
        if (auto const activity = load_to_show(selector->storage, id); activity) {
            sweattrails->show_activity(*activity);
        } else {
            message_box(activity.error());
        }
    };
    // At the activity on display, or else at the latest month that lists
    // any.
    auto const &shown = sweattrails->last_activity;
    auto        month = shown.month;
    if (!std::ranges::binary_search(*months, shown.month)) {
        auto const latest = sweattrails->storage.listed_month(months->back(), -1, true);
        if (!latest || !*latest) {
            message_box("There are no activities yet");
            return;
        }
        month = (*latest)->id;
    }
    auto const &selector = Widget::make<ActivitySelector>(activity_selected, month, sweattrails->storage);
    selector->show();
    selector->select(shown);
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
    // The download its DownloadDone ends: until then, Ctrl+G mustn't start
    // another.
    if (ended.downloading) {
        the->downloading = true;
    }
    if (!ended.message.empty()) {
        message_box(ended.message);
    }
}

// Each of the maps that download tiles takes its own: the app's, the
// overlay preview's, and a render's.
void SweatTrails::notif_map_tile(Notification::Payload const &data)
{
    auto const  the = SweatTrails::the();
    auto const &tile = std::get<MapTile>(data);
    the->activity_display->map->add_tile(tile);
    the->activity_display->preview->tiles.add_tile(tile);
    if (the->overlay_render) {
        the->overlay_render->tiles().add_tile(tile);
    }
}

void SweatTrails::notif_map_tiles_done(Notification::Payload const &data)
{
    auto const  the = SweatTrails::the();
    auto const &done = std::get<MapTilesDone>(data);
    the->activity_display->map->tiles_done(done);
    the->activity_display->preview->tiles.tiles_done(done);
    if (the->overlay_render) {
        the->overlay_render->tiles().tiles_done(done);
    }
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
        .bind(KeyCombo { KEY_Q, KModControl | KModShift })
        .run_under_modals();
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
    // The table and the data panel follow the lap these select.
    bus.add_task<SweatTrails>("st-next-lap", self(), cmd_next_lap)
        .bind(KeyCombo { KEY_DOWN, KModNone });
    bus.add_task<SweatTrails>("st-previous-lap", self(), cmd_previous_lap)
        .bind(KeyCombo { KEY_UP, KModNone });
    // As Left and Right go through the months in Ctrl+M's list.
    bus.add_task<SweatTrails>("st-next-activity", self(), cmd_next_activity)
        .bind(KeyCombo { KEY_RIGHT, KModNone });
    bus.add_task<SweatTrails>("st-previous-activity", self(), cmd_previous_activity)
        .bind(KeyCombo { KEY_LEFT, KModNone });
    bus.add_task<SweatTrails>("st-whole-activity", self(), cmd_whole_activity)
        .bind(KeyCombo { KEY_ESCAPE, KModNone });
    // F2 rather than Ctrl+R, which often means reload, and Cmd+Shift+R
    // (Ctrl+Shift+R off macOS) for keyboards without function keys. A
    // click on the title in the data panel runs it too.
#ifdef __APPLE__
    auto const command = KModSuper;
#else
    auto const command = KModControl;
#endif
    bus.add_task<SweatTrails>("st-rename-activity", self(), cmd_rename_activity)
        .bind(KeyCombo { KEY_F2, KModNone })
        .bind(KeyCombo { KEY_R, command | KModShift });
    bus.add_task<SweatTrails>("st-overlay-preview", self(), cmd_overlay_preview)
        .bind(KeyCombo { KEY_O, KModControl });
    bus.add_task<SweatTrails>("st-overlay-layout", self(), cmd_overlay_layout)
        .bind(KeyCombo { KEY_O, KModControl | KModShift });
    bus.add_task<SweatTrails>("st-overlay-render", self(), cmd_overlay_render)
        .bind(KeyCombo { KEY_E, KModControl });
    // + is Shift = on most keyboards.
    bus.add_task<SweatTrails>("st-map-zoom-in", self(), cmd_map_zoom_in)
        .bind(KeyCombo { KEY_EQUAL, KModNone })
        .bind(KeyCombo { KEY_EQUAL, KModShift })
        .bind(KeyCombo { KEY_KP_ADD, KModNone });
    bus.add_task<SweatTrails>("st-map-zoom-out", self(), cmd_map_zoom_out)
        .bind(KeyCombo { KEY_MINUS, KModNone })
        .bind(KeyCombo { KEY_KP_SUBTRACT, KModNone });
    bus.add_task<SweatTrails>("st-map-whole-route", self(), cmd_map_whole_route)
        .bind(KeyCombo { KEY_ZERO, KModNone });

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

    auto const load = [this](std::string_view font) -> bool {
        if (auto const path = find_font(font); path) {
            set_fonts(path->string());
            return true;
        }
        return false;
    };
    if (!load(font_name) && (font_name != default_font)) {
        assert(load(default_font));
    }
}

ImageFinder SweatTrails::image_finder()
{
    return [](std::string_view src, fs::path const &layout_dir) {
        return find_image(src, layout_dir, fs::path { SWEATTRAILS_DATADIR } / "overlays", [](fs::path const &path) {
            std::error_code ec { };
            return fs::is_regular_file(path, ec);
        });
    };
}

std::optional<fs::path> SweatTrails::find_font(std::string_view name)
{
    for (auto const &dir : get_font_dirs()) {
        std::error_code ec { };
        if (auto const path = fs::path { dir } / name; fs::is_regular_file(path, ec)) {
            return path;
        }
    }
    return { };
}

void SweatTrails::set_message(std::string_view const &text)
{
    notify<NotificationType::Message>(std::string { text });
}

// At most this long a UI frame, so that the app stays usable.
constexpr double RenderBudget = 0.010;

void SweatTrails::step_render()
{
    if (!overlay_render) {
        return;
    }
    overlay_render->step(RenderBudget);
    auto const name = overlay_render->output().filename().string();
    if (!overlay_render->over()) {
        auto const percent = static_cast<int>(overlay_render->progress() * 100);
        auto const waiting = overlay_render->waiting();
        if (percent != render_percent || waiting != render_waiting) {
            render_percent = percent;
            render_waiting = waiting;
            set_status(std::format("Rendering {}: {}%{}", name, percent, waiting ? ", downloading map tiles" : ""));
        }
        return;
    }
    auto const &result = *overlay_render->result();
    auto const  failed = overlay_render->tiles().failed();
    auto const  without = (failed == 0) ? std::string { } : std::format(". Couldn't download {} map {}", failed, (failed == 1) ? "tile" : "tiles");
    set_status(result
            ? std::format("Rendered the overlay to {}{}", overlay_render->output().string(), without)
            : std::format("Couldn't render {}: {}", name, result.error()));
    overlay_render.reset();
}

void SweatTrails::stop_render()
{
    if (!overlay_render) {
        return;
    }
    overlay_render->stop();
    set_status(std::format("Stopped rendering {}, and removed it", overlay_render->output().filename().string()));
    overlay_render.reset();
}

// Must be called on the UI thread; jobs send a notification instead.
void SweatTrails::set_status(std::string_view const &status_text)
{
    // A download or the inbox may finish while a login waits.
    auto const text = (status_text.empty() && wahoo_logging_in)
        ? WaitingForLoginStatus
        : status_text;
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
