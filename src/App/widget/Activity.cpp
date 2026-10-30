#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <ranges>

#include <raylib.h>

#include <Format.h>
#include <StringUtil.h>

#include <Widget.h>

#include <SweatTrails.h>
#include <storage/Activity.h>
#include <widget/Activity.h>
#include <widget/SportIcon.h>

namespace ST {

struct Text {
    std::string text;
    Color       color;
};

ActivityDataDisplay::ActivityDataDisplay(pWidget const &parent)
    : Widget(parent, SizePolicy::Stretch, 0.0)
{
}

ActivityDataDisplay::~ActivityDataDisplay()
{
    UnloadTexture(sport_icon);
}

void ActivityDataDisplay::initialize()
{
}

namespace {

std::string label_text(DataField const &field)
{
    return field.label + ":";
}

}

void ActivityDataDisplay::draw()
{
    if (activity == nullptr) {
        return;
    }
    auto const top = 20.0f;
    render_texture(10, top, sport_icon);
    auto sz = render_text(
        116, top,
        TextFormat("%s - %s", FIT::tag(activity->activity.sport), activity->activity.segment.start_time.format().c_str()),
        FontSize::Small,
        RAYWHITE);

    // The title, the notes under it in the small font, and the summary
    // under those in the title's font.
    auto const line_height = 6.0f * App::measure_text("Mg", FontSize::Large).y / 5.0f;
    auto const title_y = top + sz.y + 20;
    auto const title_size = render_text(116, title_y, activity->activity.title, FontSize::Large, RAYWHITE);
    title_area = Rectangle { .x = viewport.x + 116, .y = viewport.y + title_y, .width = title_size.x, .height = title_size.y };
    auto const summary_y = draw_notes(title_y + line_height);
    auto const summary = activity->lap ? lap_summary(activity->activity, *activity->lap) : activity_summary(activity->activity);
    render_text(116, summary_y, summary, FontSize::Large, RAYWHITE);

    // The fields start below the icon, which is 96 high, or below the
    // summary, whichever is lower.
    auto const icon_bottom = top + 96;
    draw_fields(std::max(icon_bottom, summary_y + line_height) + 10);
}

// Draws the activity's notes at y, wrapped, NotesLines lines at most, and
// returns where what's below them goes.
float ActivityDataDisplay::draw_notes(float y)
{
    auto const width = viewport.width - 116 - 10;
    auto const measure = [](std::string_view text) {
        return App::measure_text(std::string { text }.c_str(), FontSize::Small).x;
    };
    auto const lines = clamp_lines(wrap_words(activity->activity.notes, width, measure), NotesLines, width, measure);
    if (lines.empty()) {
        return y;
    }
    auto const line_height = 6.0f * App::measure_text("Mg", FontSize::Small).y / 5.0f;
    for (auto const &line : lines) {
        render_text(116, y, line, FontSize::Small, RAYWHITE);
        y += line_height;
    }
    return y + line_height / 2;
}

// Draws the fields, the selected lap's or the activity's, in two columns.
// In each column the values start at the same x, past the column's widest
// label, so that they line up in any font.
void ActivityDataDisplay::draw_fields(float y)
{
    auto const &a = activity->activity;
    auto const  lines = activity->lap
        ? data_fields(a, a.laps[*activity->lap].segment, a.laps[*activity->lap].first_record)
        : data_fields(a);
    auto const  gap = App::the()->char_size(FontSize::Medium).x;
    auto const  line_height = 6.0f * App::measure_text("Mg", FontSize::Medium).y / 5.0f;
    auto const  label_width = [](DataField const &field) {
        return App::measure_text(label_text(field).c_str(), FontSize::Medium).x;
    };

    auto const left = 10.0f;
    auto const right = viewport.width / 2 + 10.0f;
    auto       left_values = left;
    auto       right_values = right;
    for (auto const &line : lines) {
        if (line.left) {
            left_values = std::max(left_values, left + label_width(*line.left) + gap);
        }
        if (line.right) {
            right_values = std::max(right_values, right + label_width(*line.right) + gap);
        }
    }

    clickable.clear();
    for (auto const &line : lines) {
        if (line.left) {
            draw_field(*line.left, left, left_values, y, line_height);
        }
        if (line.right) {
            draw_field(*line.right, right, right_values, y, line_height);
        }
        y += line_height;
    }
}

// Draws the field's label at x and its value at value_x. Remembers where a
// field that marks a record is, for process_input().
void ActivityDataDisplay::draw_field(DataField const &field, float x, float value_x, float y, float height)
{
    render_text(x, y, label_text(field), FontSize::Medium, RAYWHITE);
    auto const value_size = render_text(value_x, y, field.value, FontSize::Medium, RAYWHITE);
    if (field.mark_at) {
        clickable.push_back(ClickableField {
            .area = Rectangle {
                .x = viewport.x + x,
                .y = viewport.y + y,
                .width = value_x + value_size.x - x,
                .height = height,
            },
            .mark_at = *field.mark_at,
        });
    }
}

void ActivityDataDisplay::resize()
{
}

// Clicking the title renames the activity, as F2 does. Clicking a maximum
// marks the record where it happened, as hovering over the graph does.
void ActivityDataDisplay::process_input()
{
    if (activity == nullptr) {
        return;
    }
    auto const mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, title_area)) {
        App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            SweatTrails::the()->bus.submit("st-rename-activity", JSONValue { });
        }
        return;
    }
    for (auto const &[area, mark_at] : clickable) {
        if (CheckCollisionPointRec(mouse, area)) {
            App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                activity->set_mark(mark_at);
            }
            return;
        }
    }
}

