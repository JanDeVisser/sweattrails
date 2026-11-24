/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
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

char const *PartType_name(PartType type)
{
    switch (type) {
#undef S
#define S(V, N)       \
    case PartType::V: \
        return N;
        PARTTYPES(S)
#undef S
    }
    UNREACHABLE();
}

std::optional<PartType> PartType_by_name(std::string_view name)
{
#undef S
#define S(V, N)    \
    if (name == N) \
        return PartType::V;
    PARTTYPES(S)
#undef S
    return { };
}

bool fits_on(PartType type, Gear const &place)
{
    switch (type) {
    case PartType::Cassette:
        return place.part == PartType::RearWheel;
    case PartType::Tyre:
        return place.part == PartType::FrontWheel || place.part == PartType::RearWheel;
    default:
        return place.type == GearType::Bike;
    }
}

namespace {

// What a part of this type goes on, for the error when it's somewhere else.
char const *place_name(PartType type)
{
    switch (type) {
    case PartType::Cassette:
        return "a rear wheel";
    case PartType::Tyre:
        return "a front or a rear wheel";
    default:
        return "a bike";
    }
}

}

bool Swap::in_effect(chrono::year_month_day day) const
{
    return from <= day && day <= until;
}

bool Gear::retired_on(chrono::year_month_day day) const
{
    return retired && *retired < day;
}

std::optional<chrono::year_month_day> Gear::fitted_since() const
{
    return since ? since : purchased;
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

// A wear or wax limit: a distance over 0, or none.
std::expected<std::optional<float64>, std::string> limit_field(JSONValue const &entry, std::string_view field, std::string const &where)
{
    if (!entry.has(field)) {
        return std::optional<float64> { };
    }
    auto const km = entry.try_get<float64>(field);
    if (!km || *km <= 0) {
        return Problem { std::format("{}'s `{}` isn't a number over 0", where, field) };
    }
    return std::optional { *km };
}

std::string part_type_names()
{
    std::vector<std::string> names;
#undef S
#define S(V, N) names.emplace_back(N);
    PARTTYPES(S)
#undef S
    return join(names, ", ");
}

// A wax: a day, which counts from the next one, or a day and a
// time, "2026-11-20T08:00", which counts from then.
std::optional<std::pair<chrono::year_month_day, chrono::local_seconds>> parse_wax_time(std::string_view text)
{
    auto const t = text.find('T');
    auto const day = parse_day(text.substr(0, t));
    if (!day) {
        return { };
    }
    auto const midnight = chrono::local_seconds { chrono::local_days { *day } };
    if (t == std::string_view::npos) {
        return std::pair { *day, midnight + chrono::days { 1 } };
    }
    auto const clock = split(text.substr(t + 1), ':');
    if (clock.size() != 2) {
        return { };
    }
    auto const hour = string_to_integer<unsigned>(clock[0], 10);
    auto const minute = string_to_integer<unsigned>(clock[1], 10);
    if (!hour || !minute || *hour > 23 || *minute > 59) {
        return { };
    }
    return std::pair { *day, midnight + chrono::hours { *hour } + chrono::minutes { *minute } };
}

// A chain's waxes, in order.
std::expected<std::vector<Wax>, std::string> decode_waxes(JSONValue const &entry, std::string const &where)
{
    auto const list = entry.get("waxed");
    if (!list->is_array()) {
        return Problem { std::format("{}'s `waxed` isn't a list", where) };
    }
    std::vector<Wax> ret;
    for (auto ix = 0uz; ix < list->size(); ++ix) {
        auto const &wax = (*list)[static_cast<unsigned>(ix)];
        auto const  text = wax.is_string() ? wax.to_string() : std::string { };
        auto const  when = parse_wax_time(text);
        if (!when) {
            return Problem { std::format("{}'s `waxed` {} isn't a date, {}, or a date and time, as \"2026-03-14T08:00\"", where, ix + 1, DateExample) };
        }
        ret.push_back(Wax { .day = when->first, .counts_from = when->second });
    }
    std::ranges::stable_sort(ret, std::less { }, &Wax::counts_from);
    return ret;
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
    ret.version = TRY_EVAL(text_field(entry, "version", where));
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
    ret.max_km = TRY_EVAL(limit_field(entry, "max_km", where));
    ret.wax_km = TRY_EVAL(limit_field(entry, "wax_km", where));
    if (type == GearType::Part) {
        ret.part = PartType_by_name(TRY_EVAL(text_field(entry, "type", where)));
        if (!ret.part) {
            return Problem { std::format("{}'s `type` isn't one of {}", where, part_type_names()) };
        }
        ret.since = TRY_EVAL(date_field(entry, "since", where));
    }
    if (ret.wax_km && ret.part != PartType::Chain) {
        return Problem { std::format("{} has a `wax_km`, but it isn't a chain", where) };
    }
    if (entry.has("waxed")) {
        if (ret.part != PartType::Chain) {
            return Problem { std::format("{} has `waxed`, but it isn't a chain", where) };
        }
        ret.waxed = TRY_EVAL(decode_waxes(entry, where));
    }
    return ret;
}

std::expected<void, std::string> add_gear(Gear gear, GearList &into)
{
    if (into.find(gear.name) != nullptr) {
        return Problem { std::format("there are two pieces of gear called `{}`", gear.name) };
    }
    into.gear.push_back(std::move(gear));
    return { };
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
        TRY(add_gear(TRY_EVAL(decode_one((*list)[static_cast<unsigned>(ix)], type, entry_name(list_name, ix))), into));
    }
    return { };
}

