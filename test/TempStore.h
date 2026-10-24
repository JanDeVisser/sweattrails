/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <Date.h>
#include <IO.h>
#include <TempDir.h>

#include <storage/Storage.h>

namespace ST::Test {

namespace fs = std::filesystem;

// A store in a temporary directory, with an inbox and no activities.
struct TempStore {
    TempDir dir { "store" };
    Storage storage { };

    TempStore()
    {
        storage.data_dir = dir.path;
        storage.inbox_dir = dir.path / "inbox";
        storage.activity_dir = dir.path / "activity";
        fs::create_directories(storage.inbox_dir);
        fs::create_directories(storage.activity_dir);
    }

    // Puts an activity that starts at `start_time` in the store, as the
    // inbox would have.
    void add_activity(i64 start_time) const
    {
        auto const start = DateTime::from_timestamp(static_cast<i32>(start_time));
        auto const dir = storage.activity_dir / std::format("{:04}/{:02}", static_cast<u16>(start.year), static_cast<u8>(start.month) + 1);
        fs::create_directories(dir);
        MUST(write_file_by_name((dir / std::format("{}.fit", start_time)).string(), std::string_view { "fit" }));
    }

    [[nodiscard]] std::vector<std::string> inbox() const
    {
        std::vector<std::string> names;
        for (auto const &entry : fs::directory_iterator { storage.inbox_dir }) {
            names.push_back(entry.path().filename().string());
        }
        std::ranges::sort(names);
        return names;
    }
};

}
