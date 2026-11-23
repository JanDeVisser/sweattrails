/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The Gear screen (specs/67-gear.md, specs/68-bike-components.md): a row
 * per piece of gear in gear.json, shoes, bikes then parts, with the
 * activities it was used for and the distance on it, where a part is
 * fitted and a chain's distance since its wax, and a row per name picked
 * in sidecars that isn't in the file.
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Table.h>
#include <Widget.h>

#include <storage/Gear.h>
#include <storage/Summaries.h>

namespace ST {

// The Gear screen's columns, by their index in a row.
struct GearColumn {
    static constexpr size_t Name = 0;
    static constexpr size_t Model = 1;
    static constexpr size_t Since = 2;
    static constexpr size_t On = 3;
    static constexpr size_t Activities = 4;
    static constexpr size_t Distance = 5;
    static constexpr size_t Waxed = 6;
};

[[nodiscard]] std::vector<TableColumn> gear_columns();
// "Pegasus blue", "Nike Pegasus 41" (brand, model and version),
// "2026-03-14", "", "42", "512 km", "". A part's On is where it's fitted,
// "Canyon", "Race wheels, rear", or "–"; a chain's Waxed is "212 km (20
// Nov)", or "–" if it was never waxed. An unknown name's "Unknown: Old
// Canyon", with only its numbers.
[[nodiscard]] std::vector<TableRow> gear_rows(std::vector<GearTotal> const &totals);
// Retired gear's rows.
[[nodiscard]] std::vector<bool> gear_dimmed(std::vector<GearTotal> const &totals);
// The cells in orange: the distance of gear past its max_km, and the
// distance since its wax of a chain past its wax_km.
[[nodiscard]] std::set<TableCell> gear_highlights(std::vector<GearTotal> const &totals);

// What the screen knows so far.
struct GearState {
    std::optional<GearList>     gear { };          // None without gear.json
    std::filesystem::path       file { };          // gear.json, where it would be
    bool                        reading { false }; // The sidecars' picks
    std::string                 error { };         // Why gear.json can't be read, if it can't
    std::chrono::year_month_day today { };         // For where the parts are, and the last waxes
};

// The line under the heading: what's wrong with gear.json, how to make
// one without it, that the picks are being read, or how many pieces of
// gear there are.
[[nodiscard]] std::string gear_note(GearState const &state);

struct GearScreen : public Layout {
    static constexpr int TableRows = 8;

    GearState                state { };
    ActivitySummaries        summaries { };
    std::optional<GearPicks> picks { }; // None until they're read
    std::shared_ptr<Label>   heading;
    std::shared_ptr<Label>   note;
    std::shared_ptr<Table>   table;

    explicit GearScreen(pWidget const &parent);
    void initialize() override;

    // As the screen opens: the gear read again, and the picks to be read.
    // `error` is why gear.json can't be read now, if it can't: the note
    // says so, in a colour that stands out.
    void open(std::optional<GearList> gear, std::filesystem::path file, std::chrono::year_month_day today, std::string error = { });
    // New summaries, as the job reads them, and the picks, as their job
    // has read them: the rows are worked out again.
    void set_summaries(ActivitySummaries new_summaries);
    void set_picks(GearPicks new_picks);
    // Up and Down through the rows.
    void step_row(int step);

private:
    void refresh();
};

}