void ActivityDataDisplay::set_activity(std::shared_ptr<DisplayedActivity> const &activity)
{
    clickable.clear();
    if (this->activity) {
        UnloadTexture(sport_icon);
    }
    this->activity = activity;
    if (this->activity) {
        auto const &a = activity->activity;
        auto const  icon = std::format(SWEATTRAILS_DATADIR "/icons/{}", sport_icon_file(a.sport, a.sub_sport));
        sport_icon = LoadTexture(icon.c_str());
    }
}

ActivityGraph::ActivityGraph(pWidget const &parent)
    : Widget(parent, SizePolicy::Absolute, 200.0)
{
}

ActivityGraph::~ActivityGraph()
{
    UnloadTexture(texture);
    UnloadImage(image);
}

void ActivityGraph::initialize()
{
}

namespace {

// The strip's laps alternate between these, so that each boundary shows.
constexpr Color LapShades[] = { { 70, 70, 70, 255 }, { 110, 110, 110, 255 } };
constexpr Color SelectedLapShade = RAYWHITE;
constexpr Color SelectedRangeShade = { 255, 255, 255, 40 };

}

float ActivityGraph::strip_height() const
{
    if (!activity || !activity->has_laps()) {
        return 0.0f;
    }
    return std::ceilf(0.6f * App::the()->char_size().y);
}

Rectangle ActivityGraph::strip_area() const
{
    auto const height = strip_height();
    return Rectangle {
        .x = viewport.x,
        .y = viewport.y + viewport.height - height,
        .width = viewport.width - 20,
        .height = height,
    };
}

// The laps, each as wide as its part of the graph above it.
void ActivityGraph::draw_strip() const
{
    auto const area = strip_area();
    if (area.height <= 0) {
        return;
    }
    auto const y = area.y - viewport.y;
    auto const spans = lap_spans(activity->activity, area.width);
    for (auto ix = 0uz; ix < spans.size(); ++ix) {
        auto const color = (activity->lap == ix) ? SelectedLapShade : LapShades[ix % 2];
        draw_rectangle_no_normalize(spans[ix].x0, y, spans[ix].x1 - spans[ix].x0, area.height, color);
    }
}

// Shades the selected range, a lap's or one dragged in the graph.
void ActivityGraph::draw_selected_range() const
{
    if (!activity->segment || activity->activity.records.empty()) {
        return;
    }
    auto const dt = (viewport.width - 20) / static_cast<float>(activity->activity.records.size());
    auto const x0 = static_cast<float>(activity->segment->min) * dt;
    auto const x1 = static_cast<float>(activity->segment->max + 1) * dt;
    draw_rectangle_no_normalize(x0, 50, x1 - x0, viewport.height - 50 - strip_height(), SelectedRangeShade);
}

