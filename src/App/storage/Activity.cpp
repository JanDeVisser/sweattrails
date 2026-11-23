/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <system_error>

#include <FileIO.h>
#include <JSON.h>
#include <Logging.h>
#include <StringUtil.h>

#include <fit/FIT.h>
#include <storage/Activity.h>
#include <storage/Laps.h>
#include <storage/Legs.h>
#include <storage/Storage.h>

namespace ST {

using namespace ST::FIT;

namespace {

bool eql_ignore_case(std::string_view const &lhs, std::string_view const &rhs)
{
    return to_lower(lhs) == to_lower(rhs);
}

}

std::optional<CadenceStats> cadence_stats(std::span<CadenceSample const> samples)
{
    auto const pedaling = [](CadenceSample const &sample) {
        return sample.cadence >= PedalingCadence && sample.power.value_or(PedalingPower) >= PedalingPower;
    };

    u64    sum = 0;
    size_t count = 0;
    for (auto const &sample : samples) {
        if (pedaling(sample)) {
            sum += sample.cadence;
            ++count;
        }
    }
    if (count == 0) {
        return { };
    }
    CadenceStats stats { .average = static_cast<float32>(sum) / static_cast<float32>(count) };

    // A rolling sum over the current run of consecutive pedaling records.
    u64    run_sum = 0;
    size_t run_length = 0;
    for (size_t ix = 0; ix < samples.size(); ++ix) {
        auto const &sample = samples[ix];
        bool const  follows = ix > 0 && samples[ix - 1].at + 1 == sample.at;
        if (!pedaling(sample)) {
            run_sum = 0;
            run_length = 0;
            continue;
        }
        if (!follows) {
            run_sum = 0;
            run_length = 0;
        }
        run_sum += sample.cadence;
        ++run_length;
        if (run_length > MinCadenceRecords) {
            run_sum -= samples[ix - MinCadenceRecords].cadence;
        }
        if (run_length >= MinCadenceRecords) {
            auto const average = static_cast<float32>(run_sum) / static_cast<float32>(MinCadenceRecords);
            if (!stats.min || average < *stats.min) {
                stats.min = average;
                stats.min_at = sample.at;
            }
        }
    }
    return stats;
}

// The ranges' min_at and max_at are indices into `records`: for a lap,
// they're relative to the lap's first record.
void Segment::analyze(std::span<record> records, bool smooth)
{
    // The lowest altitude since the last rise that counted as climbing.
    std::optional<float32> climb_from { };
    // As recorded, before they're smoothed.
    std::vector<CadenceSample> cadence_samples;
    for (size_t ix = 0; ix < records.size(); ++ix) {
        auto      &rec = records[ix];
        auto const recorded_power = rec.power;
        if (rec.speed) {
            auto const smoothed = computed_speed_range.extend(*rec.speed, ix);
            if (smooth) {
                *rec.speed = smoothed;
            }
        }
        if (rec.power) {
            computed_normalized_range.extend(*rec.power);
            auto const smoothed = computed_power_range.extend(*rec.power, ix);
            if (smooth) {
                *rec.power = smoothed;
            }
        }
        if (rec.cadence) {
            cadence_samples.push_back(CadenceSample { .at = ix, .cadence = *rec.cadence, .power = recorded_power });
            auto const smoothed = computed_cadence_range.extend(*rec.cadence, ix);
            if (smooth) {
                *rec.cadence = smoothed;
            }
        }
        if (rec.heart_rate) {
            if (ix > 0) {
                auto const gap = static_cast<i64>(rec.timestamp.timestamp) - static_cast<i64>(records[ix - 1].timestamp.timestamp);
                if (gap > 0 && gap <= RangeMovingGap) {
                    auto const hr = static_cast<float64>(*rec.heart_rate);
                    hr_load = hr_load.value_or(0) + hr * hr * static_cast<float64>(gap);
                }
            }
            auto const smoothed = computed_hr_range.extend(*rec.heart_rate, ix);
            if (smooth) {
                *rec.heart_rate = smoothed;
            }
        }
        if (rec.altitude) {
            auto const elev_int = static_cast<i16>(*rec.altitude);
            computed_elevation_range.extend(elev_int, ix);
            if (!climb_from || *rec.altitude < *climb_from) {
                climb_from = rec.altitude;
            } else if (*rec.altitude - *climb_from >= ClimbHysteresis) {
                computed_climb += *rec.altitude - *climb_from;
                climb_from = rec.altitude;
            }
        }
        if (rec.position) {
            auto const &position = *rec.position;
            // Not only far from 0 both ways: London is just west of 0°.
            if (position.has_fix()) {
                if (box) {
                    box->extend(position);
                } else {
                    box = Box { .sw = position, .ne = position };
                }
            }
        }
    }
    cadence = cadence_stats(cadence_samples);
    if (computed_normalized_range.range && computed_power_range.range) {
        auto const &np = *computed_normalized_range.range;
        auto const &avg = *computed_power_range.range;
        variability_index = static_cast<float32>(np.normalized) / static_cast<float32>(avg.average);
    }
}

std::optional<Segment> range_segment(Activity const &activity, size_t first, size_t last)
{
    if (first > last || last >= activity.recorded.size()) {
        return { };
    }
    // analyze() takes the records it can smooth; this one doesn't.
    std::vector<record> records { activity.recorded.begin() + static_cast<std::ptrdiff_t>(first), activity.recorded.begin() + static_cast<std::ptrdiff_t>(last) + 1 };
    auto const         &from = records.front();
    auto const         &to = records.back();
    Segment             ret { .start_time = from.timestamp, .end_time = to.timestamp };
    // Not below 0, if the watch set its clock back during the range.
    ret.elapsed = Duration { static_cast<u32>(std::max<i64>(0, static_cast<i64>(to.timestamp.timestamp) - static_cast<i64>(from.timestamp.timestamp))) };
    if (from.distance && to.distance) {
        ret.distance = std::max(0.0f, *to.distance - *from.distance);
    }
    u32 moving { 0 };
    for (size_t ix = 1; ix < records.size(); ++ix) {
        auto const gap = static_cast<i64>(records[ix].timestamp.timestamp) - static_cast<i64>(records[ix - 1].timestamp.timestamp);
        auto const speed = records[ix].speed;
        if (gap > 0 && gap <= RangeMovingGap && (!speed || *speed >= RangeMovingSpeed)) {
            moving += static_cast<u32>(gap);
        }
    }
    ret.moving = Duration { moving };
    ret.analyze(records, false);
    return ret;
}

fs::path recordings_dir(fs::path const &main_fit)
{
    return main_fit.parent_path() / std::format("{}{}", main_fit.stem().string(), RecordingsSuffix);
}

std::vector<fs::path> recordings_of(fs::path const &main_fit)
{
    std::vector<fs::path> ret;
    std::error_code       ec { };
    for (fs::directory_iterator it { recordings_dir(main_fit), ec }; !ec && it != fs::directory_iterator { }; it.increment(ec)) {
        if (it->path().extension() == ".fit") {
            ret.push_back(it->path());
        }
    }
    std::ranges::sort(ret);
    return ret;
}

StorageResult<bool> Activity::load(LoadingDepth the_depth)
{
    trace(Storage, "Loading activity {}", id.name);
    depth = the_depth;
    if (auto d = id.month.dir(*storage); d) {
        return load_from_dir(*d);
    }
    return false;
}

StorageResult<bool> Activity::load_from_dir(fs::path const &dir)
{
    auto const main_fit = dir / std::format("{}.fit", id.name);
    if (id.files.contains(ActivityFile::Fit)) {
        auto const fit = id.recording.empty() ? main_fit : recordings_dir(main_fit) / std::format("{}.fit", id.recording);
        if (!TRY_EVAL(load_fit_file_by_name(fit))) {
            return false;
        }
        fit_title = title;
        // The activity's name, whichever recording is shown, starts from
        // the main one's FIT file: a workout one device ran names it.
        if (!id.recording.empty()) {
            Activity main { *storage, id.main() };
            main.depth = LoadingDepth::Shallow;
            if (TRY_EVAL(main.load_fit_file_by_name(main_fit))) {
                fit_title = main.title;
            }
        }
        sidecar = fs::path { fit }.replace_extension(".json");
        activity_sidecar = fs::path { main_fit }.replace_extension(".json");
        read_names();
    }
    TRY(analyze());
    // After analyze(): the laps' stats come from the records as recorded,
    // which only a deep load, not a month's list, has.
    if (!sidecar.empty() && depth == LoadingDepth::Deep) {
        auto const others = recordings_of(main_fit);
        recordings = 1 + others.size();
        auto const here = std::ranges::find(others, sidecar, [](fs::path const &other) { return fs::path { other }.replace_extension(".json"); });
        recording_number = here == others.end() ? 1 : 2 + static_cast<size_t>(here - others.begin());
        user_lap_starts = sidecar_laps(sidecar);
        if (user_lap_starts) {
            laps = laps_at(*this, *user_lap_starts);
        }
    }
    return true;
}

fs::path fit_path(Activity const &activity)
{
    if (activity.sidecar.empty()) {
        return { };
    }
    return fs::path { activity.sidecar }.replace_extension(".fit");
}

fs::path original_path(Activity const &activity)
{
    if (activity.sidecar.empty()) {
        return { };
    }
    return fs::path { activity.sidecar }.replace_extension(".fit.original");
}

std::vector<i64> Activity::lap_starts() const
{
    return user_lap_starts ? *user_lap_starts : device_lap_starts(*this);
}

std::expected<void, LibCError> Activity::set_lap_starts(std::optional<std::vector<i64>> starts)
{
    if (sidecar.empty()) {
        return std::unexpected(LibCError { "`{}`'s laps can't be changed: it wasn't loaded from the store", id.name });
    }
    if (starts) {
        std::ranges::sort(*starts);
    }
    TRY(save_sidecar_laps(sidecar, starts));
    laps = starts ? laps_at(*this, *starts) : device_laps;
    user_lap_starts = std::move(starts);
    return { };
}

void Activity::read_names()
{
    if (activity_sidecar.empty()) {
        return;
    }
    auto const names = sidecar_titles(activity_sidecar);
    if (leg) {
        read_leg_names(names);
        return;
    }
    // Each name wins over the ones before: the FIT file's, the service's,
    // the old app's `.strava` file's, Strava's in the sidecar, and the
    // user's. See specs/24-strava-titles.md.
    auto const old_strava = read_strava_file(fs::path { activity_sidecar }.replace_extension(".strava"));
    title = fit_title;
    auto const take = [this](std::optional<std::string> const &name) {
        if (name && !name->empty()) {
            title = *name;
        }
    };
    take(names.service);
    take(old_strava.name);
    take(names.strava.transform([](StravaTitle const &strava) { return strava.name; }));
    if (names.strava && names.strava->description) {
        notes = *names.strava->description;
    } else {
        notes = old_strava.description.value_or("");
    }
    // The whole of a multisport activity has no gear: its legs have.
    gear_pick = is_multisport() ? std::nullopt : names.gear;
    if (is_multisport()) {
        read_whole_names(names);
    }
    original_title = title;
    original_notes = notes;
    take(names.own);
    if (names.own_notes) {
        notes = *names.own_notes;
    }
}

namespace {

// A leg's names, from its own in the sidecar: its name, "Bike", Strava's,
// and the user's. See specs/58-multisport-leg-titles.md.
struct LegNames {
    std::string title { };
    std::string original_title { };
    std::string notes { };
    std::string original_notes { };
};

LegNames leg_names(std::string const &name, LegTitles const &titles)
{
    LegNames ret { .title = name };
    if (titles.strava && !titles.strava->name.empty()) {
        ret.title = titles.strava->name;
    }
    if (titles.strava && titles.strava->description) {
        ret.notes = *titles.strava->description;
    }
    ret.original_title = ret.title;
    ret.original_notes = ret.notes;
    if (titles.own) {
        ret.title = *titles.own;
    }
    if (titles.own_notes) {
        ret.notes = *titles.own_notes;
    }
    return ret;
}

// Gives an Activity or a Leg the names.
void take_names(auto &into, LegNames names)
{
    into.title = std::move(names.title);
    into.original_title = std::move(names.original_title);
    into.notes = std::move(names.notes);
    into.original_notes = std::move(names.original_notes);
}

LegTitles titles_of_leg(SidecarTitles const &names, size_t leg)
{
    return (leg < names.legs.size()) ? names.legs[leg] : LegTitles { };
}

}

void Activity::read_leg_names(SidecarTitles const &names)
{
    auto const titles = titles_of_leg(names, leg->index);
    take_names(*this, leg_names(leg->name, titles));
    gear_pick = titles.gear;
}

// The legs' names, and from them the whole's: the legs' common title
// wins over the names before it, and their descriptions over Strava's
// from before the legs had their own.
void Activity::read_whole_names(SidecarTitles const &names)
{
    std::vector<std::string>    titles;
    std::vector<LegDescription> descriptions;
    bool                        titled { false };
    for (size_t ix = 0; ix < legs.size(); ++ix) {
        auto      &the_leg = legs[ix];
        auto const name = leg_name(*this, ix);
        take_names(the_leg, leg_names(name, titles_of_leg(names, ix)));
        the_leg.gear_pick = titles_of_leg(names, ix).gear;
        if (the_leg.sport == FIT::sport::Transition) {
            continue;
        }
        titles.push_back(the_leg.title);
        titled = titled || the_leg.title != name;
        descriptions.push_back(LegDescription { .name = name, .description = the_leg.notes });
    }
    if (auto const common = common_title(titles, titled); common) {
        title = *common;
    }
    if (auto const described = legs_description(descriptions); !described.empty()) {
        notes = described;
    }
}

namespace {

// SweatTrails' own key in the sidecar, for the name the user gave it.
constexpr char const OwnTitleKey[] = "title";

// And for the description the user gave it.
constexpr char const OwnNotesKey[] = "notes";

// And for what Strava has for it.
constexpr char const StravaKey[] = "strava";
constexpr char const StravaRejectedKey[] = "strava_rejected";

// And for the laps the user made.
constexpr char const LapsKey[] = "laps";

// And for the names of a multisport activity's legs.
constexpr char const LegsKey[] = "legs";

// And for the gear picked for it.
constexpr char const GearKey[] = "gear";

}

std::optional<GearPick> sidecar_gear(JSONValue const &part)
{
    auto const gear = part.get(GearKey);
    if (!gear) {
        return { };
    }
    if (gear->is_null()) {
        return GearPick { };
    }
    if (auto const name = part.try_get<std::string>(GearKey); name && !name->empty()) {
        return GearPick { .name = *name };
    }
    // Anything else in the key is no pick, rather than a pick of no gear.
    return { };
}

std::optional<JSONValue> read_sidecar(fs::path const &sidecar)
{
    std::error_code ec { };
    if (!fs::exists(sidecar, ec)) {
        return { };
    }
    // Not JSONValue::read_file(), which ends the program on a syntax error.
    auto const contents = read_file_by_name(sidecar.string());
    if (!contents) {
        trace(Storage, "Ignoring sidecar `{}`: {}", sidecar.string(), contents.error().to_string());
        return { };
    }
    auto json = JSONValue::deserialize(*contents);
    if (!json) {
        trace(Storage, "Ignoring sidecar `{}`: it isn't JSON", sidecar.string());
        return { };
    }
    return std::move(*json);
}

namespace {

std::optional<std::string> non_empty_string(JSONValue const &json, std::string_view key)
{
    if (auto name = json.try_get<std::string>(key); name && !name->empty()) {
        return *name;
    }
    return { };
}

std::optional<std::string> service_title(JSONValue const &json)
{
    // Garmin Connect calls it `activityName`, Wahoo `name`.
    for (auto const *field : { "activityName", "name" }) {
        if (auto name = non_empty_string(json, field); name) {
            return name;
        }
    }
    return { };
}

std::optional<StravaTitle> strava_title(JSONValue const &json, fs::path const &sidecar)
{
    auto const strava = json.get(StravaKey);
    if (!strava) {
        return { };
    }
    auto decoded = StravaTitle::decode(*strava);
    if (!decoded) {
        trace(Storage, "Ignoring `{}`'s \"strava\": {}", sidecar.string(), decoded.error().to_string());
        return { };
    }
    return std::move(*decoded);
}

std::vector<LegTitles> leg_titles(JSONValue const &json, fs::path const &sidecar)
{
    std::vector<LegTitles> ret;
    auto const             legs = json.get(LegsKey).and_then([](JSONValue const &legs) { return legs.to_array(); });
    for (auto const &leg : legs.value_or(JSONValue::Array { })) {
        if (!leg.is_object()) {
            ret.emplace_back();
            continue;
        }
        ret.push_back(LegTitles {
            .strava = strava_title(leg, sidecar),
            .own = non_empty_string(leg, OwnTitleKey),
            .own_notes = non_empty_string(leg, OwnNotesKey),
            .gear = sidecar_gear(leg),
        });
    }
    return ret;
}

}

void change_sidecar_part(JSONValue &json, std::optional<size_t> leg, std::function<void(JSONValue &)> const &change)
{
    if (!leg) {
        change(json);
        return;
    }
    auto legs = json.get(LegsKey).and_then([](JSONValue const &legs) { return legs.to_array(); }).value_or(JSONValue::Array { });
    if (legs.size() <= *leg) {
        legs.resize(*leg + 1, JSONValue::object());
    }
    if (!legs[*leg].is_object()) {
        legs[*leg] = JSONValue::object();
    }
    change(legs[*leg]);
    while (!legs.empty() && legs.back().is_object() && legs.back().empty()) {
        legs.pop_back();
    }
    if (legs.empty()) {
        json.erase(LegsKey);
        return;
    }
    auto list = JSONValue::array();
    for (auto const &names : legs) {
        list.append(names);
    }
    json.set(LegsKey, list);
}

std::optional<JSONValue> sidecar_part(JSONValue const &json, std::optional<size_t> leg)
{
    if (!json.is_object()) {
        return { };
    }
    if (!leg) {
        return json;
    }
    auto const legs = json.get(LegsKey).and_then([](JSONValue const &legs) { return legs.to_array(); });
    if (!legs || legs->size() <= *leg || !(*legs)[*leg].is_object()) {
        return { };
    }
    return (*legs)[*leg];
}

Decoded<StravaTitle> StravaTitle::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "StravaTitle"));
    }
    StravaTitle ret {
        .id = TRY_EVAL(json.try_get<u64>("id")),
        .name = TRY_EVAL(json.try_get<std::string>("name")),
    };
    if (auto const description = json.try_get<std::string>("description"); description) {
        ret.description = *description;
    }
    return ret;
}

