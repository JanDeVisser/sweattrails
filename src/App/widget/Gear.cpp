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

namespace chrono = std::chrono;

std::vector<TableColumn> gear_columns()
{
    return {
        { .header = "Gear" },
        { .header = "Brand and model" },
        { .header = "In use since" },
        { .header = "On" },
        { .header = "Activities", .right_aligned = true },
        { .header = "Distance", .right_aligned = true },
        { .header = "Waxed", .right_aligned = true },
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

constexpr char const NotThere[] = "–";

std::string km_text(float64 km)
{
    return std::format("{:.0f} km", km);
}

// Where a part is fitted; blank for shoes and bikes.
std::string on_text(GearTotal const &total)
{
    if (!total.gear->part) {
        return { };
    }
    if (!total.fitted) {
        return NotThere;
    }
    return total.fitted->on;
}

// A chain's distance since its wax; blank for anything else.
std::string waxed_text(GearTotal const &total)
{
    if (total.gear->part != PartType::Chain) {
        return { };
    }
    if (!total.wax) {
        return NotThere;
    }
    return std::format("{} ({} {:%b})", km_text(total.wax->km), static_cast<unsigned>(total.wax->day.day()), total.wax->day.month());
}

}

std::vector<TableRow> gear_rows(std::vector<GearTotal> const &totals)
{
    std::vector<TableRow> ret;
    for (auto const &total : totals) {
        auto const activities = std::format("{}", total.activities);
        if (total.unknown()) {
            ret.push_back({ std::format("Unknown: {}", total.name), "", "", "", activities, km_text(total.km), "" });
            continue;
        }
        ret.push_back({
            total.name,
            brand_and_model(*total.gear),
            total.gear->purchased ? std::format("{}", *total.gear->purchased) : std::string { },
            on_text(total),
            activities,
            km_text(total.km),
            waxed_text(total),
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

std::set<TableCell> gear_highlights(std::vector<GearTotal> const &totals)
{
    std::set<TableCell> ret;
    for (auto row = 0uz; row < totals.size(); ++row) {
        if (totals[row].worn()) {
            ret.insert({ row, GearColumn::Distance });
        }
        if (totals[row].needs_wax()) {
            // A chain never waxed has no Waxed to show it on.
            ret.insert({ row, totals[row].wax ? GearColumn::Waxed : GearColumn::Distance });
        }
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
    auto const parts = std::ranges::count(state.gear->gear, GearType::Part, &Gear::type);
    auto       counts = std::format("{} {}, {} {}", shoes, shoes == 1 ? "pair of shoes" : "pairs of shoes", bikes, bikes == 1 ? "bike" : "bikes");
    if (parts > 0) {
        counts += std::format(", {} {}", parts, parts == 1 ? "part" : "parts");
    }
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

void GearScreen::open(std::optional<GearList> gear, std::filesystem::path file, chrono::year_month_day today, std::string error)
{
    state = GearState { .gear = std::move(gear), .file = std::move(file), .reading = true, .error = std::move(error), .today = today };
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
    auto const totals = gear_totals(state.gear.value_or(GearList { }), summaries, picks.value_or(GearPicks { }), state.today);
    auto       rows = gear_rows(totals);
    // Until the picks are read, the numbers would be the defaults' alone:
    // the gear is listed without them.
    auto highlights = gear_highlights(totals);
    if (!picks) {
        for (auto &row : rows) {
            for (auto const column : { GearColumn::Activities, GearColumn::Distance, GearColumn::Waxed }) {
                row[column].clear();
            }
        }
        highlights.clear();
    }
    auto const selection = table->selection;
    auto const top_line = table->scroll.top_line;
    table->set_rows(gear_columns(), std::move(rows), gear_dimmed(totals), std::move(highlights));
    table->scroll.top_line = top_line;
    table->scroll.clamp();
    table->select(selection);
}

}
