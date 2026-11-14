/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * What the download tasks ask for, and what they tell the user when the
 * download is done. One download job runs every source asked for, and
 * says in one message what each brought in: see specs/08-wahoo-download.md.
 * Strava brings in no activities, only their titles: see
 * specs/24-strava-titles.md.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <JSON.h>
#include <Logging.h>

namespace ST {

// `wahoo.client_id` from the settings: without one, the user doesn't use
// Wahoo.
std::optional<std::string> wahoo_client_id(JSONValue const &settings);

// The app the user registered with Strava.
struct StravaClient {
    std::string id { };
    std::string secret { }; // Empty if it's missing from the settings
};

// `strava.client_id` and `strava.client_secret` from the settings: without
// an ID, the user doesn't use Strava. With an ID and no secret, they do,
// and should hear that the secret is missing.
std::optional<StravaClient> strava_client(JSONValue const &settings);

// `strava.descriptions` from the settings: whether Strava's descriptions
// come in the background, which they do unless it's false. See
// specs/44-rate-limits.md.
bool strava_descriptions(JSONValue const &settings);

// What to tell the user about Strava's setup: how to set it up without a
// client, that the secret is missing without one, or nothing.
std::optional<std::string> strava_setup_problem(std::optional<StravaClient> const &client);

// Whether a source that brings in activities is set up: Garmin Connect
// with a key, or Wahoo with a client ID. Strava brings in only titles.
bool downloads_activities(JSONValue const &settings, bool has_garmin_key);

// How far back "Everything" goes: before any GPS watch wrote a FIT file.
constexpr int EverythingSinceYear = 1990;

// A period "Download activities from…" offers.
struct DownloadPeriod {
    std::string text { };
    int         days { 0 };
};

// The periods "Download activities from…" offers, each with the days it
// looks back from `now`, a Unix time, in local time: see
// specs/43-download-history.md.
std::vector<DownloadPeriod> download_periods(i64 now);

// Where a download from a source is, for the status line.
enum class DownloadStep {
    Starting,    // Asked for, and waiting for the jobs before it
    Listing,     // Asking the source what there is to download
    Downloading, // A file, of the ones to download
    Pausing,     // Between files: see Pacing
    Retrying,    // The source didn't answer, and is asked again after a wait
    Waiting,     // For the source's limit on requests to reset
};

// What the status line says at `step` of a download from `source`, at the
// file `index` of `count`, or waiting `until` a time such as "20:15".
std::string download_status(DownloadStep step, std::string_view source = { }, size_t index = 0, size_t count = 0, std::string_view until = { });

// What the status line says while Strava's descriptions come in, in the
// background: see specs/44-rate-limits.md.
std::string describing_status(size_t to_go);

// What a download is asked to do: the task's arguments, with the settings
// filling in what they don't say.
struct DownloadRequest {
    bool                        garmin { false };
    bool                        wahoo { false };
    bool                        strava { false }; // Titles: after the others, and the inbox
    int                         garmin_days { 30 };
    int                         wahoo_days { 30 };
    int                         wahoo_overlap_days { 30 }; // `wahoo.days` from the settings, whatever `days` says: see specs/46-wahoo-everything.md
    int                         strava_days { 30 };
    std::optional<std::string>  wahoo_client_id { };          // Without one, Wahoo isn't set up
    std::optional<StravaClient> strava_client { };            // Without one, Strava isn't set up
    bool                        strava_descriptions { true }; // In the background, after the titles
    bool                        quiet { false };              // At startup: say only what went wrong
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
    size_t      count { 0 };       // For Downloaded: activities; for Titled: titles
    size_t      waiting { 0 };     // For Titled: descriptions that follow in the background
    std::string message { };       // For NotSetUp and Failed: what to tell the user; for Titled, what stopped the list short
    bool        limited { false }; // For Failed: stopped by the source's limit on requests, not waited out
};

// What to tell the user when the download is done.
struct DownloadReport {
    std::string text { };            // Nothing to say if empty
    std::string ask_login { };       // The source to offer the login to, if any
    JSONValue   login_arguments { }; // For the login's task: what to fetch after it
    bool        describe { false };  // Strava's titles ran: its descriptions follow in the background
    std::string status { };          // Quiet: the limits that stopped it, for the status line
};

// One sentence per source, such as "Garmin Connect: 2 new activities.
// Wahoo: nothing new." A source that isn't set up is left out, unless it's
// the only one asked for, so that a user who doesn't use Wahoo never hears
// of it. When none is, the first one's message says how to set it up.
// Quiet, only failures are reported, and the login isn't offered, and a
// source's limit goes in `status`, not in a message box at every start
// until it resets: see specs/46-wahoo-everything.md. Of two
// sources that need a login, the first is offered.
DownloadReport download_report(std::vector<SourceOutcome> const &outcomes, bool quiet);

// The arguments for the login a download's report offers, so that the
// download after it looks back as far as this one: `days`, the request's
// for that source.
JSONValue login_arguments(DownloadRequest const &request, std::string_view source);

}