JSONValue StravaTitle::encode() const
{
    auto json = JSONValue::object();
    json.set("id", JSONValue { id });
    json.set("name", JSONValue { name });
    if (description) {
        json.set("description", JSONValue { *description });
    }
    return json;
}

SidecarTitles sidecar_titles(fs::path const &sidecar)
{
    auto const json = read_sidecar(sidecar);
    if (!json) {
        return { };
    }
    SidecarTitles ret {
        .service = service_title(*json),
        .own = non_empty_string(*json, OwnTitleKey),
        .own_notes = non_empty_string(*json, OwnNotesKey),
        .gear = sidecar_gear(*json),
        .strava = strava_title(*json, sidecar),
        .legs = leg_titles(*json, sidecar),
    };
    if (json->get(StravaRejectedKey)) {
        if (auto rejected = json->try_get_array<u64>(StravaRejectedKey); rejected) {
            ret.strava_rejected = std::move(*rejected);
        } else {
            warning(Storage, "Ignoring `{}`'s rejected Strava activities: {}", sidecar.string(), rejected.error().to_string());
        }
    }
    return ret;
}

std::vector<SidecarStrava> sidecar_stravas(SidecarTitles const &titles)
{
    std::vector<SidecarStrava> ret;
    if (titles.strava) {
        ret.push_back(SidecarStrava { .title = *titles.strava });
    }
    for (size_t ix = 0; ix < titles.legs.size(); ++ix) {
        if (titles.legs[ix].strava) {
            ret.push_back(SidecarStrava { .leg = ix, .title = *titles.legs[ix].strava });
        }
    }
    return ret;
}

