/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <set>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include <Format.h>
#include <JSON.h>

#include <App.h>

#include <Zwift.h>
#include <overlay/OverlayRender.h>
#include <storage/Activity.h>
#include <storage/Download.h>
#include <storage/Garmin.h>
#include <storage/Storage.h>
#include <storage/Strava.h>
#include <storage/Summaries.h>
#include <storage/Types.h>
#include <storage/Wahoo.h>
#include <widget/Activity.h>
#include <widget/Summaries.h>

namespace ST {

struct SettingsError {
    explicit SettingsError(std::string_view const &e)
        : error(e)
    {
    }

    std::string error;
};

class STError {
public:
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
#define MESSAGE(N, T, VALUES) using N = Message < T, std::monostate,

// What the Wahoo login brought back from the browser, for the job that
// trades it for tokens.
struct WahooLogin {
    std::string client_id { };
    std::string code { };
    std::string verifier { };
    int         days { WahooDefaultDays }; // For the download after the login
};

// The st-strava-titles task: the titles of the last `days`, or, without,
// of the store's whole history.
struct StravaTitlesRequest {
    std::optional<int>          days { };
    std::optional<StravaClient> client { };
};

// What the Strava login brought back from the browser, and the titles to
// fetch after it.
struct StravaLogin {
    std::string         code { };
    StravaTitlesRequest titles { };
};

// The UpdateSummaries job, a chunk at a time: how many files the chunks
// before it read.
struct SummaryProgress {
    size_t done { 0 };
};

#define JOBTYPE(S)                        \
    S(FindCurrentFolder, std::monostate)  \
    S(FindNewestActivity, std::monostate) \
    S(ProcessInbox, std::monostate)       \
    S(DownloadTiles, pTileDownload)       \
    S(Download, DownloadRequest)          \
    S(WahooLogin, WahooLogin)             \
    S(StravaTitles, StravaTitlesRequest)  \
    S(StravaLogin, StravaLogin)           \
    S(PrepareZwiftWorld, ZwiftWorld)      \
    S(UpdateSummaries, SummaryProgress)

enum class JobType {
    JOBTYPE(ENUMVALUE2)
        Count,
};

template<>
char const *value_to_string(JobType type);

using Job = Message<
    JobType,
    std::monostate,
    JOBTYPE(ENUMPAYLOAD)
        std::monostate>;

// Sent by the inbox job for each file it processes, and with an empty
// file_name when it's done.
struct ImportingFile {
    std::string file_name { };
    size_t      index { 0 };
    size_t      count { 0 };
};

// Sent by a download before each activity.
struct Downloading {
    std::string source { }; // "Garmin Connect", "Wahoo", "Strava"
    size_t      index { 0 };
    size_t      count { 0 };
};

// Sent when a login is over, however it ended, with what to tell the
// user, if anything.
struct LoginEnded {
    std::string message { };
    bool        downloading { false }; // The login worked, and a download follows
};

// Sent when Strava has changed the names or notes of activities, by their
// start times.
struct TitlesChanged {
    std::vector<i64> starts { };
};

// Sent by the UpdateSummaries job after each chunk: the summaries so far,
// how many of the files to read it has read, and what went wrong, if
// anything did.
struct SummariesUpdated {
    ActivitySummaries summaries { };
    size_t            done { 0 };
    size_t            total { 0 };
    std::string       error { }; // Why it stopped early, for the status line
};

#define NOTIFICATIONTYPE(S)             \
    S(Message, std::string)             \
    S(ActivityList, Activities)         \
    S(Activity, Activity)               \
    S(ImportingFile, ImportingFile)     \
    S(MapTile, MapTile)                 \
    S(MapTilesDone, MapTilesDone)       \
    S(Downloading, Downloading)         \
    S(DownloadDone, DownloadReport)     \
    S(LoginEnded, LoginEnded)           \
    S(TitlesChanged, TitlesChanged)     \
    S(ZwiftWorldReady, ZwiftWorldReady) \
    S(SummariesUpdated, SummariesUpdated)

enum class NotificationType {
    NOTIFICATIONTYPE(ENUMVALUE2)
        Count,
};

using Notification = Message<
    NotificationType,
    std::monostate,
    NOTIFICATIONTYPE(ENUMPAYLOAD)
        std::monostate>;

template<>
char const *value_to_string(NotificationType type);

struct SweatTrails : public Main<Job, Notification> {
    static void job_find_current_folder(Job::Payload const &data);
    static void job_find_newest_activity(Job::Payload const &data);
    static void job_process_inbox(Job::Payload const &data);
    static void job_download_tiles(Job::Payload const &data);
    static void job_download(Job::Payload const &data);
    static void job_wahoo_login(Job::Payload const &data);
    static void job_strava_titles(Job::Payload const &data);
    static void job_strava_login(Job::Payload const &data);
    static void job_prepare_zwift_world(Job::Payload const &data);
    static void job_update_summaries(Job::Payload const &data);

    // The payload variant starts with a std::monostate, so a job type's
    // payload comes one after its value.
    template<JobType Type>
    static Job make_job(auto data)
    {
        return Job::make<Type>(
            Job::Payload { std::in_place_index<static_cast<size_t>(Type) + 1>, data },
            {
                job_find_current_folder,
                job_find_newest_activity,
                job_process_inbox,
                job_download_tiles,
                job_download,
                job_wahoo_login,
                job_strava_titles,
                job_strava_login,
                job_prepare_zwift_world,
                job_update_summaries,
            });
    }

