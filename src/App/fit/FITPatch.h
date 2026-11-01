/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Patching a FIT file: records added in a gap where the watch was stopped,
 * and the distances and totals after it raised, with everything else
 * copied as the watch wrote it. See specs/30-fill-gaps.md.
 */

#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Logging.h>

namespace ST::FIT {

// Where a data message's field is, within the message.
struct FITMessageField {
    u8     num { 0 };
    size_t offset { 0 }; // From the message's header byte
    size_t size { 0 };
};

// A message in a FIT file: where it is, and what the patch needs of it.
struct FITMessage {
    size_t                       offset { 0 }; // In the file
    size_t                       size { 0 };   // The header byte included
    bool                         definition { false };
    u8                           local_type { 0 };
    u16                          global { 0 }; // A data message's
    bool                         big_endian { false };
    std::vector<FITMessageField> fields { }; // A data message's

    [[nodiscard]] std::optional<FITMessageField> field(u8 num) const;
};

struct FITScan {
    size_t                  header_size { 0 };
    std::vector<FITMessage> messages { };
};

// The messages of a FIT file, or why it can't be patched: it isn't FIT,
// it's cut short, or it has compressed timestamps, which are offsets from
// the message before and which added messages would shift.
[[nodiscard]] std::expected<FITScan, std::string> scan_fit(std::string_view bytes);

// FIT's CRC-16 of `bytes`, carrying on from `crc`.
[[nodiscard]] u16 fit_crc(std::string_view bytes, u16 crc = 0);

// A record to add, in FIT's units: semicircles, and a FIT timestamp.
struct FillRecord {
    u32                    timestamp { 0 };
    std::optional<i32>     lat { };
    std::optional<i32>     lon { };
    std::optional<float32> altitude { }; // m
    std::optional<float64> distance { }; // m, from the activity's start
    std::optional<float32> speed { };    // m/s
};

// What goes in one gap: between the last record before the watch stopped
// and the first after it started again.
struct FITFill {
    u32                     stop { 0 };
    u32                     restart { 0 };
    std::vector<FillRecord> records { };
    float64                 distance { 0 }; // m, what the gap adds
};

// The file with the fills patched in, or why it can't be.
[[nodiscard]] std::expected<std::string, std::string> patch_fit(std::string_view bytes, std::vector<FITFill> const &fills);

}