StravaFileNames read_strava_file(fs::path const &file)
{
    auto const json = read_sidecar(file);
    if (!json || !json->is_object()) {
        return { };
    }
    return StravaFileNames {
        .name = non_empty_string(*json, "name"),
        .description = non_empty_string(*json, "description"),
    };
}

std::optional<std::string> sidecar_title(fs::path const &sidecar)
{
    return sidecar_titles(sidecar).service;
}

std::optional<std::vector<i64>> sidecar_laps(fs::path const &sidecar)
{
    auto const json = read_sidecar(sidecar);
    if (!json || !json->is_object() || !json->get(LapsKey)) {
        return { };
    }
    auto starts = json->try_get_array<i64>(LapsKey);
    if (!starts) {
        warning(Storage, "Ignoring `{}`'s laps, which aren't a list of times: {}", sidecar.string(), starts.error().to_string());
        return { };
    }
    std::ranges::sort(*starts);
    return std::move(*starts);
}

std::expected<void, LibCError> save_sidecar_laps(fs::path const &sidecar, std::optional<std::vector<i64>> const &starts)
{
    return update_sidecar(sidecar, [&starts](JSONValue &json) {
        if (!starts) {
            json.erase(LapsKey);
            return;
        }
        auto list = JSONValue::array();
        for (auto const start : *starts) {
            list.append(JSONValue { start });
        }
        json.set(LapsKey, list);
    });
}

