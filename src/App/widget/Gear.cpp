/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <format>

#include <StringUtil.h>

#include <widget/Gear.h>

namespace ST {

std::vector<TableColumn> gear_columns()
{
    return {
        { .header = "Gear" },
        { .header = "Brand and model" },
        { .header = "In use since" },
        { .header = "Activities", .right_aligned = true },
        { .header = "Distance", .right_aligned = true },
    };
}

namespace {

std::string brand_and_model(Gear const &gear)
{
    std::vector<std::string> parts;
    for (auto const *part : { &gear.brand, &gear.model, &gear.version }) {
        if (!part->empty()) {
            parts.push_back(*part);
        }
    }
    return join(parts, " ");
}

}

std::vector<TableRow> gear_rows(std::vector<GearTotal> const &totals)
{
    std::vector<TableRow> ret;
    for (auto const &total : totals) {
        auto const numbers = TableRow {
            std::format("{}", total.activities),
            std::format("{:.0f} km", total.km),
        };
        if (total.unknown()) {
            ret.push_back({ std::format("Unknown: {}", total.name), "", "", numbers[0], numbers[1] });
            continue;
        }
        ret.push_back({
            total.name,
            brand_and_model(*total.gear),
            total.gear->purchased ? std::format("{}", *total.gear->purchased) : std::string { },
            numbers[0],
            numbers[1],
        });
    }
    return ret;
}

std::vector<bool> gear_dimmed(std::vector<GearTotal> const &totals)
{
    std::vector<bool> ret;
    for (auto const &total : totals) {
        ret.push_back(total.gear && total.gear->retired.has_value());
    }
    return ret;
}

std::string gear_note(GearState const &state)
{
    if (!state.error.empty()) {
        auto const what = state.gear ? "Showing the gear read before it broke" : "No gear until it's fixed";
        return std::format("{}. {} · Esc goes back", state.error, what);
    }
    if (!state.gear) {
        return std::format("No gear yet: write {}, as the README says · Esc goes back", state.file.string());
    }
    auto const shoes = std::ranges::count(state.gear->gear, GearType::Shoes, &Gear::type);
    auto const bikes = std::ranges::count(state.gear->gear, GearType::Bike, &Gear::type);
    auto       counts = std::format("{} {}, {} {}", shoes, shoes == 1 ? "pair of shoes" : "pairs of shoes", bikes, bikes == 1 ? "bike" : "bikes");
    if (state.reading) {
        counts += " · Reading which gear the activities used…";
    }
    return counts + " · Esc goes back";
}

GearScreen::GearScreen(pWidget const &parent)
    : Layout(parent)
{
}

void GearScreen::initialize()
{
    heading = add_widget<Label>("Gear");
    heading->size = FontSize::Large;
    note = add_widget<Label>("", LIGHTGRAY);
    note->size = FontSize::Small;
    table = add_widget<Table>(TableRows);
    table->policy = SizePolicy::Stretch;
    table->on_click = [this](size_t row) {
        table->select(row);
    };
}

void GearScreen::open(std::optional<GearList> gear, std::filesystem::path file, std::string error)
{
    state = GearState { .gear = std::move(gear), .file = std::move(file), .reading = true, .error = std::move(error) };
    picks.reset();
    refresh();
}

void GearScreen::set_summaries(ActivitySummaries new_summaries)
{
    summaries = std::move(new_summaries);
    refresh();
}

void GearScreen::set_picks(GearPicks new_picks)
{
    picks = std::move(new_picks);
    state.reading = false;
    refresh();
}

void GearScreen::step_row(int step)
{
    if (table->rows.empty()) {
        return;
    }
    auto const last = static_cast<int>(table->rows.size()) - 1;
    auto const from = table->selection ? static_cast<int>(*table->selection) : (step > 0 ? -1 : last + 1);
    table->select(static_cast<size_t>(std::clamp(from + step, 0, last)));
}

void GearScreen::refresh()
{
    note->text = gear_note(state);
    note->color = state.error.empty() ? LIGHTGRAY : ORANGE;
    auto const totals = gear_totals(state.gear.value_or(GearList { }), summaries, picks.value_or(GearPicks { }));
    auto       rows = gear_rows(totals);
    // Until the picks are read, the numbers would be the defaults' alone:
    // the gear is listed without them.
    if (!picks) {
        for (auto &row : rows) {
            row[3].clear();
            row[4].clear();
        }
    }
    auto const selection = table->selection;
    auto const top_line = table->scroll.top_line;
    table->set_rows(gear_columns(), std::move(rows), gear_dimmed(totals));
    table->scroll.top_line = top_line;
    table->scroll.clamp();
    table->select(selection);
}

}
