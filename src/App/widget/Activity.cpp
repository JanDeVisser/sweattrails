#include <cmath>
#include <memory>
#include <ranges>
#include <raylib.h>

#include <Format.h>
#include <SweatTrails.h>
#include <Widget.h>
#include <storage/Activity.h>
#include <widget/Activity.h>

namespace ST {

struct Text {
    std::string text;
    Color       color;
};

DisplayedActivity::DisplayedActivity(Activity const &activity)
    : activity(activity)
{
}

void DisplayedActivity::clear_segment()
{
    segment.reset();
}

void DisplayedActivity::clear_mark()
{
    mark.reset();
}

void DisplayedActivity::set_mark(size_t t)
{
    mark = t;
}

void DisplayedActivity::set_segment(size_t t)
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
    if (activity != nullptr) {
        auto y = 20;

        render_texture(10, y, sport_icon);
        auto sz = render_text(
            116, y,
            TextFormat("%s - %s", FIT::tag(activity->activity.sport), activity->activity.segment.start_time.format().c_str()),
            FontSize::Small,
            RAYWHITE);
        sz = render_text(116, y + sz.y + 20, activity->activity.title, FontSize::Large, RAYWHITE);
        y += 106; // 6 * sz.y / 5;

        char const *d;
        auto        moving = activity->activity.segment.moving;
        if (moving.minutes == 0) {
            d = TextFormat("Moving Time:   %02d.%03d", moving.seconds, static_cast<int>(moving.fraction * 1000));
        } else if (moving.hours == 0) {
            d = TextFormat("Moving Time:   %02d:%02d", moving.minutes, moving.seconds);
        } else {
            d = TextFormat("Moving Time:   %d:%02d:%02d", moving.hours, moving.minutes, moving.seconds);
        }
        sz = render_text(10, y, d, FontSize::Medium, RAYWHITE);

        auto elapsed = activity->activity.segment.elapsed;
        if (elapsed.hours == 0) {
            d = TextFormat("Elapsed Time:   %02d:%02d.%03d", elapsed.minutes, elapsed.seconds, static_cast<int>(elapsed.fraction * 1000));
        } else {
            d = TextFormat("Elapsed Time:   %d:%02d:%02d", elapsed.hours, elapsed.minutes, elapsed.seconds);
        }
        auto right_column = viewport.width / 2 + 10;
        sz = render_text(right_column, y, d, FontSize::Medium, RAYWHITE);
        y += 6 * sz.y / 5;

        auto distance = activity->activity.segment.distance / 1000.0;
        if (distance > 0) {
            if (distance < 1.0) {
                d = TextFormat("Distance:      %3d m", static_cast<int>(activity->activity.segment.distance));
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

            Duration pace(activity->activity.segment.moving.elapsed / distance);
            d = TextFormat("Avg Pace:      %d:%02d min/km", pace.minutes, pace.seconds);
            sz = render_text(10, y, d, FontSize::Medium, RAYWHITE);
            y += 6 * sz.y / 5;
        }
        if (activity->activity.segment.computed_elevation_range.range) {
            d = TextFormat("Min elevation: %d", static_cast<int>(activity->activity.segment.computed_elevation_range.min()));
            sz = render_text(10, y, d, FontSize::Medium, RAYWHITE);
            d = TextFormat("Max elevation: %d", static_cast<int>(activity->activity.segment.computed_elevation_range.max()));
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

void ActivityDataDisplay::set_activity(std::shared_ptr<DisplayedActivity> const &activity)
{
    if (this->activity) {
        UnloadTexture(sport_icon);
    }
    this->activity = activity;
    if (this->activity) {
        char const *icon;
        switch (activity->activity.sport) {
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
        sport_icon = LoadTexture(TextFormat(SWEATTRAILS_DATADIR "/icons/%s.png", icon));
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

void ActivityGraph::draw()
{
    if (!activity) {
        return;
    }
    render_texture(0, 50, texture);
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
    if (mouse_in()) {
        auto const width = this->viewport.width - 20;
        float      dt = static_cast<float>(width) / static_cast<float>(activity->activity.records.size());
        size_t     t = static_cast<size_t>(std::truncf((GetMouseX() - viewport.x) / dt));
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (!dragging) {
                activity->clear_segment();
                activity->clear_mark();
            }
            dragging = true;
            activity->set_segment(t);
        } else {
            dragging = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                activity->clear_segment();
            } else if (IsMouseButtonUp(MOUSE_BUTTON_LEFT)) {
                activity->set_mark(t);
            }
        }
    }
}
void ActivityGraph::set_activity(std::shared_ptr<DisplayedActivity> const &activity)
{
    if (this->activity) {
        UnloadTexture(texture);
        UnloadImage(image);
    }
    this->activity = activity;
    if (!this->activity) {
        return;
    }

    auto const          &seg = activity->activity.segment;
    auto const           width = this->viewport.width - 20;
    auto const           height = this->viewport.height - 50;
    float                prev_x = 0.0;
    float                prev_speed = 0.0;
    float                prev_power = 0.0;
    float                prev_hr = 0.0;
    float                dt = static_cast<float>(width) / static_cast<float>(activity->activity.records.size());
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

ActivityMap::ActivityMap(pWidget const &parent)
    : Widget(parent, SizePolicy::Relative, 0.33)
{
    padding = { 10, 10, 10, 10 };
}

ActivityMap::~ActivityMap()
{
    UnloadTexture(texture);
    UnloadImage(image);
}

void ActivityMap::initialize()
{
}

void ActivityMap::draw()
{
    if (!activity) {
        return;
    }
    DrawRectangleLines(viewport.x - 3, viewport.y - 3, viewport.width + 6, viewport.height + 6, RAYWHITE);
    render_texture(0, 0, texture, map.rectangle.x, map.rectangle.y, viewport.width, viewport.height, RAYWHITE);
    constexpr static float thick = 3.0f;
    auto                   p_prev = track[0];
    for (auto const &p : track) {
        if (!std::isnan(p.x) && !std::isnan(p.y) && (std::abs(p_prev.x - p.x) > 1 || std::abs(p_prev.y - p.y) > 1)) {
            DrawLineEx(p_prev, p, thick, RED);
            DrawCircleV(p, thick / 2, RED);
            p_prev = p;
        }
        if (std::isnan(p_prev.x) && std::isnan(p_prev.y)) {
            p_prev = p;
        }
    }
    if (activity->segment) {
        auto p_prev = track[activity->segment->min];
        for (size_t i = activity->segment->min + 1; i < activity->segment->max; ++i) {
            auto const &p = track[i];
            if (!std::isnan(p.x) && !std::isnan(p.y) && (std::abs(p_prev.x - p.x) > 1 || std::abs(p_prev.y - p.y) > 1)) {
                DrawLineEx(p_prev, p, thick, BLUE);
                DrawCircleV(p, thick / 2, BLUE);
                p_prev = p;
            }
        }
    }
    if (activity->mark) {
        DrawCircleV(track[*activity->mark], thick, BLACK);
    }
}

void ActivityMap::resize()
{
}

void ActivityMap::process_input()
{
}

void ActivityMap::set_activity(std::shared_ptr<DisplayedActivity> const &activity)
{
    if (this->activity) {
        UnloadTexture(texture);
        UnloadImage(image);
    }
    this->activity = activity;
    if (!this->activity) {
        return;
    }
    if (!this->activity->activity.segment.box) {
        policy = SizePolicy::Hide;
        return;
    }
    policy = SizePolicy::Relative;

    auto const route_area = *activity->activity.segment.box;

    // TODO: make async
    auto map_maybe = Map::init(SweatTrails::the()->storage, route_area, viewport.width, viewport.height);
    if (!map_maybe) {
        std::println("Could not load map: {}", map_maybe.error().to_string());
        policy = SizePolicy::Hide;
        return;
    }
    map = std::move(map_maybe.value());
    auto const         mid = route_area.center();
    std::vector<Image> images;
    images.resize(map.num_tiles);
    int ix = 0;
    for (auto const &m : map.tiles) {
        images[ix++] = LoadImageFromMemory(".png", reinterpret_cast<unsigned char const *>(m.data()), static_cast<int>(m.size()));
    }

    image = GenImageColor(map.columns * 256, map.rows * 256, BLANK);
    for (auto ix = 0; ix < map.num_tiles; ++ix) {
        ImageDraw(
            &image,
            images[ix],
            Rectangle {
                .x = 0,
                .y = 0,
                .width = 256,
                .height = 256,
            },
            Rectangle {
                .x = static_cast<float>((ix % map.columns) * 256),
                .y = static_cast<float>((ix / map.columns) * 256),
                .width = 256,
                .height = 256,
            },
            WHITE);
        UnloadImage(images[ix]);
    }

    track.clear();
    for (auto const &record : activity->activity.records) {
        if (record.position) {
            auto const &position = *record.position;
            if (std::abs(position.lat) > 0.1 && std::abs(position.lon) > 0.1) {
                auto const n = static_cast<float>(1u << map.zoom);
                auto const x = ((position.lon + 180.0f) / 360.0f * n - map.x) * 256.0f;
                auto const y = ((1.0f - std::asinh(std::tan(position.lat * PI / 180.0f)) / PI) / 2.0f * n - map.y) * 256.0f;

                track.emplace_back(
                    viewport.x + x - map.rectangle.x,
                    viewport.y + y - map.rectangle.y);
                continue;
            }
        }
        if (!track.empty()) {
            track.emplace_back(track.back());
        } else {
            track.emplace_back(std::nanf(nullptr), std::nanf(nullptr));
        }
    }
    texture = LoadTextureFromImage(image);
}

ActivityDisplay::ActivityDisplay(pWidget const &parent)
    : Layout(parent, ContainerOrientation::Vertical)
{
}

void ActivityDisplay::set_activity(Activity const &activity)
{
    this->activity = std::make_shared<DisplayedActivity>(activity);
    data_display->set_activity(this->activity);
    map->set_activity(this->activity);
    graph->set_activity(this->activity);
    resize();
}

void ActivityDisplay::clear_activity()
{
    this->activity = nullptr;
    data_display->set_activity(nullptr);
    map->set_activity(nullptr);
    graph->set_activity(nullptr);
    resize();
}

void ActivityDisplay::initialize()
{
    auto central = add_widget<Layout>(ContainerOrientation::Horizontal, SizePolicy::Stretch);
    data_display = central->add_widget<ActivityDataDisplay>();
    map = central->add_widget<ActivityMap>();
    graph = add_widget<ActivityGraph>();
}

}