std::expected<void, std::string> decode_fitted(JSONValue const &holder, Gear const &place, GearList &into);

// A part, listed in `place`, or at the top of `unfitted` without one, and
// the parts listed in it.
std::expected<void, std::string> decode_part(JSONValue const &entry, Gear const *place, std::string const &where, GearList &into)
{
    auto part = TRY_EVAL(decode_one(entry, GearType::Part, where));
    if (place != nullptr) {
        if (!fits_on(*part.part, *place)) {
            return Problem { std::format("`{}`, a {}, goes on {}, and `{}` isn't one", part.name, PartType_name(*part.part), place_name(*part.part), place->name) };
        }
        part.place = place->name;
    }
    auto const self = part; // `into.gear` may move as the nested parts are added
    TRY(add_gear(std::move(part), into));
    return decode_fitted(entry, self, into);
}

// The parts listed in `place`'s `fitted`, `holder` being its entry, and in
// theirs, depth first.
std::expected<void, std::string> decode_fitted(JSONValue const &holder, Gear const &place, GearList &into)
{
    auto const list = holder.get("fitted");
    if (!list) {
        return { };
    }
    auto const holds_parts = place.type == GearType::Bike || place.part == PartType::FrontWheel || place.part == PartType::RearWheel;
    if (!holds_parts) {
        return Problem { std::format("`{}` has `fitted`, but only bikes and wheels do", place.name) };
    }
    if (!list->is_array()) {
        return Problem { std::format("`{}`'s `fitted` isn't a list", place.name) };
    }
    for (auto ix = 0uz; ix < list->size(); ++ix) {
        TRY(decode_part((*list)[static_cast<unsigned>(ix)], &place, std::format("fitted {} on `{}`", ix + 1, place.name), into));
    }
    return { };
}

// The parts fitted on each of the shoes or bikes, after all of them.
std::expected<void, std::string> decode_trees(JSONValue const &json, GearType type, GearList &into)
{
    auto const list = json.get(GearType_name(type));
    if (!list) {
        return { };
    }
    for (auto ix = 0uz; ix < list->size(); ++ix) {
        auto const &entry = (*list)[static_cast<unsigned>(ix)];
        auto const  holder = *into.find(entry.try_get<std::string>("name").value_or(""));
        TRY(decode_fitted(entry, holder, into));
    }
    return { };
}

