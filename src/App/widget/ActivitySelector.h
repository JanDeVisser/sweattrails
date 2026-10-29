/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <Modal.h>

#include <storage/Activity.h>
#include <storage/Storage.h>
#include <storage/Types.h>

namespace ST {

// Not sorted: a month's activities come in the order of their start time.
using ActivityList = ListBox<ActivityID, ListBoxConfig { .search = true, .sort = false }>;

// A row of Ctrl+M's list: the sport's icon, "Tue 14  09:32", the title,
// which a search matches, and the distance and the moving time.
ActivityList::ListBoxEntry activity_entry(Activity const &activity);

// The activities of a month. Left and Right go to the month before or
// after with activities, Shift+Left and Shift+Right a year.
struct ActivitySelector : public ActivityList {
    using Submit = std::function<void(std::shared_ptr<ActivitySelector> const &, ActivityID const &)>;
    Submit             submit_fnc;
    std::vector<Month> months; // With activities, oldest first
    Month              month;
    Storage           &storage;

    ActivitySelector(Submit const &submit, std::vector<Month> months, Month const &month, Storage &storage);
    void populate();
    void submit() override;
    bool process_key(KeyboardModifier modifier, int key) override;

    // The month Left, Right, Shift+Left or Shift+Right go to: none at
    // either end, or for another key.
    [[nodiscard]] std::optional<Month> destination(KeyboardModifier modifier, int key) const;
};

}