    template<JobType Type>
    void schedule(auto data)
    {
        bus.schedule(make_job<Type>(data));
    }

    static void notif_message(Notification::Payload const &data);
    static void notif_activity_list(Notification::Payload const &data);
    static void notif_activity(Notification::Payload const &data);
    static void notif_importing_file(Notification::Payload const &data);
    static void notif_map_tile(Notification::Payload const &data);
    static void notif_map_tiles_done(Notification::Payload const &data);
    static void notif_downloading(Notification::Payload const &data);
    static void notif_download_done(Notification::Payload const &data);
    static void notif_login_ended(Notification::Payload const &data);
    static void notif_titles_changed(Notification::Payload const &data);
    static void notif_zwift_world_ready(Notification::Payload const &data);
    static void notif_summaries_updated(Notification::Payload const &data);

    template<NotificationType Type>
    static Notification make_notification(auto data)
    {
        return Notification::make<Type>(
            data,
            {
                notif_message,
                notif_activity_list,
                notif_activity,
                notif_importing_file,
                notif_map_tile,
                notif_map_tiles_done,
                notif_downloading,
                notif_download_done,
                notif_login_ended,
                notif_titles_changed,
                notif_zwift_world_ready,
                notif_summaries_updated,
            });
    }

    template<NotificationType Type>
    void notify(auto data)
    {
        bus.notify(make_notification<Type>(data));
    }

    template<NotificationType Type>
    void notify_latest(auto data)
    {
        bus.notify_latest(make_notification<Type>(data));
    }

    std::optional<std::string_view> message = { };

    Storage    storage;
    ActivityID last_activity { };
    // Between a download task and the end of its job. Only used on the UI
    // thread.
    bool downloading { false };
    // The service whose login waits for the browser, "Wahoo" or "Strava",
    // from its login task until the login is over: one at a time, as they
    // share the port. Only used on the UI thread.
    std::string logging_in { };
    // Waits for the browser to come back from the login.
    std::jthread login { };
    // The overlay's render to video, while it runs. Only used on the UI
    // thread, where it draws.
    std::unique_ptr<OverlayRender> overlay_render { nullptr };
    int                            render_percent { -1 };    // As the status line says it
    bool                           render_waiting { false }; // For tiles, as the status line says

    // From share/zwift-worlds.json; none if it can't be read.
    std::vector<ZwiftWorld> zwift_worlds { };
    // The worlds being cut, and the ones that failed, which aren't tried
    // again until the app restarts. By cache name; only used on the UI
    // thread.
    std::set<std::string> zwift_preparing { };
    std::set<std::string> zwift_failed { };

    // The summaries the UpdateSummaries job read last. Only used on the UI
    // thread.
    ActivitySummaries summaries { };
    // From scheduling the UpdateSummaries job until its last chunk, so that
    // there's one at a time. Only used by jobs, and to schedule one.
    std::atomic<bool> updating_summaries { false };
    // The status line says how far it is.
    bool reading_summaries { false };

    std::shared_ptr<WidgetStack>     widget_stack { nullptr };
    std::shared_ptr<ActivityDisplay> activity_display { nullptr };
    std::shared_ptr<SummariesScreen> summaries_screen { nullptr };
    std::string                      thresholds_error { }; // Why the settings' thresholds can't be read
    std::string                      settings_error { };   // Why a settings file can't be read
    std::shared_ptr<Label>           status { nullptr };

    SweatTrails();
    static pSweatTrails the();

    void   initialize() override;
    void   process_input() override;
    bool   query_close() override;
    void   on_start() override;
    void   on_resize() override;
    void   on_terminate() override;
    EError read_settings();
    void   load_font();
    // The font file of this name in the font directories, if there is one.
    std::optional<fs::path> find_font(std::string_view name);
    // The pictures next to a layout, or with the built-in ones.
    static ImageFinder image_finder();
    StringList         get_font_dirs();
    void               terminate();
    void               set_message(std::string_view const &text);
    // An empty text, while a login waits, shows that it waits.
    void set_status(std::string_view const &text);
    void show_activity(Activity const &activity);
    // Has the job read what's new in the store for the summaries, unless
    // it's doing that already.
    void               update_summaries();
    [[nodiscard]] bool showing_summaries() const;
    void               show_summaries(bool on);
    // Where Zwift worlds' maps are cut into tiles.
    [[nodiscard]] fs::path zwift_cache() const { return storage.tiles_dir / "zwift"; }
    // Has the job cut the world into tiles, unless it's doing that already,
    // or failed to before.
    void prepare_zwift_world(ZwiftWorld const &world);
    // Renders a few of the overlay's frames, and says how far it is.
    void step_render();
    void stop_render();
};

}

inline std::ostream &operator<<(std::ostream &os, ST::JobType type)
{
    os << ST::value_to_string(type);
    return os;
}

inline std::ostream &operator<<(std::ostream &os, ST::NotificationType type)
{
    os << ST::value_to_string(type);
    return os;
}
