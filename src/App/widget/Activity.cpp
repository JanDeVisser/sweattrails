#include <cmath>
#include <memory>
#include <raylib.h>

#include <Format.h>
#include <SweatTrails.h>
#include <Widget.h>
#include <storage/Activity.h>
#include <widget/Activity.h>

namespace ST {

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

void ActivityDataDisplay::draw()
{
    if (auto const &activity = std::dynamic_pointer_cast<ActivityDisplay>(parent)->activity; activity) {
        auto y = 20;

        render_texture(10, y, sport_icon);
        auto sz = render_text(
            116, y,
            TextFormat("%s - %s", FIT::tag(activity->sport), activity->segment.start_time.format().c_str()),
            FontSize::Small,
            RAYWHITE);
        sz = render_text(116, y + sz.y + 20, activity->title, FontSize::Large, RAYWHITE);
        y += 106; // 6 * sz.y / 5;

        char const *d;
        auto        moving = activity->segment.moving;
        if (moving.minutes == 0) {
            d = TextFormat("Moving Time:   %02d.%03d", moving.seconds, static_cast<int>(moving.fraction * 1000));
        } else if (moving.hours == 0) {
            d = TextFormat("Moving Time:   %02d:%02d", moving.minutes, moving.seconds);
        } else {
            d = TextFormat("Moving Time:   %d:%02d:%02d", moving.hours, moving.minutes, moving.seconds);
        }
        sz = render_text(10, y, d, FontSize::Medium, RAYWHITE);

        auto elapsed = activity->segment.elapsed;
        if (elapsed.hours == 0) {
            d = TextFormat("Elapsed Time:   %02d:%02d.%03d", elapsed.minutes, elapsed.seconds, static_cast<int>(elapsed.fraction * 1000));
        } else {
            d = TextFormat("Elapsed Time:   %d:%02d:%02d", elapsed.hours, elapsed.minutes, elapsed.seconds);
        }
        auto right_column = viewport.width / 2 + 10;
        sz = render_text(right_column, y, d, FontSize::Medium, RAYWHITE);
        y += 6 * sz.y / 5;

        auto distance = activity->segment.distance / 1000.0;
        if (distance > 0) {
            if (distance < 1.0) {
                d = TextFormat("Distance:      %3d m", static_cast<int>(activity->segment.distance));
            } else if (distance < 10.0) {
                d = TextFormat("Distance:      %4.2f km", distance);
            } else if (distance < 20.0) {
                d = TextFormat("Distance:      %5.2f km", distance);
            } else if (distance < 100.0) {
                d = TextFormat("Distance:      %4.1f km", distance);
            } else {
                d = TextFormat("Distance:      %3.0f km", distance);
            }
            sz = render_text(10, y, d, FontSize::Medium, RAYWHITE);
            y += 6 * sz.y / 5;

            Duration pace(activity->segment.moving.elapsed / distance);
            d = TextFormat("Avg Pace:      %d:%02d min/km", pace.minutes, pace.seconds);
            sz = render_text(10, y, d, FontSize::Medium, RAYWHITE);
            y += 6 * sz.y / 5;
        }
        if (activity->segment.computed_elevation_range.range) {
            d = TextFormat("Min elevation: %d", static_cast<int>(activity->segment.computed_elevation_range.min()));
            sz = render_text(10, y, d, FontSize::Medium, RAYWHITE);
            d = TextFormat("Max elevation: %d", static_cast<int>(activity->segment.computed_elevation_range.max()));
            sz = render_text(right_column, y, d, FontSize::Medium, RAYWHITE);
        }
    }
}

void ActivityDataDisplay::resize()
{
}

void ActivityDataDisplay::process_input()
{
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

struct Text {
    std::string text;
    Color       color;
};

void ActivityGraph::draw()
{
    if (!activity) {
        return;
    }
    render_texture(0, 50, texture);
    if (mark) {
        auto const  width = this->viewport.width - 20;
        float       dt = static_cast<float>(width) / static_cast<float>(activity->records.size());
        float       x = *mark * dt;
        auto const &record { activity->records[*mark] };
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
    if (mouse_in()) {
        auto const width = this->viewport.width - 20;
        float      dt = static_cast<float>(width) / static_cast<float>(activity->records.size());
        size_t     t = static_cast<size_t>(std::truncf((GetMouseX() - viewport.x) / dt));
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (!dragging) {
                clear_segment();
                clear_mark();
            }
            dragging = true;
            set_segment(t);
        } else {
            dragging = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                clear_segment();
            } else if (IsMouseButtonUp(MOUSE_BUTTON_LEFT)) {
                set_mark(t);
            }
        }
    }
}

void ActivityGraph::clear_segment()
{
    segment.reset();
}

void ActivityGraph::clear_mark()
{
    mark.reset();
}

void ActivityGraph::set_mark(size_t t)
{
    mark = t;
}

void ActivityGraph::set_segment(size_t t)
{
    if (segment) {
        if (t < segment->min) {
            segment->min = t;
            segment->max = t;
        } else {
            segment->max = t;
        }
    } else {
        segment = RecordRange { .min = t, .max = t };
    }
}

void ActivityGraph::set_activity(Activity const &activity)
{
    if (this->activity) {
        UnloadTexture(texture);
        UnloadImage(image);
    }
    auto const          &seg = activity.segment;
    auto const           width = this->viewport.width - 20;
    auto const           height = this->viewport.height - 50;
    float                prev_x = 0.0;
    float                prev_speed = 0.0;
    float                prev_power = 0.0;
    float                prev_hr = 0.0;
    float                dt = static_cast<float>(width) / static_cast<float>(activity.records.size());
    std::optional<float> dalt_maybe { };
    if (seg.computed_elevation_range) {
        dalt_maybe = static_cast<float>(height) / static_cast<float>(*seg.computed_elevation_range.diff());
        std::println("dalt_maybe: {}", *dalt_maybe);
    }
    std::optional<float> dspeed_maybe { };
    if (seg.computed_speed_range.range) {
        dspeed_maybe = static_cast<float>(height) / static_cast<float>(*seg.computed_speed_range.diff());
        std::println("dspeed_maybe: {}", *dspeed_maybe);
    }
    std::optional<float> dpower_maybe { };
    if (seg.computed_power_range.range) {
        dpower_maybe = static_cast<float>(height) / static_cast<float>(*seg.computed_power_range.diff());
        std::println("dpower_maybe: {}", *dpower_maybe);
    }
    std::optional<float> dhr_maybe { };
    if (seg.computed_hr_range.range) {
        dhr_maybe = static_cast<float>(height) / static_cast<float>(*seg.computed_hr_range.diff());
        std::println("dhr_maybe: {}", *dhr_maybe);
    }
    if (!dalt_maybe && !dspeed_maybe && !dpower_maybe && !dhr_maybe) {
        return;
    }

    image = GenImageColor(width, height, BLANK);
    size_t ix = 0;
    for (auto const &record : activity.records) {
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
    this->activity = activity;
}

ActivityDisplay::ActivityDisplay(pWidget const &parent)
    : Layout(parent, ContainerOrientation::Vertical)
{
}

void ActivityDisplay::set_activity(Activity const &activity)
{
    if (this->activity) {
        UnloadTexture(data_display->sport_icon);
    }
    this->activity = activity;
    char const *icon;
    switch (activity.sport) {
    case sport::Cycling:
        icon = "icons8-cycling-skin-type-2-96";
        break;
    case sport::Running:
        icon = "icons8-running-skin-type-2-96";
        break;
    default:
        icon = "share/icons/icons8-hyperactive-skin-type-3-96";
        break;
    }
    data_display->sport_icon = LoadTexture(TextFormat(SWEATTRAILS_DATADIR "/icons/%s.png", icon));
    graph->set_activity(activity);
}

void ActivityDisplay::initialize()
{
    data_display = add_widget<ActivityDataDisplay>();
    graph = add_widget<ActivityGraph>();
}

}
