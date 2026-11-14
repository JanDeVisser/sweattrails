/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <set>

#include <CSV.h>
#include <IO.h>
#include <Logging.h>
#include <StringUtil.h>
#include <Zip.h>

#include <storage/Activity.h>
#include <storage/StravaExport.h>
#include <storage/Stubs.h>

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

std::expected<ExportActivities, std::string> read_export(std::string_view csv, std::string_view file_name)
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
    auto const name_column = parsed->column("Activity Name");
    auto const file_column = parsed->column("Filename");
    auto const optional = [](std::optional<size_t> column, CSVRow const &row) {
        return (column && *column < row.size()) ? row[*column] : std::string { };
    };
    ExportActivities ret;
    for (auto const &row : parsed->rows) {
        auto const id = (*id_column < row.size()) ? parse_id(row[*id_column]) : std::nullopt;
        if (!id || *description_column >= row.size()) {
            warning(StravaExport, "Skipping a line of {} without an ID or a description", file_name);
            continue;
        }
        ret[*id] = ExportActivity {
            .name = optional(name_column, row),
            .description = row[*description_column],
            .file = optional(file_column, row),
        };
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
    if (stubs.replaced > 0) {
        ret += std::format(" Replaced {} that {} only a summary with {} recording{}.",
            count_of(stubs.replaced, "activity", "activities"), stubs.replaced == 1 ? "was" : "were",
            stubs.replaced == 1 ? "its" : "their", stubs.replaced == 1 ? "" : "s");
    }
    if (stubs.unreadable > 0) {
        ret += std::format(" {} {} a GPX or TCX file, which SweatTrails can't read.",
            stubs.replaced > 0 ? std::format("{} more", stubs.unreadable) : count_of(stubs.unreadable, "activity that's only a summary", "activities that are only a summary"),
            stubs.unreadable == 1 ? "has" : "have");
    }
    return ret;
}

namespace {

struct Missing {
    StoredStart stored { };
    StravaTitle title { }; // With the export's description
};

}

StorageResult<ExportImport> import_descriptions(Storage const &storage, ExportActivities const &activities, ExportProgress const &progress)
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
            auto const found = activities.find(title->id);
            if (title->description || found == activities.end()) {
                continue;
            }
            title->description = found->second.description;
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

namespace {

enum class OriginalKind {
    Fit,
    GzippedFit,
    Other, // GPX or TCX, gzipped or not
};

OriginalKind original_kind(std::string_view file)
{
    auto const name = to_lower(file);
    if (name.ends_with(".fit.gz")) {
        return OriginalKind::GzippedFit;
    }
    return name.ends_with(".fit") ? OriginalKind::Fit : OriginalKind::Other;
}

struct StubWithOriginal {
    StoredStart stub { };
    fs::path    original { };
};

// The FIT file `original`, ungzipped. Nothing, with a warning, if it
// can't be read.
std::optional<std::string> read_fit(fs::path const &original)
{
    auto contents = read_file_by_name(original.string());
    if (contents && original_kind(original.string()) == OriginalKind::GzippedFit) {
        contents = gunzip(*contents);
    }
    if (!contents) {
        warning(StravaExport, "Not using `{}`: {}", original.string(), contents.error().to_string());
        return { };
    }
    return std::move(*contents);
}

// The FIT file `original`, ungzipped, and when it starts as the inbox
// would file it. Nothing, with a warning, if it can't be read, or is only
// a summary too.
std::optional<std::pair<std::string, i64>> read_original(Storage &storage, fs::path const &original)
{
    auto const skip = [&original](std::string_view why) {
        warning(StravaExport, "Not using `{}`: {}", original.string(), why);
        return std::nullopt;
    };
    auto contents = read_fit(original);
    if (!contents) {
        return std::nullopt;
    }
    if (is_stub(*contents).value_or(true)) {
        return skip("it's no FIT file with records");
    }
    Activity activity { storage, ActivityID { Month { 2020, DateTime::Month::January }, original.stem().string(), FileTypes::one(ActivityFile::Fit) } };
    activity.depth = LoadingDepth::Shallow;
    if (auto const loaded = activity.load_from_slice(*contents); !loaded || !*loaded) {
        return skip("it's no activity");
    }
    return std::pair { std::move(*contents), static_cast<i64>(activity.segment.start_time.timestamp) };
}

}