std::optional<std::string> sidecar_own_title(fs::path const &sidecar)
{
    return sidecar_titles(sidecar).own;
}

std::expected<void, LibCError> save_sidecar_title(fs::path const &sidecar, std::string_view title, std::optional<size_t> leg)
{
    return update_sidecar(sidecar, [title, leg](JSONValue &json) {
        change_sidecar_part(json, leg, [title](JSONValue &names) {
            if (title.empty()) {
                names.erase(OwnTitleKey);
            } else {
                names.set(OwnTitleKey, JSONValue { std::string { title } });
            }
        });
    });
}

std::expected<void, LibCError> save_sidecar_notes(fs::path const &sidecar, std::string_view notes, std::optional<size_t> leg)
{
    return update_sidecar(sidecar, [notes, leg](JSONValue &json) {
        change_sidecar_part(json, leg, [notes](JSONValue &names) {
            if (notes.empty()) {
                names.erase(OwnNotesKey);
            } else {
                names.set(OwnNotesKey, JSONValue { std::string { notes } });
            }
        });
    });
}

std::expected<void, LibCError> save_sidecar_gear(fs::path const &sidecar, std::optional<GearPick> const &pick, std::optional<size_t> leg)
{
    return update_sidecar(sidecar, [&pick, leg](JSONValue &json) {
        change_sidecar_part(json, leg, [&pick](JSONValue &part) {
            if (!pick) {
                part.erase(GearKey);
            } else if (!pick->name) {
                part.set(GearKey, JSONValue { });
            } else {
                part.set(GearKey, JSONValue { *pick->name });
            }
        });
    });
}

