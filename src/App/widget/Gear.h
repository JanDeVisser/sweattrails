/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The Gear screen (specs/67-gear.md): a row per piece of gear in
 * gear.json, shoes then bikes, with the activities it was used for and
 * the distance on it, and a row per name picked in sidecars that isn't in
 * the file.
 */

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Table.h>
#include <Widget.h>

#include <storage/Gear.h>
#include <storage/Summaries.h>

namespace ST {

[[nodiscard]] std::vector<TableColumn> gear_columns();
// "Pegasus blue", "Nike Pegasus 41" (brand, model and version), "2026-03-14", "42", "512 km"; an
// unknown name's "Unknown: Old Canyon", with only its numbers.
[[nodiscard]] std::vector<TableRow> gear_rows(std::vector<GearTotal> const &totals);
// Retired gear's rows.
[[nodiscard]] std::vector<bool> gear_dimmed(std::vector<GearTotal> const &totals);

// What the screen knows so far.
struct GearState {
    std::optional<GearList> gear { };          // None without gear.json
    std::filesystem::path   file { };          // gear.json, where it would be
    bool                    reading { false }; // The sidecars' picks
};

// The line under the heading: how to make gear.json without one, that the
// picks are being read, or how many pieces of gear there are.
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
    void open(std::optional<GearList> gear, std::filesystem::path file);
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
