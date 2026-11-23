/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The user's gear, shoes, bikes and the bikes' parts, from `gear.json` in
 * the data directory, which the user writes by hand: which gear an
 * activity uses, picked or by default, where each part is fitted, and
 * the distance on each. See specs/67-gear.md and
 * specs/68-bike-components.md.
 */

#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <JSON.h>

#include <fit/Profile.h>
#include <storage/Activity.h>
#include <storage/Summaries.h>

namespace ST {

struct Storage;

#define GEARTYPES(S)  \
    S(Shoes, "shoes") \
    S(Bike, "bikes")  \
    S(Part, "parts")

enum class GearType {
#undef S
#define S(V, N) V,
    GEARTYPES(S)
#undef S
};

// The kinds of activity a default is for, as `gear.json` names them.
#define GEARKINDS(S)        \
    S(Running, "running")   \
    S(Road, "road")         \
    S(Gravel, "gravel")     \
    S(Mountain, "mountain") \
    S(TimeTrial, "tt")      \
    S(Indoor, "indoor")     \
    S(Cycling, "cycling")

enum class GearKind {
#undef S
#define S(V, N) V,
    GEARKINDS(S)
#undef S
};

// The parts of a bike that wear, as a part's `type` names them.
#define PARTTYPES(S)             \
    S(Chainset, "chainset")      \
    S(FrontWheel, "front wheel") \
    S(RearWheel, "rear wheel")   \
    S(Cassette, "cassette")      \
    S(Chain, "chain")            \
    S(Tyre, "tyre")

enum class PartType {
#undef S
#define S(V, N) V,
    PARTTYPES(S)
#undef S
};

char const             *GearType_name(GearType type);
char const             *GearKind_name(GearKind kind);
std::optional<GearKind> GearKind_by_name(std::string_view name);
char const             *PartType_name(PartType type);
std::optional<PartType> PartType_by_name(std::string_view name);

struct Gear;

// Whether a part of this type is fitted on `place`: a chainset, chain or
// wheel on a bike, a cassette on a rear wheel, a tyre on either wheel.
[[nodiscard]] bool fits_on(PartType type, Gear const &place);

struct Gear {
    GearType                                   type { GearType::Shoes };
    std::string                                name { }; // Its key: unique over shoes and bikes
    std::string                                brand { };
    std::string                                model { };
    std::string                                version { }; // The model's: the 5 of an Endorphin Speed 5
    std::string                                colourway { };
    std::optional<int>                         year { }; // A bike's model year
    std::optional<std::chrono::year_month_day> purchased { };
    std::string                                where { };
    std::string                                price { };      // Free text, with its currency if the user writes one
    float64                                    start_km { 0 }; // What it did before SweatTrails knew about it
    std::optional<std::chrono::year_month_day> retired { };
    std::optional<PartType>                    part { };   // A part's type; none for shoes and bikes
    std::optional<float64>                     max_km { }; // When it's worn out
    std::optional<float64>                     wax_km { }; // When a chain needs waxing again

    // Whether it was out of use on `day`: retired before it.
    [[nodiscard]] bool retired_on(std::chrono::year_month_day day) const;

    bool operator==(Gear const &) const = default;
};

// From `from` on, `gear` is the default for activities of `kind`, until a
// later entry for the same kind.
struct GearDefault {
    GearKind                    kind { GearKind::Running };
    std::string                 gear { };
    std::chrono::year_month_day from { };

    bool operator==(GearDefault const &) const = default;
};

// From `from` on, until `until` if it has one, `part` is fitted on `on`,
// a bike or a wheel.
struct Fitting {
    std::string                                part { };
    std::string                                on { };
    std::chrono::year_month_day                from { };
    std::optional<std::chrono::year_month_day> until { }; // The last day it applies

    [[nodiscard]] bool in_effect(std::chrono::year_month_day day) const;

    bool operator==(Fitting const &) const = default;
};

// A chain waxed on `day`. Rides that start at or after `counts_from`, in
// local time, are after the wax: the next midnight, as a chain is waxed
// after the day's ride, or the time the file gives.
struct Wax {
    std::string                 chain { };
    std::chrono::year_month_day day { };
    std::chrono::local_seconds  counts_from { };

    bool operator==(Wax const &) const = default;
};

struct GearList {
    std::vector<Gear>        gear { };     // Shoes, then bikes, then parts, each as in the file
    std::vector<GearDefault> defaults { }; // These three in order of their dates
    std::vector<Fitting>     fitted { };
    std::vector<Wax>         waxed { };

    [[nodiscard]] Gear const *find(std::string_view name) const;

