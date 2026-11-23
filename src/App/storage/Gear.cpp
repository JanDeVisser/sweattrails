/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <format>
#include <set>
#include <system_error>

#include <FileIO.h>
#include <StringUtil.h>

#include <storage/Gear.h>
#include <storage/Storage.h>
#include <storage/Volume.h>

namespace ST {

namespace chrono = std::chrono;

char const *GearType_name(GearType type)
{
    switch (type) {
#undef S
#define S(V, N)       \
    case GearType::V: \
        return N;
        GEARTYPES(S)
#undef S
    }
    UNREACHABLE();
}

char const *GearKind_name(GearKind kind)
{
    switch (kind) {
#undef S
#define S(V, N)       \
    case GearKind::V: \
        return N;
        GEARKINDS(S)
#undef S
    }
    UNREACHABLE();
}

std::optional<GearKind> GearKind_by_name(std::string_view name)
{
#undef S
#define S(V, N)    \
    if (name == N) \
        return GearKind::V;
    GEARKINDS(S)
#undef S
    return { };
}

bool Gear::retired_on(chrono::year_month_day day) const
{
    return retired && *retired < day;
}

Gear const *GearList::find(std::string_view name) const
{
    auto const it = std::ranges::find(gear, name, &Gear::name);
    return it == gear.end() ? nullptr : &*it;
}

namespace {

using Problem = std::unexpected<std::string>;

constexpr char const DateExample[] = "as \"2026-03-14\"";

// "shoes 2", "bikes 1", "default 3": which entry of the file is wrong,
// counting from 1.
std::string entry_name(std::string_view list, size_t ix)
{
    return std::format("{} {}", list, ix + 1);
}

std::expected<std::string, std::string> text_field(JSONValue const &entry, std::string_view field, std::string const &where)
{
    if (!entry.has(field)) {
        return std::string { };
    }
    auto const text = entry.try_get<std::string>(field);
    if (!text) {
        return Problem { std::format("{}'s `{}` isn't text", where, field) };
    }
    return *text;
}

std::expected<std::optional<chrono::year_month_day>, std::string> date_field(JSONValue const &entry, std::string_view field, std::string const &where)
{
    if (!entry.has(field)) {
        return std::optional<chrono::year_month_day> { };
    }
    auto const text = entry.try_get<std::string>(field);
    auto const day = text ? parse_day(*text) : std::nullopt;
    if (!day) {
        return Problem { std::format("{}'s `{}` isn't a date, {}", where, field, DateExample) };
    }
    return day;
}

std::expected<Gear, std::string> decode_one(JSONValue const &entry, GearType type, std::string const &where)
{
    if (!entry.is_object()) {
        return Problem { std::format("{} isn't an object", where) };
    }
    Gear ret { .type = type };
    ret.name = TRY_EVAL(text_field(entry, "name", where));
    if (strip(ret.name).empty()) {
        return Problem { std::format("{} has no `name`", where) };
    }
    ret.brand = TRY_EVAL(text_field(entry, "brand", where));
    ret.model = TRY_EVAL(text_field(entry, "model", where));
    ret.model_number = TRY_EVAL(text_field(entry, "model_number", where));
    ret.colourway = TRY_EVAL(text_field(entry, "colourway", where));
    ret.where = TRY_EVAL(text_field(entry, "where", where));
    ret.price = TRY_EVAL(text_field(entry, "price", where));
    ret.purchased = TRY_EVAL(date_field(entry, "purchased", where));
    ret.retired = TRY_EVAL(date_field(entry, "retired", where));
    if (entry.has("year")) {
        auto const year = entry.try_get<int>("year");
        if (!year) {
            return Problem { std::format("{}'s `year` isn't a number", where) };
        }
        ret.year = *year;
    }
    if (entry.has("start_km")) {
        auto const km = entry.try_get<float64>("start_km");
        if (!km || *km < 0) {
            return Problem { std::format("{}'s `start_km` isn't a number of 0 or more", where) };
        }
        ret.start_km = *km;
    }
    return ret;
}

std::expected<void, std::string> decode_gear_list(JSONValue const &json, GearType type, GearList &into)
{
    auto const *list_name = GearType_name(type);
    auto const  list = json.get(list_name);
    if (!list) {
        return { };
    }
    if (!list->is_array()) {
        return Problem { std::format("`{}` isn't a list", list_name) };
    }
    for (auto ix = 0uz; ix < list->size(); ++ix) {
        auto gear = TRY_EVAL(decode_one((*list)[static_cast<unsigned>(ix)], type, entry_name(list_name, ix)));
        if (into.find(gear.name) != nullptr) {
            return Problem { std::format("there are two pieces of gear called `{}`", gear.name) };
        }
        into.gear.push_back(std::move(gear));
    }
    return { };
}

std::string kind_names()
{
    std::vector<std::string> names;
#undef S
#define S(V, N) names.emplace_back(N);
    GEARKINDS(S)
#undef S
    return join(names, ", ");
}

std::expected<void, std::string> decode_defaults(JSONValue const &json, GearList &into)
{
    auto const list = json.get("defaults");
    if (!list) {
        return { };
    }
    if (!list->is_array()) {
        return Problem { "`defaults` isn't a list" };
    }
    for (auto ix = 0uz; ix < list->size(); ++ix) {
        auto const &entry = (*list)[static_cast<unsigned>(ix)];
        auto const  where = entry_name("default", ix);
        if (!entry.is_object()) {
            return Problem { std::format("{} isn't an object", where) };
        }
        auto const kind = GearKind_by_name(TRY_EVAL(text_field(entry, "for", where)));
        if (!kind) {
            return Problem { std::format("{}'s `for` isn't one of {}", where, kind_names()) };
        }
        auto gear = TRY_EVAL(text_field(entry, "gear", where));
        if (into.find(gear) == nullptr) {
            return Problem { std::format("{}'s `gear`, `{}`, isn't in `shoes` or `bikes`", where, gear) };
        }
        auto const from = TRY_EVAL(date_field(entry, "from", where));
        if (!from) {
            return Problem { std::format("{} has no `from`, the date it's the default from, {}", where, DateExample) };
        }
        into.defaults.push_back(GearDefault { .kind = *kind, .gear = std::move(gear), .from = *from });
    }
    // In order of their dates, so that the latest before a day is the
    // last; the file's order among those of the same day.
    std::ranges::stable_sort(into.defaults, std::less { }, &GearDefault::from);
    return { };
}

}

std::expected<GearList, std::string> decode_gear(JSONValue const &json)
{
    if (!json.is_object()) {
        return Problem { "it isn't an object" };
    }
    // The file is written by hand: a misspelt list, "shoe", would
    // otherwise leave its gear out without a word. Unknown fields in an
    // entry are fine, for notes.
    for (auto const &[key, value] : json.to_object().value_or(JSONValue::Object { })) {
        if (key != "shoes" && key != "bikes" && key != "defaults") {
            return Problem { std::format("`{}` isn't one of shoes, bikes or defaults", key) };
        }
    }
    GearList ret;
    TRY(decode_gear_list(json, GearType::Shoes, ret));
    TRY(decode_gear_list(json, GearType::Bike, ret));
    TRY(decode_defaults(json, ret));
    return ret;
}

std::filesystem::path gear_path(Storage const &storage)
{
    return storage.data_dir / "gear.json";
}

std::expected<std::optional<GearList>, std::string> read_gear(std::filesystem::path const &path)
{
    std::error_code ec { };
    if (!std::filesystem::exists(path, ec)) {
        return std::optional<GearList> { };
    }
    auto const text = read_file_by_name(path.string());
    if (!text) {
        return Problem { std::format("{} can't be read: {}", path.string(), text.error().to_string()) };
    }
    auto const json = JSONValue::deserialize(*text);
    if (!json) {
        return Problem { std::format("{} can't be read: {}", path.string(), json.error().to_string()) };
    }
    auto list = decode_gear(*json);
    if (!list) {
        return Problem { std::format("{} can't be read: {}", path.string(), list.error()) };
    }
    return std::optional<GearList> { std::move(*list) };
}

std::vector<GearKind> gear_kinds(std::optional<FIT::sport> sport, std::optional<FIT::sub_sport> sub_sport)
{
    using enum GearKind;
    using FIT::sub_sport;
    if (sport == FIT::sport::Running || sub_sport == sub_sport::treadmill || sub_sport == sub_sport::indoor_running) {
        return { Running };
    }
    // A trainer's ride is indoor whatever the sport the device gave it.
    if (sub_sport == sub_sport::indoor_cycling || sub_sport == sub_sport::spin) {
        return { Indoor, Cycling };
    }
    if (sport != FIT::sport::Cycling) {
        return { };
    }
    switch (sub_sport.value_or(sub_sport::generic)) {
    case sub_sport::virtual_activity:
        return { Indoor, Cycling };
    case sub_sport::road:
        return { Road, Cycling };
    case sub_sport::gravel_cycling:
    case sub_sport::cyclocross:
    case sub_sport::mixed_surface:
        return { Gravel, Cycling };
    case sub_sport::mountain:
    case sub_sport::downhill:
    case sub_sport::e_bike_mountain:
        return { Mountain, Cycling };
    default:
        // FIT has no time trial: a TT bike is picked.
        return { Cycling };
    }
}

std::optional<std::string> default_gear(GearList const &list, std::vector<GearKind> const &kinds, chrono::year_month_day day)
{
    for (auto const kind : kinds) {
        GearDefault const *latest = nullptr;
        for (auto const &entry : list.defaults) {
            if (entry.from > day) {
                break;
            }
            if (entry.kind == kind) {
                latest = &entry;
            }
        }
        if (latest == nullptr) {
            continue;
        }
        if (auto const *gear = list.find(latest->gear); gear == nullptr || gear->retired_on(day)) {
            return { };
        }
        return latest->gear;
    }
    return { };
}

std::optional<ActivityGear> activity_gear(GearList const &list, std::optional<GearPick> const &pick,
    std::vector<GearKind> const &kinds, chrono::year_month_day day)
{
    if (pick) {
        if (!pick->name) {
            return { };
        }
        return ActivityGear { .name = *pick->name, .unknown = list.find(*pick->name) == nullptr };
    }
    return default_gear(list, kinds, day).transform([](std::string name) {
        return ActivityGear { .name = std::move(name), .from_default = true };
    });
}

std::string gear_text(ActivityGear const &gear)
{
    return gear.unknown ? std::format("{} (unknown)", gear.name) : gear.name;
}

GearChoices gear_choices(GearList const &list, std::vector<GearKind> const &kinds,
    std::optional<GearPick> const &pick, chrono::year_month_day day)
{
    GearChoices ret;
    ret.choices.push_back(GearChoice {
        .text = std::format("Default ({})", default_gear(list, kinds, day).value_or("none")),
    });
    ret.choices.push_back(GearChoice { .text = "None", .pick = GearPick { } });
    if (pick && !pick->name) {
        ret.current = 1;
    }
    auto const picked = pick ? pick->name : std::nullopt;
    auto const shoes = kinds.empty() || kinds.front() == GearKind::Running;
    auto const bikes = kinds.empty() || kinds.front() != GearKind::Running;
    for (auto const &gear : list.gear) {
        auto const fits = (gear.type == GearType::Shoes) ? shoes : bikes;
        auto const is_pick = gear.name == picked;
        if (!is_pick && (!fits || gear.retired_on(day))) {
            continue;
        }
        if (is_pick) {
            ret.current = ret.choices.size();
        }
        ret.choices.push_back(GearChoice { .text = gear.name, .pick = GearPick { .name = gear.name } });
    }
    // A pick of gear renamed or taken out of the file stays its choice.
    if (picked && list.find(*picked) == nullptr) {
        ret.current = ret.choices.size();
        ret.choices.push_back(GearChoice {
            .text = gear_text(ActivityGear { .name = *picked, .unknown = true }),
            .pick = pick,
        });
    }
    return ret;
}

GearPickKey pick_key(ActivitySummary const &summary)
{
    return GearPickKey { .file = summary.source.file, .leg = summary.leg };
}

GearPicks read_gear_picks(Storage const &storage, ActivitySummaries const &summaries)
{
    GearPicks                ret;
    std::string              file { };
    std::optional<JSONValue> sidecar { };
    for (auto const &summary : summaries) {
        // A multisport file's legs are next to each other: one read.
        if (summary.source.file != file) {
            file = summary.source.file;
            sidecar = read_sidecar((storage.activity_dir / file).replace_extension(".json"));
        }
        if (!sidecar) {
            continue;
        }
        auto const part = sidecar_part(*sidecar, summary.leg.transform([](u32 leg) { return static_cast<size_t>(leg); }));
        if (auto pick = part.and_then(sidecar_gear); pick) {
            ret.emplace(pick_key(summary), std::move(*pick));
        }
    }
    return ret;
}

std::vector<GearTotal> gear_totals(GearList const &list, ActivitySummaries const &summaries, GearPicks const &picks)
{
    std::map<std::string, GearTotal>             by_name;
    std::map<std::string, std::set<std::string>> files; // To count a multisport file once
    for (auto const &gear : list.gear) {
        by_name[gear.name] = GearTotal { .name = gear.name, .gear = gear, .km = gear.start_km };
    }
    for (auto const &summary : summaries) {
        if (!summary.sport) {
            continue;
        }
        auto const found = picks.find(pick_key(summary));
        auto const pick = found == picks.end() ? std::nullopt : std::optional { found->second };
        auto const used = activity_gear(list, pick, gear_kinds(summary.sport, summary.sub_sport), start_day(summary));
        if (!used) {
            continue;
        }
        auto &total = by_name[used->name];
        total.name = used->name;
        total.km += summary.distance / 1000.0;
        if (files[used->name].insert(summary.source.file).second) {
            ++total.activities;
        }
    }

    // Shoes, then bikes, in use before retired, each in the file's order;
    // then the unknown names, by name.
    std::vector<GearTotal> ret;
    for (auto const type : { GearType::Shoes, GearType::Bike }) {
        for (auto const retired : { false, true }) {
            for (auto const &gear : list.gear) {
                if (gear.type == type && gear.retired.has_value() == retired) {
                    ret.push_back(by_name[gear.name]);
                }
            }
        }
    }
    for (auto const &[name, total] : by_name) {
        if (total.unknown()) {
            ret.push_back(total);
        }
    }
    return ret;
}

}
