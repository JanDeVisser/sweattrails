#include <memory>

#include <Date.h>

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
    selection = 0;
    top_line = 0;
    prompt = std::format("{} {}", month_name(month.month), month.year);
    auto activities = MUST_EVAL(month.list(storage));
    for (auto const &a : activities.activities) {
        entries.emplace_back(std::format("{} {}", a.segment.start_time, a.title), a.id);
    }
}

void ActivitySelector::submit()
{
    submit_fnc(std::dynamic_pointer_cast<ActivitySelector>(self()), selected().payload);
}

bool ActivitySelector::process_key(KeyboardModifier modifier, int key)
{
    // Left and Right go to the previous and next month. refresh() sorts the
    // new month's entries, fits the box to its prompt, and starts at the top.
    if (status == ModalStatus::Active && modifier == KModNone) {
        if (key == KEY_LEFT) {
            auto m = static_cast<DateTime::Month>(static_cast<int>(month.month) - 1);
            auto y = (m != DateTime::Month::December) ? month.year : month.year - 1;
            month = Month { y, m };
            populate();
            refresh();
            return true;
        }
        if (key == KEY_RIGHT) {
            auto m = static_cast<DateTime::Month>(static_cast<int>(month.month) + 1);
            auto y = (m != DateTime::Month::January) ? month.year : month.year + 1;
            month = Month { y, m };
            populate();
            refresh();
            return true;
        }
    }
    return ListBox::process_key(modifier, key);
}

}
