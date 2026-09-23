#include "Date.h"
#include <memory>
#include <storage/Storage.h>
#include <widget/ActivitySelector.h>

namespace ST {
ActivitySelector::ActivitySelector(Submit const &submit, Month const &month, Storage &storage)
    : ListBox(std::format("{} {}", month_name(month.month), month.year))
    , submit_fnc(submit)
    , month(month)
    , storage(storage)
{
    assert(submit_fnc != nullptr);
    populate();
}

void ActivitySelector::populate()
{
    entries.clear();
    matches.clear();
    search = { };
    prompt = std::format("{} {}", month_name(month.month), month.year);
    auto activities = MUST_EVAL(month.list(storage));
    for (auto const &a : activities.activities) {
        entries.emplace_back(std::format("{} {}", a.segment.start_time, a.title), a.id);
    }
}

void ActivitySelector::submit()
{
    submit_fnc(std::dynamic_pointer_cast<ActivitySelector>(self()), entries[selection].payload);
}

bool ActivitySelector::process_key(KeyboardModifier modifier, int key)
{
    if (key == KEY_LEFT) {
        auto m = static_cast<DateTime::Month>(static_cast<int>(month.month) - 1);
        auto y = (m != DateTime::Month::December) ? month.year : month.year - 1;
        month = Month { y, m };
        populate();
        return true;
    }
    if (key == KEY_RIGHT) {
        auto m = static_cast<DateTime::Month>(static_cast<int>(month.month) + 1);
        auto y = (m != DateTime::Month::January) ? month.year : month.year + 1;
        month = Month { y, m };
        populate();
        return true;
    }
    return ListBox::process_key(modifier, key);
}

}
