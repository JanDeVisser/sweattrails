#include <format>
#include <map>
#include <memory>
#include <ranges>

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
    // The day padded, so that the times line up: "Sat  4  10:00".
    ret.prefix = std::format("{} {:>2}  {:02}:{:02}", weekday_name(start.day_of_week).substr(0, 3), start.day_of_month, start.hour, start.minute);
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
    if (status == ModalStatus::Active && KeyCombo { key, modifier } == GoUpKey) {
        if (up_fnc) {
            up_fnc(std::dynamic_pointer_cast<ActivitySelector>(self()));
        }
        return true;
    }
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

YearList::ListBoxEntry year_entry(size_t year, size_t count)
{
    YearList::ListBoxEntry ret { std::format("{}", year), year };
    ret.columns = { std::format("{}", count) };
    return ret;
}

MonthList::ListBoxEntry month_entry(Month const &month, size_t count)
{
    MonthList::ListBoxEntry ret { month_name(month.month), month };
    ret.columns = { std::format("{}", count) };
    return ret;
}

std::vector<std::string> activity_trail(std::optional<Month> const &shown)
{
    if (!shown) {
        return { "Activities" };
    }
    return { "Activities", std::format("{}", shown->year), std::string { month_name(shown->month) } };
}

YearSelector::YearSelector(Submit const &submit, MonthCounts const &counts)
    : ListBox("Years")
    , submit_fnc(submit)
{
    assert(submit_fnc != nullptr);
    std::map<size_t, size_t> years;
    for (auto const &[month, count] : counts) {
        years[month.year] += count;
    }
    for (auto const &[year, count] : years) {
        entries.push_back(year_entry(year, count));
    }
}

void YearSelector::submit()
{
    submit_fnc(std::dynamic_pointer_cast<YearSelector>(self()), selected().payload);
}

// Up from the top goes nowhere, but isn't the app's key either.
bool YearSelector::process_key(KeyboardModifier modifier, int key)
{
    if (status == ModalStatus::Active && KeyCombo { key, modifier } == GoUpKey) {
        return true;
    }
    return ListBox::process_key(modifier, key);
}

MonthSelector::MonthSelector(Submit const &submit, Up const &up, MonthCounts counts, size_t year, std::optional<Month> shown)
    : ListBox(std::format("{}", year))
    , submit_fnc(submit)
    , up_fnc(up)
    , counts(std::move(counts))
    , year(year)
    , shown(shown)
{
    assert(submit_fnc != nullptr && up_fnc != nullptr);
    populate();
}

std::optional<Month> MonthSelector::populate()
{
    entries.clear();
    matches.clear();
    selection = 0;
    scroll.top_line = 0;
    prompt = std::format("{}", year);
    auto const first = counts.lower_bound(Month { year, DateTime::Month::January });
    auto const last = counts.lower_bound(Month { year + 1, DateTime::Month::January });
    for (auto const &[month, count] : std::ranges::subrange(first, last)) {
        entries.push_back(month_entry(month, count));
    }
    return month_to_select(counts, year, shown);
}

void MonthSelector::submit()
{
    submit_fnc(std::dynamic_pointer_cast<MonthSelector>(self()), selected().payload);
}

std::optional<size_t> MonthSelector::destination(KeyboardModifier modifier, int key) const
{
    if ((key != KEY_LEFT && key != KEY_RIGHT) || modifier != KModNone) {
        return { };
    }
    return year_next_to(counts, year, (key == KEY_RIGHT) ? 1 : -1);
}

// refresh() fits the box to the new year's prompt, and starts at the top,
// so the month is selected after it. At either end, the key does nothing.
bool MonthSelector::process_key(KeyboardModifier modifier, int key)
{
    if (status != ModalStatus::Active) {
        return ListBox::process_key(modifier, key);
    }
    if (KeyCombo { key, modifier } == GoUpKey) {
        up_fnc(std::dynamic_pointer_cast<MonthSelector>(self()));
        return true;
    }
    if ((key == KEY_LEFT || key == KEY_RIGHT) && modifier == KModNone) {
        if (auto const to = destination(modifier, key); to) {
            year = *to;
            auto const month = populate();
            refresh();
            if (month) {
                select(*month);
            }
        }
        return true;
    }
    return ListBox::process_key(modifier, key);
}

}
