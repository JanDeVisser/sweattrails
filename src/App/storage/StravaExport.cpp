/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

#include <CSV.h>
#include <Logging.h>

#include <storage/Activity.h>
#include <storage/StravaExport.h>

namespace ST {

namespace {

std::optional<u64> parse_id(std::string_view text)
{
    u64 id { 0 };
    auto const [end, ec] = std::from_chars(text.data(), text.data() + text.size(), id);
    if (ec != std::errc { } || end != text.data() + text.size()) {
        return { };
    }
    return id;
}

// "1 activity", "2 activities".
std::string count_of(size_t count, std::string_view one, std::string_view more)
{
    return std::format("{} {}", count, count == 1 ? one : more);
}

}

bool is_csv_file(fs::path const &path)
{
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });
    return extension == ".csv";
}

std::expected<std::map<u64, std::string>, std::string> export_descriptions(std::string_view csv, std::string_view file_name)
{
    auto const not_strava = [file_name](std::string_view why) {
        return std::unexpected(std::format("{} isn't Strava's list of activities: {}", file_name, why));
    };
    auto const parsed = CSV::parse(csv);
    if (!parsed) {
        return not_strava(parsed.error().description);
    }
    auto const id_column = parsed->column("Activity ID");
    if (!id_column) {
        return not_strava("it has no Activity ID column.");
    }
    auto const description_column = parsed->column("Activity Description");
    if (!description_column) {
        return not_strava("it has no Activity Description column.");
    }
    std::map<u64, std::string> ret;
    for (auto const &row : parsed->rows) {
        auto const id = (*id_column < row.size()) ? parse_id(row[*id_column]) : std::nullopt;
        if (!id || *description_column >= row.size()) {
            warning(StravaExport, "Skipping a line of {} without an ID or a description", file_name);
            continue;
        }
        ret[*id] = row[*description_column];
    }
    return ret;
}

std::string ExportImport::message() const
{
    auto ret = (descriptions == 0)
        ? std::string { "Strava's export has none of the descriptions missing here." }
        : std::format("Strava's export: {}, {} of them empty.", count_of(descriptions, "description", "descriptions"), empty);
    if (untitled > 0) {
        ret += std::format(" {} no Strava title: if Strava has them, get all the titles (Ctrl+Shift+G) and import again.",
            count_of(untitled, "activity has", "activities have"));
    }
    return ret;
}

namespace {

struct Missing {
    StoredStart stored { };
    StravaTitle title { }; // With the export's description
};

}

StorageResult<ExportImport> import_descriptions(Storage const &storage, std::map<u64, std::string> const &descriptions, ExportProgress const &progress)
{
    ExportImport         ret;
    std::vector<Missing> missing;
    for (auto const &month : TRY_EVAL(storage.months_with_activities())) {
        for (auto const &stored : TRY_EVAL(storage.month_starts(month))) {
            auto title = sidecar_titles(stored.sidecar).strava;
            if (!title) {
                ++ret.untitled;
                continue;
            }
            auto const found = descriptions.find(title->id);
            if (title->description || found == descriptions.end()) {
                continue;
            }
            title->description = found->second;
            missing.push_back(Missing { .stored = stored, .title = *title });
        }
    }

    for (size_t ix = 0; ix < missing.size(); ++ix) {
        auto const &[stored, title] = missing[ix];
        if (progress) {
            progress(ix, missing.size());
        }
        if (auto const saved = save_sidecar_strava(stored.sidecar, title); !saved) {
            warning(StravaExport, "Could not save Strava's description in `{}`: {}", stored.sidecar.string(), saved.error().to_string());
            continue;
        }
        ++ret.descriptions;
        if (title.description->empty()) {
            ++ret.empty;
        }
        ret.changed.push_back(stored.start);
    }
    std::ranges::sort(ret.changed);
    return ret;
}

}