    bool operator==(GearList const &) const = default;
};

// `gear.json`'s contents, or what's wrong with them: a field of the wrong
// type, a date that isn't YYYY-MM-DD, a kind or part type it doesn't
// know, two pieces of gear of the same name, a default naming gear that
// isn't shoes or a bike, a part fitted on what it can't go on, or a wax
// of something that isn't a chain.
[[nodiscard]] std::expected<GearList, std::string> decode_gear(JSONValue const &json);

[[nodiscard]] std::filesystem::path gear_path(Storage const &storage);

// The gear in the file, none if there's no file, or why it can't be read,
// with the file's name.
[[nodiscard]] std::expected<std::optional<GearList>, std::string> read_gear(std::filesystem::path const &path);

// The kinds of an activity of this sport and sub-sport, most specific
// first, for the defaults: { road, cycling } for a road ride. None for a
// walk, a hike, or anything else without gear by default.
[[nodiscard]] std::vector<GearKind> gear_kinds(std::optional<FIT::sport> sport, std::optional<FIT::sub_sport> sub_sport);

// The default gear on `day` for an activity of `kinds`: the latest entry
// dated on or before it, for the first of the kinds that has one. None if
// that entry's gear was retired before `day`: a retirement ends the
// default.
[[nodiscard]] std::optional<std::string> default_gear(GearList const &list, std::vector<GearKind> const &kinds, std::chrono::year_month_day day);

// Where `part` is fitted on `day`: its latest entry in effect, if no later
// part has taken its place there. None if it isn't fitted, or was
// retired before `day`.
[[nodiscard]] Fitting const *fitting_of(GearList const &list, std::string_view part, std::chrono::year_month_day day);

// The part of `type` fitted on `place` on `day`: of the entries in effect
// there, the latest whose part is still there. None if there's none, or
// if that part was retired before `day`: the part it replaced doesn't
// come back.
[[nodiscard]] Gear const *part_at(GearList const &list, std::string_view place, PartType type, std::chrono::year_month_day day);

// The parts a ride on `bike` wears on `day`: its chainset, chain and
// wheels, the front wheel's tyre, and the rear wheel's cassette and tyre.
// Only the chainset and chain `indoor`, as the wheels don't turn on a
// direct-drive trainer.
[[nodiscard]] std::vector<Gear const *> parts_used(GearList const &list, std::string_view bike, std::chrono::year_month_day day, bool indoor);

// The gear an activity uses, as shown.
struct ActivityGear {
    std::string name { };
    bool        from_default { false }; // Not picked: a guess
    bool        unknown { false };      // Picked, but not in the list

    bool operator==(ActivityGear const &) const = default;
};

// The pick if there is one, else the default. None for a pick of no gear,
// or no default.
[[nodiscard]] std::optional<ActivityGear> activity_gear(GearList const &list, std::optional<GearPick> const &pick,
    std::vector<GearKind> const &kinds, std::chrono::year_month_day day);

// "Canyon", or "Old Canyon (unknown)".
[[nodiscard]] std::string gear_text(ActivityGear const &gear);

// A line of the picker: what it says, and the pick it saves, none for the
// default.
struct GearChoice {
    std::string             text { };
    std::optional<GearPick> pick { };

    bool operator==(GearChoice const &) const = default;
};

struct GearChoices {
    std::vector<GearChoice> choices { };
    size_t                  current { 0 }; // The line of the activity's choice now
};

// The picker's lines for an activity of `kinds` on `day`, whose pick is
// `pick`: "Default (Canyon)", "None", then the gear that fits it, shoes
// for a run, bikes for a ride, both for anything else. Gear retired
// before `day` is left out, unless it's the pick, as is a pick of gear
// that isn't in the list.
[[nodiscard]] GearChoices gear_choices(GearList const &list, std::vector<GearKind> const &kinds,
    std::optional<GearPick> const &pick, std::chrono::year_month_day day);

// Which pick a summary's sidecar has: by its file, and its leg's index
// for a leg of a multisport file.
struct GearPickKey {
    std::string        file { };
    std::optional<u32> leg { };

    auto operator<=>(GearPickKey const &) const = default;
};

using GearPicks = std::map<GearPickKey, GearPick>;

[[nodiscard]] GearPickKey pick_key(ActivitySummary const &summary);

// The picks in the sidecars of the summaries' files. Reads a sidecar per
// file that has one: for a job.
[[nodiscard]] GearPicks read_gear_picks(Storage const &storage, ActivitySummaries const &summaries);

// A chain's last wax, and the distance it did since.
struct WaxTotal {
    std::chrono::year_month_day day { };
    float64                     km { 0 };

    bool operator==(WaxTotal const &) const = default;
};

// A row of the Gear screen: a piece of gear, or a name picked in sidecars
// that isn't in the list, with the activities it was used for and its
// distance, start_km and theirs. For a part, where it's fitted today, and
// for a waxed chain, its last wax.
struct GearTotal {
    std::string             name { };
    std::optional<Gear>     gear { }; // None for a name that isn't in the list
    size_t                  activities { 0 };
    float64                 km { 0 };
    std::optional<Fitting>  fitted { };
    std::optional<WaxTotal> wax { };

    [[nodiscard]] bool unknown() const { return !gear.has_value(); }
    // Over its max_km: time to replace it. Never for retired gear.
    [[nodiscard]] bool worn() const;
    // A chain over its wax_km since its last wax, or in all if it was
    // never waxed. Never for a retired chain.
    [[nodiscard]] bool needs_wax() const;
    bool               operator==(GearTotal const &) const = default;
};

// A row per piece of gear, shoes, bikes then parts, with retired gear at
// the bottom of its group, then a row per unknown name. A multisport file
// counts once for each piece of gear its legs used. A ride counts for the
// parts on its bike that day too. `today` is for where the parts are.
[[nodiscard]] std::vector<GearTotal> gear_totals(GearList const &list, ActivitySummaries const &summaries, GearPicks const &picks,
    std::chrono::year_month_day today);

}