std::expected<void, LibCError> save_sidecar_strava(fs::path const &sidecar, StravaTitle const &strava, std::optional<size_t> leg)
{
    return update_sidecar(sidecar, [&strava, leg](JSONValue &json) {
        if (leg) {
            json.erase(StravaKey);
        }
        change_sidecar_part(json, leg, [&strava](JSONValue &names) {
            names.set(StravaKey, strava.encode());
        });
    });
}

std::expected<void, LibCError> reject_sidecar_strava(fs::path const &sidecar, u64 id)
{
    auto rejected = sidecar_titles(sidecar).strava_rejected;
    if (!std::ranges::contains(rejected, id)) {
        rejected.push_back(id);
    }
    auto const legs = sidecar_titles(sidecar).legs.size();
    return update_sidecar(sidecar, [id, &rejected, legs](JSONValue &json) {
        auto const take_off = [id](JSONValue &names) {
            if (auto const strava = names.get(StravaKey); strava) {
                if (auto const title = StravaTitle::decode(*strava); title && title->id == id) {
                    names.erase(StravaKey);
                }
            }
        };
        take_off(json);
        for (size_t leg = 0; leg < legs; ++leg) {
            change_sidecar_part(json, leg, take_off);
        }
        auto list = JSONValue::array();
        for (auto const rejected_id : rejected) {
            list.append(JSONValue { rejected_id });
        }
        json.set(StravaRejectedKey, list);
    });
}

