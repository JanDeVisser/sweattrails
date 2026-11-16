/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Modal.h>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <storage/Summaries.h>
#include <storage/Types.h>

namespace ST {

// Goes up from a month's activities to its year's months, and from those to
// the years. Not Ctrl+Up on macOS, which is Mission Control's.
#ifdef __APPLE__
constexpr KeyCombo GoUpKey { KEY_UP, KModSuper };
#else
constexpr KeyCombo GoUpKey { KEY_UP, KModControl };
#endif

// Not sorted: a month's activities come in the order of their start time.
using ActivityList = ListBox<ActivityID, ListBoxConfig { .search = true, .sort = false }>;

// A row of Ctrl+M's list: the sport's icon, "Tue 14  09:32", the title,
// which a search matches, and the distance and the moving time.
ActivityList::ListBoxEntry activity_entry(Activity const &activity);

// The activities of a month. Left and Right go to the month before or
// after with activities, Shift+Left and Shift+Right a year.
struct ActivitySelector : public ActivityList {
    using Submit = std::function<void(std::shared_ptr<ActivitySelector> const &, ActivityID const &)>;
    using Up = std::function<void(std::shared_ptr<ActivitySelector> const &)>;
    Submit   submit_fnc;
    Up       up_fnc { nullptr }; // GoUpKey, if set
    Month    month;
    Storage &storage;

    ActivitySelector(Submit const &submit, Month const &month, Storage &storage);
    void populate();
    void submit() override;
    bool process_key(KeyboardModifier modifier, int key) override;

    // The month Left, Right, Shift+Left or Shift+Right go to: none at
    // either end, or for another key.
    [[nodiscard]] std::optional<Month> destination(KeyboardModifier modifier, int key) const;
};

// The years and the months aren't searched: there are a dozen at most. Their
// rows are short, so the boxes are too.
using YearList = ListBox<size_t, ListBoxConfig { .search = false, .sort = false, .narrow = true }>;
using MonthList = ListBox<Month, ListBoxConfig { .search = false, .sort = false, .narrow = true }>;

// A row of the year list, "2024", and of the month list, "January", with
// the number of activities as a column.
YearList::ListBoxEntry  year_entry(size_t year, size_t count);
MonthList::ListBoxEntry month_entry(Month const &month, size_t count);

// The top bar's trail to `shown`, naming the year and month as the lists
// do: "Activities", "2025", "November", or just "Activities" without an
// activity on display.
[[nodiscard]] std::vector<std::string> activity_trail(std::optional<Month> const &shown);

// Ctrl+Y's list: the years with activities, oldest first. Enter goes down
// to a year's months.
struct YearSelector : public YearList {
    using Submit = std::function<void(std::shared_ptr<YearSelector> const &, size_t year)>;
    Submit submit_fnc;

    YearSelector(Submit const &submit, MonthCounts const &counts);
    void submit() override;
    bool process_key(KeyboardModifier modifier, int key) override;
};

// The months of a year with activities, in calendar order. Enter goes down
// to a month's activities, GoUpKey up to the years, and Left and Right go
// to the year before or after with activities.
struct MonthSelector : public MonthList {
    using Submit = std::function<void(std::shared_ptr<MonthSelector> const &, Month const &)>;
    using Up = std::function<void(std::shared_ptr<MonthSelector> const &)>;
    Submit               submit_fnc;
    Up                   up_fnc;
    MonthCounts          counts;
    size_t               year;
    std::optional<Month> shown; // The month of the activity on display

    MonthSelector(Submit const &submit, Up const &up, MonthCounts counts, size_t year, std::optional<Month> shown);
    // The rows of `year`, and the month to select, which select() selects
    // after refresh().
    std::optional<Month> populate();
    void                 submit() override;
    bool                 process_key(KeyboardModifier modifier, int key) override;

    // The year Left or Right go to: none at either end, or for another key.
    [[nodiscard]] std::optional<size_t> destination(KeyboardModifier modifier, int key) const;
};

}
