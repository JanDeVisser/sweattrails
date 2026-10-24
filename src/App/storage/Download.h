/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * What the download tasks ask for, and what they tell the user when the
 * download is done. One download job runs every source asked for, and
 * says in one message what each brought in: see specs/wahoo-download.md.
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

// What a download is asked to do: the task's arguments, with the settings
// filling in what they don't say.
struct DownloadRequest {
    bool                       garmin { false };
    bool                       wahoo { false };
    int                        garmin_days { 30 };
    int                        wahoo_days { 30 };
    std::optional<std::string> wahoo_client_id { }; // Without one, Wahoo isn't set up
    bool                       quiet { false };     // At startup: say only what went wrong
};

// A download from the sources asked for: `garmin.days` and `wahoo.days`
// from the settings, unless the task's `days` argument overrides both, the
// task's `quiet` argument, and Wahoo's client ID from the settings.
DownloadRequest make_download_request(JSONValue const &settings, JSONValue const &arguments, bool garmin, bool wahoo);

// How the download from one source went.
struct SourceOutcome {
    enum class Kind {
        NotSetUp,   // No Garmin key, or no Wahoo client ID
        NeedsLogin, // Wahoo, without a login it accepts
        Downloaded,
        Failed,
    };

    std::string source { }; // "Garmin Connect", "Wahoo"
    Kind        kind { Kind::Downloaded };
    size_t      count { 0 }; // For Downloaded
    std::string message { }; // For NotSetUp and Failed: what to tell the user
};

// What to tell the user when the download is done.
struct DownloadReport {
    std::string text { };            // Nothing to say if empty
    bool        ask_login { false }; // Whether to offer the Wahoo login
};

// One sentence per source, such as "Garmin Connect: 2 new activities.
// Wahoo: nothing new." A source that isn't set up is left out, unless it's
// the only one asked for, so that a user who doesn't use Wahoo never hears
// of it. When none is, the first one's message says how to set it up. Quiet, only failures are reported, and the login isn't offered.
DownloadReport download_report(std::vector<SourceOutcome> const &outcomes, bool quiet);

}
