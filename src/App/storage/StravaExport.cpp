/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <iterator>
#include <set>

#include <CSV.h>
#include <IO.h>
#include <Logging.h>
#include <StringUtil.h>
#include <Zip.h>

#include <storage/Activity.h>
#include <storage/Recordings.h>
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

// The sentence on what came in through the inbox, if anything did.
std::string missing_message(MissingImport const &missing)
{
    auto const were = (missing.set_aside == 1) ? "was" : "were";
    if (missing.added > 0) {
        return std::format(" Added {} from the export{}.", count_of(missing.added, "activity", "activities"),
            missing.set_aside > 0 ? std::format("; {} {} already here", missing.set_aside, were) : std::string { });
    }
    if (missing.set_aside > 0) {
        return std::format(" {} of the export's activities {} already here.", missing.set_aside, were);
    }
    return { };
}

std::string moved_message(MissingImport const &missing)
{
    if (missing.titles_moved == 0) {
        return { };
    }
    return std::format(" Moved {} to the right activity.", count_of(missing.titles_moved, "Strava title", "Strava titles"));
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
    ret += missing_message(missing);
    ret += moved_message(missing);
    auto const unreadable = stubs.unreadable + missing.unreadable;
    if (unreadable > 0) {
        auto const which = (stubs.replaced > 0 || missing.added > 0) ? std::format("{} more", unreadable)
            : (missing.unreadable > 0)                               ? count_of(unreadable, "activity", "activities")
                                                                     : count_of(unreadable, "activity that's only a summary", "activities that are only a summary");
        ret += std::format(" {} {} a GPX or TCX file, which SweatTrails can't read.", which, unreadable == 1 ? "has" : "have");
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

// The FIT files in the store with each Strava ID: the activities', and
// the recordings' in their folders, as one the import grouped with
// another activity before has its ID there.
StorageResult<std::map<u64, std::vector<fs::path>>> strava_fits(Storage const &storage)
{
    std::map<u64, std::vector<fs::path>> ret;
    auto const                           add = [&ret](fs::path const &fit) {
        if (auto const title = sidecar_titles(fs::path { fit }.replace_extension(".json")).strava; title) {
            ret[title->id].push_back(fit);
        }
    };
    for (auto const &month : TRY_EVAL(storage.months_with_activities())) {
        for (auto const &stored : TRY_EVAL(storage.month_starts(month))) {
            auto const fit = fs::path { stored.sidecar }.replace_extension(".fit");
            add(fit);
            for (auto const &recording : recordings_of(fit)) {
                add(recording);
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

ExportFile classify_export_file(RecordingInfo const &strava_file, std::vector<StoredWithID> const &stored)
{
    auto const same = [&strava_file](RecordingInfo const &recording) { return same_recording(strava_file, recording); };
    for (auto const &with_id : stored) {
        if (std::ranges::any_of(with_id.recordings, same)) {
            return ExportFile::Same;
        }
    }
    auto const overlaps = [&strava_file](StoredWithID const &with_id) { return same_activity(strava_file, with_id.info); };
    return std::ranges::any_of(stored, overlaps) ? ExportFile::OtherRecording : ExportFile::OtherActivity;
}

namespace {

// A line of the export to put in the inbox, and for one whose ID the
// store has, its files with the ID.
struct ToQueue {
    u64                   id { 0 };
    ExportActivity const *activity { nullptr };
    std::vector<fs::path> with_id { };
};

// The start of the listed activity a FIT file in the store is of, as
// TitlesChanged has it.
std::optional<i64> listed_start(fs::path const &fit)
{
    return parse_id(main_of(fit).stem().string()).transform([](u64 start) { return static_cast<i64>(start); });
}

bool queue(Storage const &storage, MissingImport &ret, u64 id, ExportActivity const &activity, std::string_view fit)
{
    if (auto const queued = to_inbox(storage, id, activity, fit); !queued) {
        warning(StravaExport, "Couldn't put `{}` in the inbox: {}", activity.file, queued.error().to_string());
        return false;
    }
    ++ret.queued;
    return true;
}

// Takes Strava's activity `id` off the FIT files `wrong`, whose activity
// Strava's file isn't. Whether it could take it off all of them.
bool reject(MissingImport &ret, u64 id, std::vector<fs::path> const &wrong)
{
    auto all = true;
    for (auto const &fit : wrong) {
        if (auto const rejected = reject_sidecar_strava(fs::path { fit }.replace_extension(".json"), id); !rejected) {
            warning(StravaExport, "Couldn't take Strava's activity {} off `{}`: {}", id, fit.string(), rejected.error().to_string());
            all = false;
        } else if (auto const start = listed_start(fit); start) {
            ret.retitled.push_back(*start);
        }
    }
    return all;
}

// Gives the FIT file `fit` in the store, which is Strava's activity `id`'s,
// its Strava title, unless it has one: one file uploaded to Strava twice
// is two Strava activities, and the first keeps it. Whether it took it.
bool give_title(MissingImport &ret, u64 id, ExportActivity const &activity, fs::path const &fit)
{
    auto const sidecar = fs::path { fit }.replace_extension(".json");
    if (sidecar_titles(sidecar).strava) {
        return false;
    }
    if (auto const saved = save_sidecar_strava(sidecar, StravaTitle { .id = id, .name = activity.name, .description = activity.description }); !saved) {
        warning(StravaExport, "Couldn't give `{}` Strava's title: {}", fit.string(), saved.error().to_string());
        return false;
    }
    if (auto const start = listed_start(fit); start) {
        ret.retitled.push_back(*start);
    }
    return true;
}

bool same_bytes(std::string_view fit, std::vector<fs::path> const &stored)
{
    return std::ranges::any_of(stored, [fit](fs::path const &path) {
        auto const contents = read_file_by_name(path.string());
        return contents && *contents == fit;
    });
}

// The store's files with the ID, each with its activity's recordings.
// Those that can't be read are left out, with a warning.
std::vector<StoredWithID> stored_with_id(Storage &storage, std::vector<fs::path> const &with_id)
{
    std::vector<StoredWithID> ret;
    for (auto const &fit : with_id) {
        auto info = recording_info(storage, fit);
        if (!info) {
            warning(StravaExport, "Can't compare Strava's file with `{}`: it can't be read", fit.string());
            continue;
        }
        StoredWithID stored { .info = *info };
        auto const   main = main_of(fit);
        std::vector  group { main };
        std::ranges::copy(recordings_of(main), std::back_inserter(group));
        for (auto const &recording : group) {
            if (recording == fit) {
                stored.recordings.push_back(*info);
            } else if (auto const other = recording_info(storage, recording); other) {
                stored.recordings.push_back(*other);
            }
        }
        ret.push_back(std::move(stored));
    }
    return ret;
}

}

StorageResult<MissingImport> queue_missing(Storage &storage, ExportActivities const &activities, fs::path const &export_dir, ExportProgress const &progress)
{
    MissingImport        ret;
    auto const           stored = TRY_EVAL(strava_fits(storage));
    auto const           set_aside = storage.set_aside_as_duplicates(Source);
    std::vector<ToQueue> to_queue;
    for (auto const &[id, activity] : activities) {
        if (activity.file.empty() || set_aside.contains(id)) {
            continue;
        }
        auto const found = stored.find(id);
        if (original_kind(activity.file) == OriginalKind::Other) {
            // A GPX file of an activity the store has adds nothing to it.
            if (found == stored.end()) {
                ++ret.unreadable;
            }
            continue;
        }
        to_queue.push_back(ToQueue { .id = id, .activity = &activity, .with_id = (found != stored.end()) ? found->second : std::vector<fs::path> { } });
    }

    for (size_t ix = 0; ix < to_queue.size(); ++ix) {
        auto const &[id, activity, with_id] = to_queue[ix];
        if (progress) {
            progress(ix, to_queue.size());
        }
        auto const fit = read_fit(export_dir / activity->file);
        if (!fit) {
            continue;
        }
        if (with_id.empty()) {
            queue(storage, ret, id, *activity, *fit);
            continue;
        }
        // Most of them are the stored file, byte for byte: no need to read
        // them as FIT files.
        if (same_bytes(*fit, with_id)) {
            continue;
        }
        auto const info = recording_info(storage, std::string_view { *fit });
        if (!info) {
            warning(StravaExport, "Not using `{}`: it's no activity", activity->file);
            continue;
        }
        auto const kind = classify_export_file(*info, stored_with_id(storage, with_id));
        if (kind == ExportFile::Same) {
            continue;
        }
        // Strava's file is in the store already, without the ID: the inbox
        // would set it aside. A stub there it would replace.
        if (auto const here = storage.fit_at(info->start); here && !is_stub_file(*here).value_or(false)) {
            if (kind == ExportFile::OtherRecording || !reject(ret, id, with_id)) {
                continue;
            }
            if (give_title(ret, id, *activity, *here)) {
                ++ret.titles_moved;
                continue;
            }
            // It has a Strava title of its own, so no file has the ID now:
            // the inbox sets Strava's file aside, and remembers it, so that
            // the next import doesn't queue it again.
            queue(storage, ret, id, *activity, *fit);
            continue;
        }
        if (queue(storage, ret, id, *activity, *fit) && kind == ExportFile::OtherActivity && reject(ret, id, with_id)) {
            ++ret.titles_moved;
        }
    }
    std::ranges::sort(ret.retitled);
    auto const duplicates = std::ranges::unique(ret.retitled);
    ret.retitled.erase(duplicates.begin(), duplicates.end());
    return ret;
}

}