std::expected<void, LibCError> update_sidecar(fs::path const &sidecar, std::function<void(JSONValue &)> const &change)
{
    std::error_code ec { };
    auto const      exists = fs::exists(sidecar, ec);
    if (ec) {
        return std::unexpected(LibCError { ec });
    }
    auto json = JSONValue::object();
    if (exists) {
        auto const contents = TRY_EVAL(read_file_by_name(sidecar.string()));
        auto       read = JSONValue::deserialize(contents);
        if (!read || !read->is_object()) {
            return std::unexpected(LibCError { "`{}` isn't a JSON object", sidecar.string() });
        }
        json = std::move(*read);
    }
    change(json);
    // Nothing left: the app made it, for a name, and it can go.
    if (json.empty()) {
        if (exists && !fs::remove(sidecar, ec)) {
            return std::unexpected(LibCError { ec });
        }
        return { };
    }
    return write_file_atomically(sidecar, json.serialize());
}

std::expected<void, LibCError> Activity::rename(std::string_view name)
{
    auto const trimmed = std::string { strip(name) };
    auto const new_title = trimmed.empty() ? original_title : trimmed;
    if (new_title == title) {
        return { };
    }
    if (activity_sidecar.empty()) {
        return std::unexpected(LibCError { "`{}` can't be renamed: it wasn't loaded from the store", id.name });
    }
    TRY(save_sidecar_title(activity_sidecar, trimmed, leg.transform([](LegShown const &shown) { return shown.index; })));
    title = new_title;
    return { };
}

std::expected<void, LibCError> Activity::set_gear(std::optional<GearPick> pick)
{
    if (pick == gear_pick) {
        return { };
    }
    if (activity_sidecar.empty()) {
        return std::unexpected(LibCError { "`{}`'s gear can't be saved: it wasn't loaded from the store", id.name });
    }
    TRY(save_sidecar_gear(activity_sidecar, pick, leg.transform([](LegShown const &shown) { return shown.index; })));
    gear_pick = std::move(pick);
    return { };
}

std::expected<void, LibCError> Activity::set_notes(std::string_view text)
{
    auto const trimmed = std::string { strip(text) };
    // Strava's own text, saved as it was or typed in again, isn't the
    // user's. Strava's is kept as Strava sent it, with its white space.
    auto const stravas = trimmed.empty() || trimmed == strip(original_notes);
    auto const new_notes = stravas ? original_notes : trimmed;
    if (new_notes == notes) {
        return { };
    }
    if (activity_sidecar.empty()) {
        return std::unexpected(LibCError { "`{}`'s description can't be saved: it wasn't loaded from the store", id.name });
    }
    TRY(save_sidecar_notes(activity_sidecar, stravas ? std::string_view { } : std::string_view { trimmed }, leg.transform([](LegShown const &shown) { return shown.index; })));
    notes = new_notes;
    return { };
}

StorageResult<bool> Activity::load_from_file(fs::path const &file)
{
    assert(id.files.contains(ActivityFile::Fit));
    if (!TRY_EVAL(load_fit_file_by_name(file))) {
        return false;
    }
    TRY(analyze());
    return true;
}

StorageResult<bool> Activity::load_fit_file_by_name(fs::path const &file)
{
    auto const file_name = file.string();
    auto       fit_file_maybe = FITFile::read(file_name);
    if (!fit_file_maybe.has_value()) {
        trace(Storage, "Error reading fit file `{}`", file_name);
        return std::unexpected(StorageError { fit_file_maybe.error() });
    }
    auto &fit_file = fit_file_maybe.value();
    auto  try_load = load_fit_file(fit_file);
    if (!try_load.has_value()) {
        trace(Storage, "load_fit_file(`{}`) failed", file_name);
        return std::unexpected(try_load.error());
    }
    if (!try_load.value()) {
        trace(Storage, "load_fit_file(`{}`) returned false", file_name);
        return false;
    }
    return true;
}

