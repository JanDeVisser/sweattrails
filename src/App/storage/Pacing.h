/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * How a download of many FIT files is paced, so that Garmin Connect and
 * Wahoo don't stop it for asking too much: see
 * specs/43-download-history.md.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <functional>

namespace ST {

// The HTTP status of a service that's had too many requests.
constexpr long TooManyRequests = 429;

struct Pacing {
    size_t                                    batch { 25 };
    std::chrono::seconds                      pause { 60 };
    int                                       retries { 3 }; // For TooManyRequests, per file
    std::function<void(std::chrono::seconds)> wait { };      // Without one, nothing waits
};

// Whether to pause before the file at `index`: after every batch.
inline bool pause_before(Pacing const &pacing, size_t index)
{
    return pacing.batch > 0 && index > 0 && index % pacing.batch == 0;
}

// Waits as long as `pacing` says, if it can.
inline void pause(Pacing const &pacing)
{
    if (pacing.wait) {
        pacing.wait(pacing.pause);
    }
}

// Downloads a file with `fetch`, which returns an expected with an error
// that has the HTTP `status`, and on TooManyRequests pauses and asks
// again, up to `pacing.retries` times.
auto fetch_paced(Pacing const &pacing, auto const &fetch)
{
    auto ret = fetch();
    for (auto retry = 0; !ret && ret.error().status == TooManyRequests && retry < pacing.retries; ++retry) {
        pause(pacing);
        ret = fetch();
    }
    return ret;
}

}
