/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * What the download tasks ask for, and what they tell the user when the
 * download is done. One download job runs every source asked for, and
 * says in one message what each brought in: see specs/wahoo-download.md.
 * Strava brings in no activities, only their titles: see
 * specs/strava-titles.md.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <JSON.h>

namespace ST {

// `wahoo.client_id` from the settings: without one, the user doesn't use
// Wahoo.
std::optional<std::string> wahoo_client_id(JSONValue const &settings);

// The app the user registered with Strava.
struct StravaClient {
    std::string id { };
    std::string secret { };
};

// `strava.client_id` and `strava.client_secret` from the settings: without
// both, the user doesn't use Strava.
std::optional<StravaClient> strava_client(JSONValue const &settings);

// What a download is asked to do: the task's arguments, with the settings
// filling in what they don't say.
struct DownloadRequest {
    bool                        garmin { false };
    bool                        wahoo { false };
    bool                        strava { false }; // Titles: after the others, and the inbox
    int                         garmin_days { 30 };
    int                         wahoo_days { 30 };
    int                         strava_days { 30 };
    std::optional<std::string>  wahoo_client_id { }; // Without one, Wahoo isn't set up
    std::optional<StravaClient> strava_client { };   // Without one, Strava isn't set up
    bool                        quiet { false };     // At startup: say only what went wrong
};

// Which sources a download asks for.
struct Sources {
    bool garmin { false };
    bool wahoo { false };
    bool strava { false };
};

// A download from the sources asked for: `garmin.days`, `wahoo.days` and
// `strava.days` from the settings, unless the task's `days` argument
// overrides them all, the task's `quiet` argument, and the Wahoo and
// Strava clients from the settings.
DownloadRequest make_download_request(JSONValue const &settings, JSONValue const &arguments, Sources sources);

// How the download from one source went.
struct SourceOutcome {
    enum class Kind {
        NotSetUp,   // No Garmin key, or no Wahoo client ID
        NeedsLogin, // Wahoo or Strava, without a login it accepts
        Downloaded,
        Titled, // Strava, which brings in titles, not activities
        Failed,
    };

    std::string source { }; // "Garmin Connect", "Wahoo", "Strava"
    Kind        kind { Kind::Downloaded };
    size_t      count { 0 };        // For Downloaded: activities; for Titled: titles
    size_t      descriptions { 0 }; // For Titled
    size_t      waiting { 0 };      // For Titled: descriptions left for the next time
    std::string resets_at { };      // For Titled: when they can be fetched, such as "14:45"
    std::string message { };        // For NotSetUp and Failed: what to tell the user; for Titled, what stopped the descriptions
};

// What to tell the user when the download is done.
struct DownloadReport {
    std::string text { };      // Nothing to say if empty
    std::string ask_login { }; // The source to offer the login to, if any
};

// One sentence per source, such as "Garmin Connect: 2 new activities.
// Wahoo: nothing new." A source that isn't set up is left out, unless it's
// the only one asked for, so that a user who doesn't use Wahoo never hears
// of it. When none is, the first one's message says how to set it up.
// Quiet, only failures are reported, and the login isn't offered. Of two
// sources that need a login, the first is offered.
DownloadReport download_report(std::vector<SourceOutcome> const &outcomes, bool quiet);

}