StorageResult<bool> Activity::load_from_slice(std::string_view const &buffer)
{
    assert(id.files.contains(ActivityFile::Fit));
    FITFile fit_file { buffer };
    if (!TRY_EVAL(load_fit_file(fit_file))) {
        return false;
    }
    TRY(analyze());
    return true;
}

// The activity's sport and numbers are its session's, or, for several, a
// multisport activity's, made from its legs.
void Activity::take_legs()
{
    if (legs.size() == 1) {
        auto const &leg = legs.front();
        segment = leg.segment;
        if (leg.sport) {
            sport = *leg.sport;
        }
        sub_sport = leg.sub_sport;
    } else if (is_multisport()) {
        segment = whole_segment(legs);
        sport = sport::Multisport;
        sub_sport = { };
    }
}

Error<StorageError> Activity::analyze()
{
    // Laps are contiguous and in chronological order, so a lap runs from its
    // own start time up to the start time of the next lap. Everything from the
    // start of the last lap onwards belongs to the last lap.
    auto const first_at_or_after = [this](DateTime const &t) {
        return std::ranges::lower_bound(records, t.timestamp, { }, [](record const &rec) { return rec.timestamp.timestamp; });
    };
    for (size_t ix = 0; ix < laps.size(); ++ix) {
        auto      &lap = laps[ix];
        auto const begin = first_at_or_after(lap.segment.start_time);
        auto       end = (ix + 1 < laps.size()) ? first_at_or_after(laps[ix + 1].segment.start_time) : records.end();
        if (end < begin) {
            end = begin;
        }
        lap.first_record = static_cast<size_t>(begin - records.begin());
        lap.end_record = static_cast<size_t>(end - records.begin());
        lap.segment.analyze(std::span { begin, end }, false);
    }
    // A shallow load, for a month's list, has neither records nor laps.
    if (is_multisport() && depth == LoadingDepth::Deep) {
        split_into_legs(*this);
    }
    // Last: it smooths the records, and the laps should see them as
    // recorded, as the activity does. So should a range, later.
    recorded = records;
    device_laps = laps;
    segment.analyze(records, true);
    return { };
}

bool Activity::load_fit_file_filter(FITDataRecord const &rec)
{
    switch (rec.mesg_num) {
    case mesg_num::file_id: {
        auto f = make_from_rec<file_id>(rec);
        if (!f) {
            warning(Storage, "Error converting file_id message to struct: {}", f.error());
            return true;
        }
        if (f->type != file_type::activity) {
            return false;
        }
        number = f->number.value_or(0);
        serial = f->serial_number.value_or(0);
        manufacturer = f->manufacturer;
        product_name = product_text(f->product_name.value_or(""));
        product = product_number(f->product);
    } break;
    case mesg_num::activity: {
        auto a = make_from_rec<ST::activity>(rec);
        if (!a) {
            warning(Storage, "Error converting activity message to struct: {}", a.error());
            return true;
        }
        if (a->local_timestamp) {
            utc_offset = static_cast<i32>(static_cast<i64>(a->local_timestamp->timestamp) - static_cast<i64>(a->timestamp.timestamp));
        }
    } break;
    case mesg_num::session: {
        auto s = make_from_rec<session>(rec);
        if (!s) {
            warning(Storage, "Error converting session message to struct: {}", s.error());
            return true;
        }
        Leg leg {
            .sport = s->sport,
            .sub_sport = s->sub_sport,
            .segment = Segment {
                .start_time = s->start_time,
                .end_time = s->timestamp,
                .elapsed = Duration { s->total_elapsed_time },
                .moving = Duration { s->total_timer_time.value_or(s->total_elapsed_time) },
                .distance = s->total_distance.value_or(0),
                .total_ascent = s->total_ascent,
            },
            .first_lap_index = s->first_lap_index,
            .num_laps = s->num_laps,
        };
        legs.push_back(std::move(leg));
    } break;
    case mesg_num::workout: {
        auto w = make_from_rec<workout>(rec);
        if (!w) {
            warning(Storage, "Error converting workout message to struct: {}", w.error());
            return true;
        }
        if (w->sport) {
            sport = *w->sport;
        }
        if (w->wkt_name && !w->wkt_name.value().empty()) {
            title = *w->wkt_name;
        }
    } break;
    case mesg_num::lap: {
        if (depth == LoadingDepth::Deep) {
            auto l = make_from_rec<ST::lap>(rec);
            if (!l) {
                warning(Storage, "Error converting lap message to struct: {}", l.error());
                return true;
            }
            Lap lap { };
            lap.segment.start_time = l->start_time;
            lap.segment.moving = Duration { l->total_timer_time.value_or(l->total_elapsed_time) };
            lap.segment.elapsed = Duration { l->total_elapsed_time };
            lap.segment.end_time = l->timestamp;
            if (l->total_distance) {
                lap.segment.distance = *l->total_distance;
            }
            laps.emplace_back(lap);
        }
    } break;
    case mesg_num::record: {
        if (depth == LoadingDepth::Deep) {
            auto r = make_from_rec<ST::record>(rec);
            if (!r) {
                warning(Storage, "Error converting record message to struct: {}", r.error());
                return true;
            }
            records.emplace_back(*r);
        }
    } break;
    case mesg_num::vendor: {
        if (depth == LoadingDepth::Deep && manufacturer == FIT::manufacturer::wahoo_fitness) {
            // A message nobody documents isn't worth giving up the
            // activity for: one that can't be read is left out.
            if (auto r = make_paused_record(rec); r) {
                paused.emplace_back(*r);
            } else {
                warning(Storage, "Error converting Wahoo's paused record to struct: {}", r.error());
            }
        }
    } break;
    default:
        break;
    }
    return false;
}