void ActivityGraph::draw()
{
    if (!activity) {
        return;
    }
    render_texture(0, 50, texture);
    draw_selected_range();
    draw_strip();
    if (activity->mark) {
        auto const  width = this->viewport.width - 20;
        float       dt = static_cast<float>(width) / static_cast<float>(activity->activity.records.size());
        float       x = *activity->mark * dt;
        auto const &record { activity->activity.records[*activity->mark] };
        draw_line(x, 50, x, 50 + texture.height, BLACK);
        std::vector<Text> banner;
        size_t            sz = 0;
        if (record.altitude) {
            banner.push_back({ std::format("Alt {} m", std::truncf(*record.altitude)), LIGHTGRAY });
            sz += App::measure_text(banner.back().text.c_str(), FontSize::Small).x;
        }
        if (record.power) {
            banner.push_back({ std::format("Pwr {} W", *record.power), DARKBLUE });
            sz += App::measure_text(banner.back().text.c_str(), FontSize::Small).x;
        }
        if (record.speed) {
            banner.push_back({ std::format("Spd {:.1f} km/h", *record.speed * 3.6), DARKGREEN });
            sz += App::measure_text(banner.back().text.c_str(), FontSize::Small).x;
        }
        if (record.heart_rate) {
            banner.push_back({ std::format("HR {} bpm", *record.heart_rate), RED });
            sz += App::measure_text(banner.back().text.c_str(), FontSize::Small).x;
        }
        if (!banner.empty()) {
            draw_line(x, 30, x, 50, RAYWHITE);
            sz += (banner.size() - 1) * 10.0;
            auto render_banner = [this, &banner](float x) {
                for (auto const &text : banner) {
                    x += render_text(x, 20, text.text.c_str(), FontSize::Small, text.color).x + 10;
                }
            };

            if (x + sz <= width) {
                draw_line(x, 30, x + 20, 30, RAYWHITE);
                draw_circle(x + 20, 30, 5, RAYWHITE);
                render_banner(x + 30);
            } else {
                draw_line(x, 30, x - 20, 30, RAYWHITE);
                draw_circle(x - 20, 30, 5, RAYWHITE);
                render_banner(x - sz - 30);
            }
        }
    }
}

void ActivityGraph::resize()
{
}

void ActivityGraph::process_input()
{
    // There's no activity until the first one is loaded, which can take a
    // while if the inbox is processed first.
    if (!activity || activity->activity.records.empty()) {
        return;
    }
    // The record under the mouse's x, also when it's dragged past the
    // graph's ends.
    auto const mouse = GetMousePosition();
    auto const record_at = [this, &mouse]() {
        auto const num_records = activity->activity.records.size();
        auto const dt = (viewport.width - 20) / static_cast<float>(num_records);
        auto const x = std::max(0.0f, mouse.x - viewport.x);
        return std::min(static_cast<size_t>(std::truncf(x / dt)), num_records - 1);
    };

    // A drag goes on until the button is let go, wherever the mouse goes,
    // the strip included.
    if (dragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            activity->set_segment(record_at());
        } else {
            dragging = false;
            activity->end_segment();
        }
        return;
    }

    // A click in the strip selects the lap, as in the table.
    auto const strip = strip_area();
    if (strip.height > 0 && CheckCollisionPointRec(mouse, strip)) {
        auto const lap = lap_at(lap_spans(activity->activity, strip.width), mouse.x - strip.x);
        if (lap) {
            App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                activity->toggle_lap(*lap);
            }
        }
        return;
    }

    // A drag starts with a press in the graph, and ends a lap's selection.
    if (!mouse_in()) {
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        activity->clear_mark();
        dragging = true;
        activity->start_segment(record_at());
    } else if (IsMouseButtonUp(MOUSE_BUTTON_LEFT)) {
        activity->set_mark(record_at());
    }
}

