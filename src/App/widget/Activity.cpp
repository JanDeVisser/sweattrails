#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <ranges>

#include <raylib.h>

#include <Format.h>
#include <StringUtil.h>

#include <Chart.h>
#include <Widget.h>

#include <SweatTrails.h>
#include <storage/Activity.h>
#include <widget/Activity.h>
#include <widget/GraphData.h>
#include <widget/SportIcon.h>

namespace ST {

ActivityDataDisplay::ActivityDataDisplay(pWidget const &parent)
    : Widget(parent, SizePolicy::Stretch, 0.0)
{
}

ActivityDataDisplay::~ActivityDataDisplay()
{
    UnloadTexture(sport_icon);
    UnloadTexture(pen_icon);
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

// The title, the notes under it in the small font, and the summary under
// those in the title's font. The fields start below the sport icon, which
// is 96 high, or below the summary, whichever is lower.
ActivityDataDisplay::Rows ActivityDataDisplay::rows(float width) const
{
    auto const top = 20.0f;
    auto const line_height = [](FontSize size) {
        return 6.0f * App::measure_text("Mg", size).y / 5.0f;
    };
    auto const notes_width = width - 116 - 10;
    auto const measure = [](std::string_view text) {
        return App::measure_text(std::string { text }.c_str(), FontSize::Small).x;
    };
    Rows ret { .title_y = top + App::measure_text("Mg", FontSize::Small).y + 20 };
    ret.notes_y = ret.title_y + line_height(FontSize::Large);
    ret.notes = clamp_lines(wrap_words(activity->activity.notes, notes_width, measure), NotesLines, notes_width, measure);
    ret.summary_y = ret.notes_y;
    if (!ret.notes.empty()) {
        ret.summary_y += (static_cast<float>(ret.notes.size()) + 0.5f) * line_height(FontSize::Small);
    }
    auto const icon_bottom = top + 96;
    ret.fields_y = std::max(icon_bottom, ret.summary_y + line_height(FontSize::Large)) + 10;
    return ret;
}

float ActivityDataDisplay::needed_height(float width) const
{
    if (activity == nullptr) {
        return 0;
    }
    std::vector<std::optional<Comparison>> laps_compared;
    std::optional<Comparison>              whole;
    if (activity->compared) {
        whole = activity->whole_comparison();
        for (auto ix = 0uz; ix < activity->activity.laps.size(); ++ix) {
            laps_compared.push_back(activity->lap_comparison(ix));
        }
    }
    auto const lines = static_cast<float>(most_data_lines(activity->activity, whole, laps_compared));
    auto const line_height = 6.0f * App::measure_text("Mg", FontSize::Medium).y / 5.0f;
    return rows(width).fields_y + lines * line_height + 10;
}

void ActivityDataDisplay::draw()
{
    if (activity == nullptr) {
        return;
    }
    auto const top = 20.0f;
    render_texture(10, top, sport_icon);
    render_text(
        116, top,
        TextFormat("%s - %s", FIT::tag(activity->activity.sport), activity->activity.segment.start_time.format().c_str()),
        FontSize::Small,
        RAYWHITE);

    auto const at = rows(viewport.width);
    auto const title_size = render_text(116, at.title_y, activity->activity.title, FontSize::Large, RAYWHITE);
    title_area = Rectangle { .x = viewport.x + 116, .y = viewport.y + at.title_y, .width = title_size.x, .height = title_size.y };
    draw_pen(title_area);
    draw_notes(at.notes, at.notes_y);
    auto const summary = [this] {
        auto const &a = activity->activity;
        if (activity->lap) {
            return lap_summary(a, *activity->lap);
        }
        if (activity->range_stats && activity->segment) {
            return range_summary(a, activity->segment->min, activity->segment->max, *activity->range_stats);
        }
        return activity_summary(a);
    }() + (activity->compared ? compared_suffix(device_name(activity->compared->activity)) : std::string { });
    render_text(116, at.summary_y, summary, FontSize::Large, RAYWHITE);
    draw_fields(at.fields_y);
}

// The pen after the title, as high as it, says the title can be changed:
// grey, so as not to be loud, and white while the mouse is over it or the
// title.
void ActivityDataDisplay::draw_pen(Rectangle const &title)
{
    if (pen_icon.id == 0) {
        pen_icon = LoadTexture(SWEATTRAILS_DATADIR "/icons/pen-48.png");
        SetTextureFilter(pen_icon, TEXTURE_FILTER_BILINEAR);
    }
    pen_area = Rectangle { .x = title.x + title.width + 10, .y = title.y, .width = title.height, .height = title.height };
    Rectangle const source { .x = 0, .y = 0, .width = static_cast<float>(pen_icon.width), .height = static_cast<float>(pen_icon.height) };
    DrawTexturePro(pen_icon, source, pen_area, Vector2 { }, 0, renaming_hovered ? RAYWHITE : GRAY);
}

void ActivityDataDisplay::draw_notes(std::vector<std::string> const &notes, float y)
{
    auto const line_height = 6.0f * App::measure_text("Mg", FontSize::Small).y / 5.0f;
    for (auto const &line : notes) {
        render_text(116, y, line, FontSize::Small, RAYWHITE);
        y += line_height;
    }
}

// Draws the fields, the selected lap's or the activity's, in two columns.
// In each column the values start at the same x, past the column's widest
// label, so that they line up in any font.
void ActivityDataDisplay::draw_fields(float y)
{
    auto const &a = activity->activity;
    auto const  lines = [this, &a] {
        auto const comparison = activity->comparison();
        if (activity->lap) {
            return data_fields(a, a.laps[*activity->lap].segment, a.laps[*activity->lap].first_record, comparison);
        }
        if (activity->range_stats && activity->segment) {
            return data_fields(a, *activity->range_stats, activity->segment->min, comparison);
        }
        return data_fields(a, a.segment, 0, comparison);
    }();
    auto const gap = App::the()->char_size(FontSize::Medium).x;
    auto const line_height = 6.0f * App::measure_text("Mg", FontSize::Medium).y / 5.0f;
    auto const label_width = [](DataField const &field) {
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

// Clicking the title or the pen after it renames the activity, as F2
// does. Clicking a maximum marks the record where it happened, as hovering
// over the graph does.
void ActivityDataDisplay::process_input()
{
    if (activity == nullptr) {
        return;
    }
    auto const mouse = GetMousePosition();
    renaming_hovered = CheckCollisionPointRec(mouse, title_area) || CheckCollisionPointRec(mouse, pen_area);
    if (renaming_hovered) {
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
    : Widget(parent, SizePolicy::Absolute, PlotHeight)
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

float ActivityGraph::needed_height()
{
    return top_band() + PlotHeight + bottom_band();
}

namespace {

// The strip's laps alternate between these, so that each boundary shows.
constexpr Color LapShades[] = { { 70, 70, 70, 255 }, { 110, 110, 110, 255 } };
constexpr Color SelectedLapShade = RAYWHITE;
constexpr Color SelectedRangeShade = { 255, 255, 255, 40 };

// Room between the legend's entries, and between labels along the graph.
constexpr float LabelGap = 10.0f;
// How far a tick mark sticks out from the plot or the strip.
constexpr float TickLength = 4.0f;
// The legend's distance from the top of the graph.
constexpr float LegendTop = 5.0f;

// One line of the graph, drawn a pixel column at a time. A column without
// a value breaks it: a dropout shows as a gap.
struct GraphLine {
    Color                  color;
    std::optional<Vector2> previous { };

    void to(Image &image, float x, std::optional<float> y)
    {
        if (!y) {
            previous.reset();
            return;
        }
        Vector2 const point { .x = x, .y = std::ceilf(*y) };
        if (previous) {
            ImageDrawLineV(&image, *previous, point, color);
        }
        previous = point;
    }
};

// The recording's records as recorded, which the graph smooths itself.
// An activity that wasn't analyzed has only `records`.
std::span<record const> as_recorded(Activity const &activity)
{
    if (activity.recorded.size() == activity.records.size()) {
        return activity.recorded;
    }
    return activity.records;
}

}

float ActivityGraph::label_row()
{
    return std::ceilf(App::the()->char_size(FontSize::Small).y);
}

float ActivityGraph::top_band()
{
    return LegendTop + label_row() + LabelGap / 2 + label_row() + TickLength + 2;
}

float ActivityGraph::bottom_band()
{
    return TickLength + label_row() + 2;
}

float ActivityGraph::plot_width() const
{
    return viewport.width - 20;
}

float ActivityGraph::plot_height() const
{
    return viewport.height - top_band() - strip_height() - bottom_band();
}

float ActivityGraph::strip_height() const
{
    if (!activity || !activity->has_laps()) {
        return 0.0f;
    }
    return std::ceilf(0.6f * App::the()->char_size().y);
}

Rectangle ActivityGraph::plot_area() const
{
    return Rectangle {
        .x = viewport.x,
        .y = viewport.y + top_band(),
        .width = plot_width(),
        .height = plot_height(),
    };
}

Rectangle ActivityGraph::strip_area() const
{
    auto const height = strip_height();
    return Rectangle {
        .x = viewport.x,
        .y = viewport.y + top_band() + plot_height(),
        .width = plot_width(),
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
    auto const spans = lap_spans(activity->activity, rendered_view, area.width);
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
    // Zoomed in, the selection can reach past the view, or lie outside it.
    auto const width = plot_width();
    auto const x0 = std::clamp(view_x(rendered_view, activity->segment->min, width), 0.0f, width);
    auto const x1 = std::clamp(view_x(rendered_view, activity->segment->max + 1, width), 0.0f, width);
    if (x1 > x0) {
        draw_rectangle_no_normalize(x0, top_band(), x1 - x0, plot_height(), SelectedRangeShade);
    }
}

// Labels centred on their ticks, at `y`, those that would overlap the one
// before left out, with a tick mark from `tick_y` to `tick_y + tick`.
void ActivityGraph::draw_ticks(std::vector<GraphTick> const &ticks, float y, float tick_y, float tick) const
{
    auto const         width = plot_width();
    std::vector<float> centres;
    std::vector<float> widths;
    for (auto const &t : ticks) {
        auto const w = App::measure_text(t.label.c_str(), FontSize::Small).x;
        // A label wider than the graph isn't drawn.
        centres.push_back(w < width ? std::clamp(t.x, w / 2, width - w / 2) : t.x);
        widths.push_back(w < width ? w : 0);
    }
    for (auto const ix : visible_labels(centres, widths, LabelGap)) {
        draw_line_no_normalize(ticks[ix].x, tick_y, ticks[ix].x, tick_y + tick, ChartScaleColor);
        render_text(centres[ix] - widths[ix] / 2, y, ticks[ix].label, FontSize::Small, ChartLabelColor);
    }
}

// The lines' ranges, or while the mouse is over the plot the values at
// the mark. The mark stays when the mouse leaves, for the map, and is set
// from the data panel too: neither is hovering.
void ActivityGraph::draw_legend() const
{
    auto       labels = ranges;
    auto const hovering = CheckCollisionPointRec(GetMousePosition(), plot_area());
    if (auto const mark = activity->mark; mark && hovering && *mark < activity->activity.records.size()) {
        auto const recorded = as_recorded(activity->activity);
        if (auto const &compared = activity->compared; compared) {
            auto const pair = *mark < compared->pairs.size() ? compared->pairs[*mark] : std::nullopt;
            auto const other_recorded = as_recorded(compared->activity);
            auto const other = pair && *pair < other_recorded.size() ? std::optional { other_recorded[*pair] } : std::nullopt;
            labels = compare_banner(activity->activity, recorded[*mark], other);
        } else {
            labels = legend_values(activity->activity, recorded[*mark]);
        }
        auto position = mark_position(activity->activity.records, *mark);
        labels.insert(labels.end(), position.begin(), position.end());
    }
    auto x = 0.0f;
    for (auto const &label : labels) {
        x += render_text(x, LegendTop, label.text, FontSize::Small, label.color).x + LabelGap;
    }
}

void ActivityGraph::draw()
{
    if (!activity) {
        return;
    }
    // Zooming is a task, which doesn't know about the graph.
    if (activity->view() != rendered_view) {
        render();
    }
    auto const height = plot_height();
    if (texture.id != 0) {
        draw_line(0, top_band() + height / 2, plot_width(), top_band() + height / 2, ChartGridColor);
    }
    render_texture(0, top_band(), texture);
    draw_selected_range();
    draw_strip();
    auto const top = top_band();
    draw_ticks(km_labels, top - TickLength - 2 - label_row(), top - TickLength, TickLength);
    auto const below = top + height + strip_height();
    draw_ticks(time_labels, below + TickLength, below, TickLength);
    if (activity->mark && *activity->mark >= rendered_view.first && *activity->mark < rendered_view.end) {
        auto const x = view_x(rendered_view, *activity->mark, plot_width());
        draw_line_no_normalize(x, top_band(), x, top_band() + height, MarkColor);
    }
    draw_legend();
}

// The layout resizes all its widgets whenever any of them changes: only a
// new size needs the graph drawn again.
void ActivityGraph::resize()
{
    if (plot_width() != rendered.x || plot_height() != rendered.y) {
        render();
    }
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
        return view_record(rendered_view, mouse.x - viewport.x, plot_width());
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
        auto const lap = lap_at(lap_spans(activity->activity, rendered_view, strip.width), mouse.x - strip.x);
        if (lap) {
            App::the()->request_cursor(MOUSE_CURSOR_POINTING_HAND);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                activity->toggle_lap(*lap);
            }
        }
        return;
    }

    // A drag starts with a press in the plot, and ends a lap's selection.
    // The legend and the labels around it don't respond.
    if (!CheckCollisionPointRec(mouse, plot_area())) {
        return;
    }
    // A right-click opens the menu to extend the selection, or to select
    // from the record clicked, the mark, if there's no selection.
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        activity->set_mark(record_at());
        SweatTrails::the()->selection_menu(mouse);
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
    this->activity = activity;
    render();
}

void ActivityGraph::render()
{
    // Reset them after unloading: an activity without data to graph doesn't
    // make new ones, and draw() mustn't draw the old texture, nor the next
    // activity unload the old ones again. Unloading empty ones does nothing,
    // and an empty texture isn't drawn.
    UnloadTexture(texture);
    UnloadImage(image);
    texture = { };
    image = { };
    ranges.clear();
    time_labels.clear();
    km_labels.clear();
    rendered = { .x = plot_width(), .y = plot_height() };
    rendered_view = activity ? activity->view() : GraphView { };
    if (!activity || rendered_view.count() == 0) {
        return;
    }
    auto const width = static_cast<int>(rendered.x);
    auto const height = static_cast<int>(rendered.y);
    if (width <= 0 || height <= 0) {
        return;
    }
    time_labels = time_ticks(activity->activity.records, rendered_view, rendered.x);
    km_labels = distance_ticks(activity->activity.records, rendered_view, rendered.x);
    image = GenImageColor(width, height, BLANK);
    if (activity->compared) {
        render_compared(width, height);
    } else {
        render_lines(width, height);
    }
    texture = LoadTextureFromImage(image);
}

// Altitude as an area under the lines, and speed, heart rate and power,
// power on top.
void ActivityGraph::render_lines(int width, int height)
{
    auto const &shown = activity->activity;
    auto const  recorded = as_recorded(shown);
    auto const  h = static_cast<float>(height);
    auto const  speed = is_on_foot(shown) ? GraphValue::Pace : GraphValue::Speed;
    for (auto const value : { GraphValue::Altitude, speed, GraphValue::HeartRate, GraphValue::Power }) {
        // The scale is the values', not the columns': a column averages
        // its records, and would hide the lowest and highest of them.
        auto const series = view_series(graph_series(recorded, value), rendered_view);
        auto const columns = graph_columns(series, width);
        auto const scale = graph_scale(value, std::span { &series, 1 });
        if (!scale) {
            continue;
        }
        auto const label = legend_range(shown, value, *scale);
        ranges.push_back(label);
        GraphLine line { .color = label.color };
        for (auto x = 0uz; x < columns.size(); ++x) {
            auto const y = columns[x] ? std::optional { graph_y(*scale, *columns[x], h) } : std::nullopt;
            if (value != GraphValue::Altitude) {
                line.to(image, static_cast<float>(x), y);
            } else if (y) {
                ImageDrawRectangleRec(&image, Rectangle { .x = static_cast<float>(x), .y = *y, .width = 1, .height = h - *y }, label.color);
            }
        }
    }
}

// Power, heart rate and cadence of both recordings, the other's lighter,
// on one scale for both. The other's records are paired with the shown
// one's, so that both are drawn along the same x. Power is drawn last, on
// top, but comes first in the legend, as in the data panel.
void ActivityGraph::render_compared(int width, int height)
{
    auto const &shown = activity->activity;
    auto const &compared = *activity->compared;
    auto const  other_recorded = as_recorded(compared.activity);
    auto const  h = static_cast<float>(height);
    for (auto const value : { GraphValue::Cadence, GraphValue::HeartRate, GraphValue::Power }) {
        auto const  other_series = graph_series(other_recorded, value);
        GraphSeries paired(shown.records.size());
        for (auto ix = 0uz; ix < paired.size() && ix < compared.pairs.size(); ++ix) {
            if (auto const pair = compared.pairs[ix]; pair && *pair < other_series.size()) {
                paired[ix] = other_series[*pair];
            }
        }
        std::array const series { view_series(graph_series(as_recorded(shown), value), rendered_view), view_series(paired, rendered_view) };
        std::array const columns { graph_columns(series[0], width), graph_columns(series[1], width) };
        auto const       scale = graph_scale(value, series);
        if (!scale) {
            continue;
        }
        auto const label = legend_range(shown, value, *scale);
        ranges.insert(ranges.begin(), label);
        GraphLine ours { .color = label.color };
        GraphLine theirs { .color = ColorBrightness(label.color, 0.5f) };
        for (auto x = 0uz; x < static_cast<size_t>(width); ++x) {
            auto const y = [&](GraphSeries const &series) {
                return x < series.size() && series[x] ? std::optional { graph_y(*scale, *series[x], h) } : std::nullopt;
            };
            theirs.to(image, static_cast<float>(x), y(columns[1]));
            ours.to(image, static_cast<float>(x), y(columns[0]));
        }
    }
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
    show(activity, false);
}

void ActivityDisplay::compare_with(std::optional<Activity> const &other)
{
    if (!activity) {
        return;
    }
    activity->compare_with(other);
    show_laps();
    graph->set_activity(activity);
    // The data panel shows the comparison's fields.
    resize();
}

void ActivityDisplay::switch_recording(Activity const &recording)
{
    auto const lap = activity ? activity->lap : std::nullopt;
    show(recording, true);
    activity->keep_lap(lap);
}

void ActivityDisplay::show(Activity const &activity, bool keep_view)
{
    this->activity = std::make_shared<DisplayedActivity>(activity);
    show_laps();
    data_display->set_activity(this->activity);
    map->set_activity(this->activity, keep_view);
    preview->set_activity(this->activity);
    graph->set_activity(this->activity);
    resize();
}

void ActivityDisplay::laps_changed()
{
    show_laps();
    resize();
}

void ActivityDisplay::show_laps()
{
    if (activity && activity->has_laps()) {
        std::vector<std::optional<Comparison>> compared;
        if (activity->compared) {
            for (auto ix = 0uz; ix < activity->activity.laps.size(); ++ix) {
                compared.push_back(activity->lap_comparison(ix));
            }
        }
        auto [columns, rows] = lap_table(activity->activity, compared);
        laps->set_rows(std::move(columns), std::move(rows));
        laps->policy = SizePolicy::Stretch;
    } else {
        laps->set_rows({ }, { });
        laps->policy = SizePolicy::Hide;
    }
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

// The graph's bands are as high as their text, which the fonts, loaded
// after the widgets are made, decide.
void ActivityDisplay::on_resize()
{
    graph->policy_size = ActivityGraph::needed_height();
}

ActivityColumn::ActivityColumn(pWidget const &parent)
    : Layout(parent, ContainerOrientation::Vertical)
{
}

void ActivityColumn::initialize()
{
    data_display = add_widget<ActivityDataDisplay>();
    laps = add_widget<Table>(1);
    laps->policy = SizePolicy::Hide;
}

void ActivityColumn::on_resize()
{
    if (laps->policy == SizePolicy::Hide) {
        data_display->policy = SizePolicy::Stretch;
        return;
    }
    // On a small window the panel gives way: the layout needs room for
    // the table, which stretches.
    auto const least_table = 3 * 1.2f * App::the()->char_size().y;
    data_display->policy = SizePolicy::Absolute;
    data_display->policy_size = std::max(0.0f, std::min(data_display->needed_height(viewport.width), viewport.height - least_table));
}

void ActivityDisplay::initialize()
{
    central = add_widget<Layout>(ContainerOrientation::Horizontal, SizePolicy::Stretch);
    column = central->add_widget<ActivityColumn>();
    map = central->add_widget<ActivityMap>();
    preview = add_widget<OverlayPreview>();
    data_display = column->data_display;
    laps = column->laps;
    laps->on_click = [this](size_t row) {
        if (activity) {
            activity->toggle_lap(row);
        }
    };
    graph = add_widget<ActivityGraph>();
}

}