StorageResult<StubImport> import_originals(Storage &storage, ExportActivities const &activities, fs::path const &export_dir, ExportProgress const &progress)
{
    StubImport                    ret;
    std::vector<StubWithOriginal> stubs;
    for (auto const &month : TRY_EVAL(storage.months_with_activities())) {
        for (auto const &stored : TRY_EVAL(storage.month_starts(month))) {
            auto const title = sidecar_titles(stored.sidecar).strava;
            auto const found = title ? activities.find(title->id) : activities.end();
            if (found == activities.end() || found->second.file.empty()) {
                continue;
            }
            // Only now, as reading every FIT file takes a while.
            if (!is_stub_file(fs::path { stored.sidecar }.replace_extension(".fit")).value_or(false)) {
                continue;
            }
            if (original_kind(found->second.file) == OriginalKind::Other) {
                ++ret.unreadable;
                continue;
            }
            stubs.push_back(StubWithOriginal { .stub = stored, .original = export_dir / found->second.file });
        }
    }
    // month_starts() is in no order.
    std::ranges::sort(stubs, { }, [](StubWithOriginal const &s) { return s.stub.start; });

    for (size_t ix = 0; ix < stubs.size(); ++ix) {
        auto const &[stub, original] = stubs[ix];
        if (progress) {
            progress(ix, stubs.size());
        }
        auto const read = read_original(storage, original);
        if (!read) {
            continue;
        }
        auto const &[fit, start] = *read;
        if (auto const replaced = replace_stub(storage, stub, fit, start); !replaced) {
            warning(StravaExport, "Couldn't replace `{}` with `{}`: {}", stub.sidecar.string(), original.string(), replaced.error().to_string());
            continue;
        }
        ++ret.replaced;
        ret.moved.emplace_back(stub.start, start);
    }
    return ret;
}

namespace {

// As the inbox names the files: strava-<id>.fit.
constexpr std::string_view Source = "strava";

// The Strava IDs of the activities in the store, and of the recordings in
// their folders: one the import grouped with another activity before has
// its ID there.
StorageResult<std::set<u64>> strava_ids(Storage const &storage)
{
    std::set<u64> ret;
    auto const    add = [&ret](fs::path const &sidecar) {
        if (auto const title = sidecar_titles(sidecar).strava; title) {
            ret.insert(title->id);
        }
    };
    for (auto const &month : TRY_EVAL(storage.months_with_activities())) {
        for (auto const &stored : TRY_EVAL(storage.month_starts(month))) {
            add(stored.sidecar);
            for (auto const &recording : recordings_of(fs::path { stored.sidecar }.replace_extension(".fit"))) {
                add(fs::path { recording }.replace_extension(".json"));
            }
        }
    }
    return ret;
}

// Puts `fit` in the inbox as strava-<id>.fit, the sidecar first: the inbox
// takes a FIT file as soon as it's there, and would leave the sidecar
// behind if it came second.
std::expected<void, LibCError> to_inbox(Storage const &storage, u64 id, ExportActivity const &activity, std::string_view fit)
{
    auto const stem = storage.inbox_dir / std::format("{}-{}", Source, id);
    TRY(save_sidecar_strava(fs::path { stem }.replace_extension(".json"),
        StravaTitle { .id = id, .name = activity.name, .description = activity.description }));
    TRY(write_file_by_name(fs::path { stem }.replace_extension(".fit").string(), fit));
    return { };
}

}

void MissingImport::count(std::string_view file_name, Storage::InboxFiling const &filing)
{
    if (!file_name.starts_with(std::format("{}-", Source))) {
        return;
    }
    if (filing.filed()) {
        ++added;
    } else if (filing.outcome == Storage::InboxOutcome::Duplicate) {
        ++set_aside;
    }
}

StorageResult<MissingImport> queue_missing(Storage const &storage, ExportActivities const &activities, fs::path const &export_dir, ExportProgress const &progress)
{
    MissingImport                                       ret;
    auto const                                          stored = TRY_EVAL(strava_ids(storage));
    auto const                                          set_aside = storage.set_aside_as_duplicates(Source);
    std::vector<std::pair<u64, ExportActivity const *>> missing;
    for (auto const &[id, activity] : activities) {
        if (activity.file.empty() || stored.contains(id) || set_aside.contains(id)) {
            continue;
        }
        if (original_kind(activity.file) == OriginalKind::Other) {
            ++ret.unreadable;
            continue;
        }
        missing.emplace_back(id, &activity);
    }

    for (size_t ix = 0; ix < missing.size(); ++ix) {
        auto const &[id, activity] = missing[ix];
        if (progress) {
            progress(ix, missing.size());
        }
        auto const fit = read_fit(export_dir / activity->file);
        if (!fit) {
            continue;
        }
        if (auto const queued = to_inbox(storage, id, *activity, *fit); !queued) {
            warning(StravaExport, "Couldn't put `{}` in the inbox: {}", activity->file, queued.error().to_string());
            continue;
        }
        ++ret.queued;
    }
    return ret;
}

}