StorageResult<bool> Activity::load_fit_file(FITFile &fit_file)
{
    // zig's FITFile.openReader() reads the header as part of attaching the
    // reader; in C++ that is an explicit step.
    if (auto res = fit_file.read_header(); !res) {
        trace(Storage, "load_fit_file: read_header failed");
        return std::unexpected(StorageError { res.error() });
    }
    if (title.empty()) {
        title = id.name;
    }
    SensorList devices;
    auto const read = fit_file.read_until([this, &devices](FITDataRecord const &rec) {
        if (rec.mesg_num == mesg_num::device_info && depth == LoadingDepth::Deep) {
            if (auto const info = make_from_rec<device_info>(rec); info) {
                devices.add(*info);
            } else {
                warning(Storage, "Error converting device_info message to struct: {}", info.error());
            }
        }
        return load_fit_file_filter(rec);
    });
    if (!read) {
        trace(Storage, "load_fit_file: read_until failed");
        return std::unexpected(StorageError { read.error() });
    }
    take_legs();
    sensors = devices.sensors();
    // A Wahoo ELEMNT ROAM names itself in its device_info listing only.
    if (product_name.empty()) {
        product_name = devices.creator_name();
    }
    return segment.start_time.timestamp > 0;
}

Error<StorageError> Activities::list()
{
    if (auto d = id.dir(*storage); d) {
        std::error_code ec { };
        for (fs::directory_iterator it { *d, ec }; !ec && it != fs::directory_iterator { }; it.increment(ec)) {
            auto const     &entry = *it;
            std::error_code entry_ec { };
            if (!entry.is_regular_file(entry_ec)) {
                if (entry_ec) {
                    warning(Storage, "Skipping `{}`: {}", entry.path().string(), LibCError { entry_ec }.to_string());
                }
                continue;
            }
            auto const ext = entry.path().extension().string();
            FileTypes  file_types { };
            // The inbox stores FIT files as `<ts>.fit`, and load() opens
            // `<name>.fit`, which on a case-sensitive filesystem doesn't find
            // e.g. a `.FIT` file copied in by hand.
            if (ext == ".fit") {
                file_types.insert(ActivityFile::Fit);
            } else if (eql_ignore_case(ext, ".fit")) {
                warning(Storage, "Skipping `{}`: activity files must be named `.fit`", entry.path().string());
                // } else if (eql_ignore_case(ext, ".strava")) {
                //     file_types.insert(ActivityFile::Strava);
                // } else if (eql_ignore_case(ext, ".sweattrails")) {
                //     file_types.insert(ActivityFile::SweatTrails);
            }
            if (file_types.count() == 0) {
                continue;
            }
            auto const stem = entry.path().stem().string();
            auto       existing = std::find_if(
                activities.begin(), activities.end(),
                [&stem](Activity const &activity) { return stem == activity.id.name; });
            if (existing != activities.end()) {
                existing->id.files.set_union(file_types);
                continue;
            }
            activities.emplace_back(*storage, ActivityID { id, stem, file_types });
        }
        if (ec) {
            return std::unexpected(StorageError { LibCError { ec } });
        }
        // An activity that fails to load is left out, rather than failing the
        // whole month.
        for (auto ix = activities.size(); ix > 0;) {
            --ix;
            auto const loaded = activities[ix].load(LoadingDepth::Shallow);
            if (!loaded) {
                warning(Storage, "Skipping activity `{}`: {}", activities[ix].id.name, loaded.error().to_string());
            }
            if (!loaded.value_or(false)) {
                // swapRemove
                activities[ix] = std::move(activities.back());
                activities.pop_back();
            }
        }
        std::sort(activities.begin(), activities.end(), Activity::less_than);
    }
    return { };
}

}