void ActivityGraph::set_activity(std::shared_ptr<DisplayedActivity> const &activity)
{
    // Reset them after unloading: an activity without data to graph doesn't
    // make new ones, and draw() mustn't draw the old texture, nor the next
    // activity unload the old ones again. Unloading empty ones does nothing,
    // and an empty texture isn't drawn.
    UnloadTexture(texture);
    UnloadImage(image);
    texture = { };
    image = { };
    this->activity = activity;
    if (!this->activity) {
        return;
    }

    auto const          &seg = activity->activity.segment;
    auto const           width = this->viewport.width - 20;
    auto const           height = this->viewport.height - 50 - strip_height();
    float                prev_x = 0.0;
    float                prev_speed = 0.0;
    float                prev_power = 0.0;
    float                prev_hr = 0.0;
    float                dt = static_cast<float>(width) / static_cast<float>(activity->activity.records.size());
    std::optional<float> dalt_maybe { };
    if (seg.computed_elevation_range) {
        dalt_maybe = static_cast<float>(height) / static_cast<float>(*seg.computed_elevation_range.diff());
    }
    std::optional<float> dspeed_maybe { };
    if (seg.computed_speed_range.range) {
        dspeed_maybe = static_cast<float>(height) / static_cast<float>(*seg.computed_speed_range.diff());
    }
    std::optional<float> dpower_maybe { };
    if (seg.computed_power_range.range) {
        dpower_maybe = static_cast<float>(height) / static_cast<float>(*seg.computed_power_range.diff());
    }
    std::optional<float> dhr_maybe { };
    if (seg.computed_hr_range.range) {
        dhr_maybe = static_cast<float>(height) / static_cast<float>(*seg.computed_hr_range.diff());
    }
    if (!dalt_maybe && !dspeed_maybe && !dpower_maybe && !dhr_maybe) {
        return;
    }

    image = GenImageColor(width, height, BLANK);
    size_t ix = 0;
    for (auto const &record : activity->activity.records) {
        float x = dt * ix;
        if (x - prev_x > 1.0) {
            if (dalt_maybe && record.altitude) {
                float alt_y = height - (*record.altitude - seg.computed_elevation_range.min()) * *dalt_maybe;
                ImageDrawRectangleRec(
                    &image,
                    Rectangle { .x = prev_x, .y = alt_y, .width = std::ceilf(x - prev_x), .height = height - alt_y },
                    LIGHTGRAY);
            }
            if (dspeed_maybe && record.speed) {
                float speed_y = height - *record.speed * *dspeed_maybe;
                ImageDrawLineV(
                    &image,
                    Vector2 { .x = prev_x, .y = std::ceilf(prev_speed) },
                    Vector2 { .x = x, .y = std::ceilf(speed_y) },
                    DARKGREEN);
                prev_speed = speed_y;
            }
            if (dhr_maybe && record.heart_rate) {
                float hr_y = height - (*record.heart_rate - seg.computed_hr_range.range->min) * *dhr_maybe;
                ImageDrawLineV(
                    &image,
                    Vector2 { .x = prev_x, .y = std::ceilf(prev_hr) },
                    Vector2 { .x = x, .y = std::ceilf(hr_y) },
                    RED);
                prev_hr = hr_y;
            }
            if (dpower_maybe && record.power) {
                float power_y = height - *record.power * *dpower_maybe;
                ImageDrawLineV(
                    &image,
                    Vector2 { .x = prev_x, .y = std::ceilf(prev_power) },
                    Vector2 { .x = x, .y = std::ceilf(power_y) },
                    DARKBLUE);
                prev_power = power_y;
            }
            prev_x = x;
        }
        ix += 1;
    }
    texture = LoadTextureFromImage(image);
}

ActivityDisplay::ActivityDisplay(pWidget const &parent)
    : Layout(parent, ContainerOrientation::Vertical)
{
}

// The table only shows for more than one lap: one lap is the whole
// activity, which the data panel shows. The map fits the route to its new
// size when the layout resizes it.
void ActivityDisplay::set_activity(Activity const &activity)
{
    this->activity = std::make_shared<DisplayedActivity>(activity);
    if (this->activity->has_laps()) {
        auto [columns, rows] = lap_table(this->activity->activity);
        laps->set_rows(std::move(columns), std::move(rows));
        laps->policy = SizePolicy::Characters;
    } else {
        laps->set_rows({ }, { });
        laps->policy = SizePolicy::Hide;
    }
    data_display->set_activity(this->activity);
    map->set_activity(this->activity);
    preview->set_activity(this->activity);
    graph->set_activity(this->activity);
    resize();
}

void ActivityDisplay::clear_activity()
{
    this->activity = nullptr;
    laps->set_rows({ }, { });
    laps->policy = SizePolicy::Hide;
    data_display->set_activity(nullptr);
    map->set_activity(nullptr);
    preview->set_activity(nullptr);
    graph->set_activity(nullptr);
    resize();
}

bool ActivityDisplay::previewing() const
{
    return preview->policy != SizePolicy::Hide;
}

// The preview takes the place of the data panel and the map, which keeps
// its own policy, Hide for an activity without a route.
void ActivityDisplay::show_preview(bool on)
{
    central->policy = on ? SizePolicy::Hide : SizePolicy::Stretch;
    preview->policy = on ? SizePolicy::Stretch : SizePolicy::Hide;
    resize();
}

// The table's selection follows the activity's lap, however it was
// selected: in the table, in the strip, with the keys, or ended by a drag.
// It only scrolls when the lap changes, so that the wheel can scroll away
// from the selection.
void ActivityDisplay::on_draw()
{
    if (activity && laps->selection != activity->lap) {
        laps->select(activity->lap);
    }
}

void ActivityDisplay::initialize()
{
    central = add_widget<Layout>(ContainerOrientation::Horizontal, SizePolicy::Stretch);
    data_display = central->add_widget<ActivityDataDisplay>();
    map = central->add_widget<ActivityMap>();
    preview = add_widget<OverlayPreview>();
    laps = add_widget<Table>(LapTableRows);
    laps->policy = SizePolicy::Hide;
    laps->on_click = [this](size_t row) {
        if (activity) {
            activity->toggle_lap(row);
        }
    };
    graph = add_widget<ActivityGraph>();
}

}
