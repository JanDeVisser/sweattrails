#include <format>
#include <memory>

#include <config.h>

#include <Date.h>

#include <storage/Storage.h>
#include <widget/ActivitySelector.h>
#include <widget/DataFields.h>
#include <widget/SportIcon.h>

namespace ST {

ActivityList::ListBoxEntry activity_entry(Activity const &activity)
{
    ActivityList::ListBoxEntry ret { activity.title, activity.id };
    auto const                &start = activity.segment.start_time;
    ret.icon = std::format(SWEATTRAILS_DATADIR "/icons/{}", sport_icon_file(activity.sport, activity.sub_sport));
    ret.prefix = std::format("{} {}  {:02}:{:02}", weekday_name(start.day_of_week).substr(0, 3), start.day_of_month, start.hour, start.minute);
    auto const &segment = activity.segment;
    ret.columns = { (segment.distance > 0) ? format_distance(segment.distance) : std::string { }, format_moving_time(segment.moving) };
    return ret;
}

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
    scroll.top_line = 0;
    prompt = std::format("{} {}", month_name(month.month), month.year);
    auto activities = MUST_EVAL(month.list(storage));
    for (auto const &a : activities.activities) {
        entries.push_back(activity_entry(a));
    }
}

void ActivitySelector::submit()
{
    submit_fnc(std::dynamic_pointer_cast<ActivitySelector>(self()), selected().payload);
}

// Only to months that list activities: one with only a broken FIT file
// would show as an empty list. A month that can't be read is nowhere to go.
std::optional<Month> ActivitySelector::destination(KeyboardModifier modifier, int key) const
{
    if ((key != KEY_LEFT && key != KEY_RIGHT) || (modifier != KModNone && modifier != KModShift)) {
        return { };
    }
    auto const step = (key == KEY_RIGHT) ? 1 : -1;
    auto const listed = (modifier == KModNone)
        ? storage.listed_month(month, step, false)
        : storage.listed_month(month.plus(12 * step), step, true);
    if (!listed || !*listed) {
        return { };
    }
    return (*listed)->id;
}

// refresh() fits the box to the new month's prompt, and starts at the top.
// At either end, the key does nothing.
bool ActivitySelector::process_key(KeyboardModifier modifier, int key)
{
    if (status == ModalStatus::Active && (key == KEY_LEFT || key == KEY_RIGHT) && (modifier == KModNone || modifier == KModShift)) {
        if (auto const to = destination(modifier, key); to) {
            month = *to;
            populate();
            refresh();
        }
        return true;
    }
    return ListBox::process_key(modifier, key);
}

}