std::expected<void, std::string> decode_unfitted(JSONValue const &json, GearList &into)
{
    auto const list = json.get("unfitted");
    if (!list) {
        return { };
    }
    if (!list->is_array()) {
        return Problem { "`unfitted` isn't a list" };
    }
    for (auto ix = 0uz; ix < list->size(); ++ix) {
        TRY(decode_part((*list)[static_cast<unsigned>(ix)], nullptr, entry_name("unfitted", ix), into));
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
        auto        gear = TRY_EVAL(text_field(entry, "gear", where));
        auto const *found = into.find(gear);
        if (found == nullptr || found->type == GearType::Part) {
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

// A swap's part and place, checked: what `fits_on()` allows, with a last
// day on or after its first.
std::expected<void, std::string> check_swap(GearList const &list, Swap const &swap, std::string const &where)
{
    auto const *part = list.find(swap.part);
    if (part == nullptr || !part->part) {
        return Problem { std::format("{}'s `part`, `{}`, isn't a part in `bikes` or `unfitted`", where, swap.part) };
    }
    auto const *place = list.find(swap.on);
    if (place == nullptr || !(place->type == GearType::Bike || place->part == PartType::FrontWheel || place->part == PartType::RearWheel)) {
        return Problem { std::format("{}'s `on`, `{}`, isn't a bike or a wheel", where, swap.on) };
    }
    if (!fits_on(*part->part, *place)) {
        return Problem { std::format("{}: a {} goes on {}, and `{}` isn't one", where, PartType_name(*part->part), place_name(*part->part), swap.on) };
    }
    if (swap.until < swap.from) {
        return Problem { std::format("{}'s `until` is before its `from`", where) };
    }
    return { };
}

// Two swaps at once of one part, or onto one place for one type, would
// leave it unclear where the part is, or which part is there.
std::expected<void, std::string> check_overlaps(GearList const &list)
{
    for (auto ix = 0uz; ix < list.swaps.size(); ++ix) {
        auto const &swap = list.swaps[ix];
        for (auto earlier = 0uz; earlier < ix; ++earlier) {
            auto const &other = list.swaps[earlier];
            if (other.until < swap.from || swap.until < other.from) {
                continue;
            }
            if (other.part == swap.part) {
                return Problem { std::format("swap {} and swap {} put `{}` in two places at once", earlier + 1, ix + 1, swap.part) };
            }
            auto const type = list.find(swap.part)->part;
            if (other.on == swap.on && list.find(other.part)->part == type) {
                return Problem { std::format("swap {} and swap {} both put a {} on `{}` at once", earlier + 1, ix + 1, PartType_name(*type), swap.on) };
            }
        }
    }
    return { };
}

std::expected<void, std::string> decode_swaps(JSONValue const &json, GearList &into)
{
    auto const list = json.get("swaps");
    if (!list) {
        return { };
    }
    if (!list->is_array()) {
        return Problem { "`swaps` isn't a list" };
    }
    for (auto ix = 0uz; ix < list->size(); ++ix) {
        auto const &entry = (*list)[static_cast<unsigned>(ix)];
        auto const  where = entry_name("swap", ix);
        if (!entry.is_object()) {
            return Problem { std::format("{} isn't an object", where) };
        }
        auto const from = TRY_EVAL(date_field(entry, "from", where));
        if (!from) {
            return Problem { std::format("{} has no `from`, its first day, {}", where, DateExample) };
        }
        auto const until = TRY_EVAL(date_field(entry, "until", where));
        if (!until) {
            return Problem { std::format("{} has no `until`, its last day, {}: a lasting change goes in `fitted`", where, DateExample) };
        }
        Swap swap {
            .part = TRY_EVAL(text_field(entry, "part", where)),
            .on = TRY_EVAL(text_field(entry, "on", where)),
            .from = *from,
            .until = *until,
        };
        if (strip(swap.part).empty()) {
            return Problem { std::format("{} has no `part`, the part it swaps in", where) };
        }
        if (strip(swap.on).empty()) {
            return Problem { std::format("{} has no `on`, the bike or wheel it swaps the part onto", where) };
        }
        TRY(check_swap(into, swap, where));
        into.swaps.push_back(std::move(swap));
    }
    return check_overlaps(into);
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
    static constexpr std::array Keys { "shoes", "bikes", "unfitted", "defaults", "swaps" };
    for (auto const &[key, value] : json.to_object().value_or(JSONValue::Object { })) {
        if (std::ranges::find(Keys, key) == Keys.end()) {
            return Problem { std::format("`{}` isn't one of shoes, bikes, unfitted, defaults or swaps", key) };
        }
    }
    GearList ret;
    TRY(decode_gear_list(json, GearType::Shoes, ret));
    TRY(decode_gear_list(json, GearType::Bike, ret));
    // Shoes have no parts, but `fitted` on them gets its error here.
    TRY(decode_trees(json, GearType::Shoes, ret));
    TRY(decode_trees(json, GearType::Bike, ret));
    TRY(decode_unfitted(json, ret));
    TRY(decode_defaults(json, ret));
    TRY(decode_swaps(json, ret));
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

namespace {

bool holds(Gear const &gear, PartType type)
{
    return gear.part == type;
}

// The swap in effect on `day` that puts a part of `type` on `place`.
Swap const *swap_onto(GearList const &list, std::string_view place, PartType type, chrono::year_month_day day)
{
    auto const it = std::ranges::find_if(list.swaps, [&](Swap const &swap) {
        return swap.on == place && swap.in_effect(day) && holds(*list.find(swap.part), type);
    });
    return it == list.swaps.end() ? nullptr : &*it;
}

// The swap in effect on `day` that takes `part` elsewhere.
Swap const *swap_of(GearList const &list, std::string_view part, chrono::year_month_day day)
{
    auto const it = std::ranges::find_if(list.swaps, [&](Swap const &swap) {
        return swap.part == part && swap.in_effect(day);
    });
    return it == list.swaps.end() ? nullptr : &*it;
}

// Of the parts of `type` listed in `place`, the one fitted there last on
// or before `day`; the later in the file of two fitted on one day.
Gear const *resident_at(GearList const &list, std::string_view place, PartType type, chrono::year_month_day day)
{
    Gear const *ret = nullptr;
    for (auto const &gear : list.gear) {
        if (place.empty() || gear.place != place || !holds(gear, type)) {
            continue;
        }
        auto const since = gear.fitted_since();
        if (since && *since > day) {
            continue;
        }
        // An optional without a day compares before any day: always there.
        if (ret == nullptr || since >= ret->fitted_since()) {
            ret = &gear;
        }
    }
    return ret;
}

}

Gear const *part_at(GearList const &list, std::string_view place, PartType type, chrono::year_month_day day)
{
    Gear const *ret = nullptr;
    if (auto const *swap = swap_onto(list, place, type, day); swap != nullptr) {
        ret = list.find(swap->part);
    } else {
        ret = resident_at(list, place, type, day);
        if (ret != nullptr && swap_of(list, ret->name, day) != nullptr) {
            return nullptr;
        }
    }
    return (ret == nullptr || ret->retired_on(day)) ? nullptr : ret;
}

Gear const *place_of(GearList const &list, std::string_view part, chrono::year_month_day day)
{
    auto const *gear = list.find(part);
    if (gear == nullptr || !gear->part || gear->retired_on(day)) {
        return nullptr;
    }
    if (auto const *swap = swap_of(list, part, day); swap != nullptr) {
        return list.find(swap->on);
    }
    if (part_at(list, gear->place, *gear->part, day) != gear) {
        return nullptr;
    }
    return list.find(gear->place);
}

std::vector<Gear const *> parts_used(GearList const &list, std::string_view bike, chrono::year_month_day day, bool indoor)
{
    std::vector<Gear const *> ret;
    auto const                add = [&ret](Gear const *part) {
        if (part != nullptr) {
            ret.push_back(part);
        }
    };
    add(part_at(list, bike, PartType::Chainset, day));
    add(part_at(list, bike, PartType::Chain, day));
    if (indoor) {
        return ret;
    }
    if (auto const *front = part_at(list, bike, PartType::FrontWheel, day); front != nullptr) {
        add(front);
        add(part_at(list, front->name, PartType::Tyre, day));
    }
    if (auto const *rear = part_at(list, bike, PartType::RearWheel, day); rear != nullptr) {
        add(rear);
        add(part_at(list, rear->name, PartType::Cassette, day));
        add(part_at(list, rear->name, PartType::Tyre, day));
    }
    return ret;
}

std::optional<ActivityGear> activity_gear(GearList const &list, std::optional<GearPick> const &pick,
    std::vector<GearKind> const &kinds, chrono::year_month_day day)
{
    if (pick) {
        if (!pick->name) {
            return { };
        }
        // A part is never an activity's gear: only a hand-edited sidecar
        // could pick one.
        auto const *gear = list.find(*pick->name);
        return ActivityGear { .name = *pick->name, .unknown = gear == nullptr || gear->part.has_value() };
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
        if (gear.type == GearType::Part) {
            continue;
        }
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
    if (auto const *gear = picked ? list.find(*picked) : nullptr; picked && (gear == nullptr || gear->part)) {
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

bool GearTotal::worn() const
{
    return gear && !gear->retired && gear->max_km && km > *gear->max_km;
}

bool GearTotal::needs_wax() const
{
    return gear && !gear->retired && gear->wax_km && (wax ? wax->km : km) > *gear->wax_km;
}

namespace {

// A chain's last wax on or before `today`.
Wax const *last_wax(Gear const &chain, chrono::year_month_day today)
{
    Wax const *ret = nullptr;
    for (auto const &wax : chain.waxed) {
        if (wax.day <= today) {
            ret = &wax; // In order, so the last is the latest
        }
    }
    return ret;
}

// Where a part is on `today`, if that's on a bike: the bike, or the wheel
// on it. Empty for a part on nothing, or on a wheel that's on nothing.
std::string on_a_bike(GearList const &list, Gear const &part, chrono::year_month_day today)
{
    auto const *place = place_of(list, part.name, today);
    if (place == nullptr || (place->type != GearType::Bike && place_of(list, place->name, today) == nullptr)) {
        return { };
    }
    return place->name;
}

}

std::vector<GearTotal> gear_totals(GearList const &list, ActivitySummaries const &summaries, GearPicks const &picks,
    chrono::year_month_day today)
{
    std::map<std::string, GearTotal> by_name;
    // Picked names that aren't shoes or a bike, apart from the gear: a
    // part's name picked by hand is unknown, not a ride of the part.
    std::map<std::string, GearTotal>             unknown;
    std::map<std::string, std::set<std::string>> files; // To count a multisport file once
    std::map<std::string, std::set<std::string>> unknown_files;
    std::map<std::string, Wax const *>           waxes;
    for (auto const &gear : list.gear) {
        auto &total = by_name[gear.name] = GearTotal { .name = gear.name, .gear = gear, .km = gear.start_km };
        if (gear.part) {
            total.on = on_a_bike(list, gear, today);
        }
        if (auto const *wax = last_wax(gear, today); wax != nullptr) {
            total.wax = WaxTotal { .day = wax->day };
            waxes[gear.name] = wax;
        }
    }
    auto const add = [&](std::string const &name, ActivitySummary const &summary, bool is_unknown = false) -> GearTotal & {
        auto &total = (is_unknown ? unknown : by_name)[name];
        total.name = name;
        total.km += summary.distance / 1000.0;
        if ((is_unknown ? unknown_files : files)[name].insert(summary.source.file).second) {
            ++total.activities;
        }
        return total;
    };
    for (auto const &summary : summaries) {
        if (!summary.sport) {
            continue;
        }
        auto const found = picks.find(pick_key(summary));
        auto const pick = found == picks.end() ? std::nullopt : std::optional { found->second };
        auto const kinds = gear_kinds(summary.sport, summary.sub_sport);
        auto const day = start_day(summary);
        auto const used = activity_gear(list, pick, kinds, day);
        if (!used) {
            continue;
        }
        add(used->name, summary, used->unknown);
        if (used->unknown) {
            continue;
        }
        auto const *bike = list.find(used->name);
        if (bike == nullptr || bike->type != GearType::Bike) {
            continue;
        }
        auto const indoor = !kinds.empty() && kinds.front() == GearKind::Indoor;
        for (auto const *part : parts_used(list, bike->name, day, indoor)) {
            auto      &total = add(part->name, summary);
            auto const wax = waxes.find(part->name);
            if (wax != waxes.end() && local_start(summary) >= wax->second->counts_from) {
                total.wax->km += summary.distance / 1000.0;
            }
        }
    }

    // Shoes, bikes, then parts, in use before retired, each in the file's
    // order; then the unknown names, by name.
    std::vector<GearTotal> ret;
    for (auto const type : { GearType::Shoes, GearType::Bike, GearType::Part }) {
        for (auto const retired : { false, true }) {
            for (auto const &gear : list.gear) {
                if (gear.type == type && gear.retired.has_value() == retired) {
                    ret.push_back(by_name[gear.name]);
                }
            }
        }
    }
    for (auto const &[name, total] : unknown) {
        ret.push_back(total);
    }
    return ret;
}

}
