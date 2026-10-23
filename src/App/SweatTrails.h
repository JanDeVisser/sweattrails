/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string_view>
#include <utility>
#include <variant>

#include <Format.h>
#include <JSON.h>

#include <App.h>

#include <storage/Activity.h>
#include <storage/Garmin.h>
#include <storage/Storage.h>
#include <storage/Types.h>
#include <widget/Activity.h>

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

#define JOBTYPE(S)                        \
    S(FindCurrentFolder, std::monostate)  \
    S(FindNewestActivity, std::monostate) \
    S(ProcessInbox, std::monostate)       \
    S(DownloadTiles, pTileDownload)       \
    S(DownloadGarmin, GarminDownloadRequest)

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

// Sent by the Garmin Connect download before each activity, and with a
// count of 0 when it's done.
struct Downloading {
    size_t index { 0 };
    size_t count { 0 };
};

#define NOTIFICATIONTYPE(S)         \
    S(Message, std::string)         \
    S(ActivityList, Activities)     \
    S(Activity, Activity)           \
    S(ImportingFile, ImportingFile) \
    S(MapTile, MapTile)             \
    S(MapTilesDone, MapTilesDone)   \
    S(Downloading, Downloading)

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
    static void job_download_garmin(Job::Payload const &data);

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
                job_download_garmin,
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

    std::shared_ptr<WidgetStack>     widget_stack { nullptr };
    std::shared_ptr<ActivityDisplay> activity_display { nullptr };
    std::shared_ptr<Label>           status { nullptr };

    SweatTrails();
    static pSweatTrails the();

    void       initialize() override;
    bool       query_close() override;
    void       on_start() override;
    void       on_resize() override;
    void       on_terminate() override;
    EError     read_settings();
    void       load_font();
    StringList get_font_dirs();
    void       terminate();
    void       set_message(std::string_view const &text);
    void       set_status(std::string_view const &text);
    void       show_activity(Activity const &activity);
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
