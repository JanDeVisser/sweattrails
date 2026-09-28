/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <concepts>
#include <condition_variable>
#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#include <raylib.h>

#include <Format.h>
#include <JSON.h>
#include <Logging.h>
#include <Utf8.h>

namespace ST {

#define PADDING 5.0f

#define KEYBOARDMODIFIERS(S) \
    S(None, 0, "")           \
    S(Shift, 1, "S-")        \
    S(Control, 2, "C-")      \
    S(Alt, 4, "M-")          \
    S(Super, 8, "U-")

using KeyboardModifier = uint8_t;

#undef KEYBOARDMODIFIER
#define KEYBOARDMODIFIER(mod, ord, str) constexpr KeyboardModifier KMod##mod = (ord);
KEYBOARDMODIFIERS(KEYBOARDMODIFIER)
#undef KEYBOARDMODIFIER
constexpr KeyboardModifier KModCount = 16;

extern bool             is_modifier_down(KeyboardModifier modifier);
extern std::string      modifier_string(KeyboardModifier modifiers);
extern KeyboardModifier modifier_current();

#define CONTAINERORIENTATIONS(S) \
    S(Horizontal, 0)             \
    S(Vertical, 1)

enum class ContainerOrientation {
#undef S
#define S(O, V) O = V,
    CONTAINERORIENTATIONS(S)
#undef S
};

enum class SizePolicy {
    Absolute = 0,
    Relative,
    Characters,
    Calculated,
    Stretch,
    Hide,
};

extern char const *SizePolicy_name(SizePolicy policy);
extern char const *ContainerOrientation_name(ContainerOrientation orientation);

using rune = wchar_t;
using rune_view = std::basic_string_view<rune>;
using rune_string = std::basic_string<rune>;

template<typename T1, typename T2>
    requires std::convertible_to<T2, T1>
constexpr T1 min(T1 i1, T2 i2)
{
    return (i2 < i1) ? i2 : i1;
}

template<typename T1, typename T2>
    requires std::convertible_to<T2, T1>
constexpr T1 max(T1 i1, T2 i2)
{
    return (i2 > i1) ? i2 : i1;
}

template<typename T, typename Min, typename Max>
    requires std::convertible_to<Min, T> && std::convertible_to<Max, T>
constexpr T clamp(T v, Min low, Max high)
{
    T h = max(low, high);
    T l = min(low, high);
    return min(max(v, l), h);
}

template<typename T, typename Min, typename Max>
    requires std::convertible_to<Min, T> && std::convertible_to<Max, T>
constexpr bool contains(T v, Min low, Max high)
{
    return v >= low && v <= high;
}

template<typename T>
union Vec {
    struct {
        T x;
        T y;
    };
    struct {
        T column;
        T line;
    };
    T coords[2];
};

using Position = Vec<size_t>;

template<typename T>
    requires std::convertible_to<T, int>
union Rect {
    constexpr Rect(T c1, T c2, T c3, T c4)
        : left(c1)
        , top(c2)
        , right(c3)
        , bottom(c4)
    {
    }

    explicit constexpr Rect(T c)
        : Rect(c, c, c, c)
    {
    }

    constexpr Rect() = default;
    // constexpr Rect(Rect const &) = default;
    struct {
        T x { 0 };
        T y { 0 };
        T width { 0 };
        T height { 0 };
    };
    struct {
        T left;
        T top;
        T right;
        T bottom;
    };
    //  Rectangle r;
    T coords[4];
    struct {
        Vec<T> position;
        Vec<T> size;
    };

    constexpr std::string
    to_string()
    {
        return std::format("{}x{}@+{},+{}",
            static_cast<int>(width), static_cast<int>(height),
            static_cast<int>(x), static_cast<int>(y));
    }

    constexpr static Rect<T> zero()
    {
        return Rect<T>();
    }
};

struct App;

struct KeyCombo {
    int              key;
    KeyboardModifier modifier;

    auto operator<=>(KeyCombo const &) const = default;
};

constexpr auto ZeroPadding = Rect<float> { 0.0 };
constexpr auto DefaultPadding = Rect<float> { 5.0 };

using pWidget = std::shared_ptr<struct Widget>;

template<typename T>
concept Application = std::derived_from<T, struct App>;

void job_handler(void *bus);

template<typename C, typename... Payloads>
struct Message {
    using Choices = C;
    using Payload = std::variant<Payloads...>;
    using Handler = std::function<void(Payload const &)>;
    static constexpr size_t Count = static_cast<size_t>(Choices::Count);
    Choices                 choice;
    Payload                 data;
    Handler                 handler;

    template<Choices Choice>
    static Message make(Payload payload, std::array<Handler, Count> handlers)
    {
        return Message { Choice, payload, handlers[static_cast<int>(Choice)] };
    }

    void execute() const
    {
        handler(data);
    }
};

template<typename Job, typename Notification>
struct AppBus {
    struct Task {
        using Handler = std::function<void(pWidget const &, JSONValue const &)>;
        AppBus     &bus;
        std::string task;
        pWidget     owner;
        Handler     handler;

        Task(AppBus &bus, std::string name, pWidget const &owner, Handler handler)
            : bus(bus)
            , task(std::move(name))
            , owner(owner)
            , handler(std::move(handler))
        {
        }
        Task(Task const &) = default;

        Task &bind(KeyCombo const &combo, JSONValue const &arguments = JSONValue { })
        {
            return bus.bind(*this, combo, arguments);
        }
    };

    struct Binding {
        KeyCombo    combo;
        std::string task;
        JSONValue   arguments;
    };

    struct PendingTask {
        Task      task;
        pWidget   current_focus;
        JSONValue arguments;

        PendingTask(Task const &task, JSONValue arguments = JSONValue { })
            : task(task)
            , arguments(std::move(arguments))
        {
            task.owner->bubble_up([this](auto const &w) -> bool {
                if (auto app = std::dynamic_pointer_cast<App>(w)) {
                    current_focus = app->focus;
                    return true;
                }
                return false;
            });
        }

        void execute() const
        {
            task.handler(task.owner, arguments);
            task.owner->bubble_up([this](auto const &w) -> bool {
                if (auto app = std::dynamic_pointer_cast<App>(w)) {
                    app->focus = current_focus;
                    return true;
                }
                return false;
            });
        }
    };

    using SyncMsg = std::variant<PendingTask, Notification>;

    std::deque<Job>             jobs { };
    std::deque<SyncMsg>         messages { };
    std::mutex                  messages_mutex { };
    std::mutex                  jobs_mutex { };
    std::condition_variable     cv { };
    std::map<std::string, Task> tasks;
    std::vector<Binding>        bindings { };
    std::thread                 job_handler_thread;

    AppBus()
    {
    }

    void start()
    {
        job_handler_thread = std::thread { AppBus::handler, this };
    }

    static void handler(AppBus *bus)
    {
        trace(AppBus, "Job handler started");
        while (true) {
            std::unique_lock lk(bus->jobs_mutex);

            // wait until job arrives in queue
            while (bus->jobs.empty()) {
                trace(AppBus, "Job handler waiting");
                bus->cv.wait(lk);
            }

            // after the wait, we own the lock
            auto job = bus->jobs.front();
            bus->jobs.pop_front();
            trace(AppBus, "Executing {}", typeid(job).name());
            job.execute();

            // manual unlocking is done before notifying, to avoid waking up
            // the waiting thread only to block again (see notify_one for details)
            lk.unlock();
            bus->cv.notify_one();
        }
    }

    void schedule(Job const &job)
    {
        auto lg = std::lock_guard(jobs_mutex);
        trace(AppBus, "Scheduling {}::{}", typeid(this).name(), typeid(job).name());
        jobs.emplace_back(job);
        cv.notify_all();
    }

    void submit(std::string_view const &task, JSONValue const &args)
    {
        trace(AppBus, "Submitting {}::{}({})", typeid(this).name(), task, args.serialize());
        auto lg = std::lock_guard(messages_mutex);
        auto name = std::string { task };
        if (!tasks.contains(name)) {
            return;
        }
        auto const &t = tasks.at(name);
        submit(PendingTask { t, args });
    }

    void submit(PendingTask const &pending)
    {
        messages.emplace_back(pending);
    }

    void notify(Notification const &notification)
    {
        trace(AppBus, "Notification: {}::{}", typeid(this).name(), typeid(notification).name());
        auto lg = std::lock_guard(messages_mutex);
        messages.emplace_back(notification);
    }

    void handle_one()
    {
        if (messages.empty()) {
            return;
        }
        trace(AppBus, "handle_one()");
        auto message = ({
            auto lg = std::lock_guard(messages_mutex);
            trace(AppBus, "handle_one() getting message");
            SyncMsg message = messages.front();
            messages.pop_front();
            message;
        });
        trace(AppBus, "handle_one() executing message");
        std::visit(
            [](auto const &msg) -> void {
                msg.execute();
            },
            message);
    }

    void handle_keys(std::set<int> pressed_keys)
    {
        KeyboardModifier modifier = modifier_current();
        for (auto key : pressed_keys) {
            for (auto const &[combo, task, arguments] : bindings) {
                if (combo.key == key && combo.modifier == modifier) {
                    submit(task, arguments);
                    return;
                }
            }
        }
    }

    Task &bind(std::string_view task, KeyCombo const &combo, JSONValue const &arguments = JSONValue { })
    {
        auto task_name = std::string { task };
        assert(tasks.contains(task_name));
        auto &t = tasks.at(task_name);
        return t.bind(combo, arguments);
    }

    Task &bind(Task &task, KeyCombo const &combo, JSONValue const &arguments = JSONValue { })
    {
        auto args = arguments;
        if (args.is_null()) {
            args = JSONValue::object();
            set(args, "key", combo.key);
            set(args, "modifier", combo.modifier);
        }
        bindings.emplace_back(combo, task.task, args);
        return task;
    }

    template<typename C>
    Task &add_task(std::string_view const &task, pWidget const &owner, std::function<void(std::shared_ptr<C> const &, JSONValue const &)> handler)
    {
        auto wrapper = [handler](pWidget const &target, JSONValue const &args) -> void {
            auto const &t = std::dynamic_pointer_cast<C>(target);
            assert(t != nullptr);
            handler(t, args);
        };

        auto task_name = std::string { task };
        tasks.try_emplace(task_name, *this, task_name, owner, wrapper);
        return tasks.at(task_name);
    }
};

enum class FontSize {
    XSmall,
    Small,
    Medium,
    Large,
    XLarge,
};

struct Widget : public std::enable_shared_from_this<Widget> {
private:
    struct Private {
    };

public:
    Rect<float> viewport { 0.0 };
    Rect<float> padding { ZeroPadding };
    Color       background { BLACK };
    SizePolicy  policy { SizePolicy::Stretch };
    float       policy_size { 0 };
    pWidget     parent { nullptr };
    pWidget     delegate { nullptr };
    pWidget     memo { nullptr };

    template<typename Pred>
    bool bubble_up(Pred const &predicate)
    {
        if (predicate(self())) {
            return true;
        }
        if (delegate) {
            if (delegate->bubble_up(predicate)) {
                return true;
            }
        }
        if (parent) {
            return parent->bubble_up(predicate);
        }
        return false;
    }

    virtual void initialize() { }
    virtual void draw() { }
    virtual void resize() { }
    virtual bool character(int) { return false; }
    virtual bool process_key(KeyboardModifier, int) { return false; }
    virtual void process_input() { }

    Vector2 render_sized_text_(float x, float y, rune_view const &text, Font font, float size, Color color) const;
    Vector2 render_sized_text_(float x, float y, rune_view const &text, FontSize font_size, Color color) const;
    void    render_text_bitmap_(float x, float y, std::string_view const &text, Color color) const;
    void    draw_rectangle_(float x, float y, float width, float height, Color color) const;
    void    draw_outline_(float x, float y, float width, float height, Color color) const;
    void    draw_line_(float x0, float y0, float x1, float y1, Color color) const;
    void    draw_circle_(float x, float y, float r, Color color) const;
    void    draw_hover_panel_(float x, float y, StringList const &text, Color bgcolor, Color textcolor, FontSize font_size = FontSize::Small) const;
    void    draw_rectangle_no_normalize_(float x, float y, float width, float height, Color color) const;
    void    draw_outline_no_normalize_(float x, float y, float width, float height, Color color) const;
    bool    mouse_in() const;

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    bool contains(Tx x, Ty y) const
    {
        auto x_f = static_cast<float>(x);
        auto y_f = static_cast<float>(y);
        return (x_f >= viewport.x && x_f <= viewport.x + viewport.width)
            && (y_f >= viewport.y && y_f <= viewport.y + viewport.height);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    Vector2 render_text(Tx x, Ty y, rune_view const &text, Font font, Color color) const
    {
        return render_sized_text_(x, y, text, font, 1.0, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    Vector2 render_text(Tx x, Ty y, std::string_view const &text, Font font, Color color) const
    {
        return render_sized_text_(x, y, MUST_EVAL(to_wstring(text)), font, 1.0, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    void render_codepoint(Tx x, Ty y, rune ch, Font font, Color color) const
    {
        if (!ch) {
            return;
        }
        Vector2 const pos { viewport.x + x, viewport.y + y };
        DrawTextCodepoint(font, ch, pos, static_cast<float>(font.baseSize), color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    Vector2 render_text(Tx x, Ty y, rune_view const &text, FontSize font_size, Color color) const
    {
        return render_sized_text_(x, y, text, font_size, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    Vector2 render_text(Tx x, Ty y, std::string_view const &text, FontSize font_size, Color color) const
    {
        return render_sized_text_(x, y, MUST_EVAL(to_wstring(text)), font_size, color);
    }

    template<typename Tx, typename Ty, typename Ts>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Ts, float>)
    Vector2 render_sized_text(Tx x, Ty y, rune_view const &text, Font font, Ts size, Color color) const
    {
        return render_sized_text_(x, y, text, font, size, color);
    }

    template<typename Tx, typename Ty, typename Ts>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Ts, float>)
    Vector2 render_sized_text(Tx x, Ty y, std::string_view const &text, Font font, Ts size, Color color) const
    {
        return render_sized_text_(x, y, MUST_EVAL(to_wstring(text)), font, size, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    void render_text_bitmap(Tx x, Ty y, std::string_view const &text, Color color) const
    {
        render_text_bitmap_(x, y, text, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    void render_texture(Tx x, Ty y, Texture2D texture, Color color = RAYWHITE) const
    {
        Vector2 const pos { viewport.x + x, viewport.y + y };
        DrawTextureV(texture, pos, color);
    }

    template<typename Tx, typename Ty, typename Tx2, typename Ty2, typename Tw, typename Th>
        requires(
            std::convertible_to<Tx, float>
            && std::convertible_to<Ty, float>
            && std::convertible_to<Tx2, float>
            && std::convertible_to<Ty2, float>
            && std::convertible_to<Tw, float>
            && std::convertible_to<Th, float>)
    void render_texture(Tx at_x, Ty at_y, Texture2D texture, Tx2 from_x, Ty2 from_y, Tw width, Th height, Color color = RAYWHITE) const
    {
        Rectangle const src { .x = from_x, .y = from_y, .width = width, .height = height };
        Vector2 const   dest { .x = viewport.x + at_x, .y = viewport.y + at_y };
        DrawTextureRec(texture, src, dest, color);
    }

    template<typename Tx, typename Ty, typename Tw, typename Th>
        requires(
            std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Tw, float> && std::convertible_to<Th, float>)
    void draw_rectangle(Tx x, Ty y, Tw width, Th height, Color color) const
    {
        draw_rectangle_(x, y, width, height, color);
    }

    template<typename Tx, typename Ty, typename Tw, typename Th>
        requires(
            std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Tw, float> && std::convertible_to<Th, float>)
    void draw_outline(Tx x, Ty y, Tw width, Th height, Color color) const
    {
        draw_outline_(x, y, width, height, color);
    }

    template<typename Tx0, typename Ty0, typename Tx1, typename Ty1>
        requires(
            std::convertible_to<Tx0, float> && std::convertible_to<Ty0, float> && std::convertible_to<Tx1, float> && std::convertible_to<Ty1, float>)
    void draw_line(Tx0 x0, Ty0 y0, Tx1 x1, Ty1 y1, Color color) const
    {
        draw_line_(x0, y0, x1, y1, color);
    }

    template<typename Tx, typename Ty, typename Tr>
        requires(
            std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Tr, float>)
    void draw_circle(Tx x, Ty y, Tr r, Color color) const
    {
        draw_circle_(x, y, r, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    void draw_hover_panel(Tx x, Ty y, StringList const &text, Color bgcolor, Color textcolor) const
    {
        draw_hover_panel_(x, y, text, bgcolor, textcolor);
    }

    template<typename Tx, typename Ty, typename Tw, typename Th>
        requires(
            std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Tw, float> && std::convertible_to<Th, float>)
    void draw_rectangle_no_normalize(Tx x, Ty y, Tw width, Th height, Color color) const
    {
        draw_rectangle_no_normalize_(x, y, width, height, color);
    }

    template<typename Tx, typename Ty, typename Tw, typename Th>
        requires(
            std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Tw, float> && std::convertible_to<Th, float>)
    void draw_outline_no_normalize(Tx x, Ty y, Tw width, Th height, Color color) const
    {
        draw_outline_no_normalize_(x, y, width, height, color);
    }

    template<typename C, typename... Args>
        requires(std::derived_from<C, Widget> && !std::derived_from<C, struct App>)
    static std::shared_ptr<C> make(Args &&...args)
    {
        auto ret = std::make_shared<C>(std::forward<Args>(args)...);
        ret->initialize();
        return ret;
    }

    template<typename A, typename... Args>
        requires std::derived_from<A, Widget>
    static std::shared_ptr<A> make(Args &&...args)
    {
        auto ret = std::make_shared<A>(std::forward<Args>(args)...);
        return ret;
    }

    template<class C = Widget>
        requires std::derived_from<C, Widget>
    std::shared_ptr<C> self()
    {
        return std::dynamic_pointer_cast<C>(shared_from_this());
    }

    template<typename Left, typename Top, typename Width, typename Height>
        requires std::assignable_from<float &, Left> && std::assignable_from<float &, Top> && std::assignable_from<float &, Width> && std::assignable_from<float &, Height>
    Rectangle normalize(Left left, Top top, Width width, Height height) const
    {
        auto l = static_cast<float>(left);
        auto t = static_cast<float>(top);
        auto w = static_cast<float>(width);
        auto h = static_cast<float>(height);
        if (l < 0) {
            l = viewport.width + l;
        }
        if (t < 0) {
            t = viewport.height + t;
        }
        if (w <= 0) {
            w = viewport.width + 2 * w;
        }
        if (h <= 0) {
            h = viewport.height + 2 * h;
        }
        l = clamp(l, 0, viewport.width);
        t = clamp(t, 0, viewport.height);
        w = clamp(w, 0, viewport.width - 1);
        h = clamp(h, 0, viewport.height - 1);
        return (Rectangle) { .x = viewport.x + l, .y = viewport.y + t, .width = w, .height = h };
    }

    [[nodiscard]] bool                    contains(Vector2 world_coordinates) const;
    [[nodiscard]] std::optional<Vec<int>> coordinates(Vector2 world_coordinates) const;

    Widget(Widget &&) = delete;
    Widget() = delete;
    Widget(pWidget parent)
        : parent(std::move(parent))
    {
    }
    Widget(pWidget parent, SizePolicy policy, float policy_size);
    virtual ~Widget() = default;
};

struct Layout : public Widget {
    ContainerOrientation orientation;
    std::vector<pWidget> widgets { };

    Layout() = delete;
    Layout(pWidget const &parent, ContainerOrientation orientation = ContainerOrientation::Vertical, SizePolicy policy = SizePolicy::Stretch, float policy_size = 0.0)
        : Widget(parent, policy, policy_size)
        , orientation(orientation)
    {
    }

    void draw() override;
    void resize() override;
    void process_input() override;

    virtual void on_resize() { }
    virtual void after_resize() { }
    virtual void on_draw() { }
    virtual void after_draw() { }
    virtual void on_process_input() { }
    virtual void after_process_input() { }

    void append(pWidget widget)
    {
        widgets.push_back(widget);
    }

    void insert(size_t ix, pWidget widget)
    {
        widgets.insert(widgets.begin() + ix, widget);
    }

    template<class Cls, typename... Args>
        requires std::derived_from<Cls, Widget>
    std::shared_ptr<Cls> add_widget(Args &&...args)
    {
        auto widget = Widget::make<Cls>(self(), std::forward<Args>(args)...);
        widgets.push_back(widget);
        return std::dynamic_pointer_cast<Cls>(widget);
    }

    template<class Cls, typename... Args>
        requires std::derived_from<Cls, Widget>
    std::shared_ptr<Cls> insert_widget(size_t ix, Args &&...args)
    {
        auto widget = Widget::make<Cls>(std::forward<Args>(args)...);
        widget->parent = self();
        widgets.insert(widgets.begin() + static_cast<ptrdiff_t>(ix), widget);
        return std::dynamic_pointer_cast<Cls>(widget);
    }

    template<typename Predicate>
    pWidget find_by_predicate(Predicate p)
    {
        auto s = self();
        if (p(s)) {
            return s;
        }
        for (auto &w : widgets) {
            if (auto layout = std::dynamic_pointer_cast<Layout>(w); layout) {
                if (auto ret = layout->find_by_predicate(p); ret) {
                    return ret;
                }
            } else {
                if (p(w)) {
                    return w;
                }
            }
        }
        return nullptr;
    }

    template<typename C>
    std::shared_ptr<C> find_by_class()
    {
        auto p = [](auto &w) {
            return std::dynamic_pointer_cast<C>(w) != nullptr;
        };
        return std::dynamic_pointer_cast<C>(find_by_predicate(p));
    }

    template<typename Callback>
    void traverse(Callback fnc)
    {
        fnc(self());
        for (auto &w : widgets) {
            if (auto layout = dynamic_pointer_cast<Layout>(w); layout) {
                layout->traverse(fnc);
            } else {
                fnc(w);
            }
        }
        fnc(nullptr);
    }

    void dump();
};

struct WidgetStack : public Widget {
    std::vector<pWidget> widgets;

    WidgetStack() = delete;
    WidgetStack(pWidget const &parent, SizePolicy policy = SizePolicy::Stretch, float policy_size = 0.0)
        : Widget(parent, policy, policy_size)
    {
    }

    void initialize() override
    {
    }

    void draw() override
    {
        if (widgets.empty()) {
            return;
        }
        auto &current { widgets.front() };
        current->draw();
    }

    void resize() override
    {
        for (auto &w : widgets) {
            w->viewport = viewport;
            w->resize();
        }
    }

    void process_input() override
    {
        if (widgets.empty()) {
            return;
        }
        auto &current { widgets.front() };
        current->process_input();
    }

    template<class W, typename... Args>
        requires std::derived_from<W, Widget>
    std::shared_ptr<W> add_widget(Args &&...args)
    {
        auto widget = Widget::make<W>(self(), std::forward<Args>(args)...);
        widgets.push_back(widget);
        return std::dynamic_pointer_cast<W>(widget);
    }

    template<class W>
    std::shared_ptr<W> activate()
    {
        if (widgets.empty()) {
            return nullptr;
        }
        auto current { widgets.front() };
        for (size_t ix = 0; ix < widgets.size(); ++ix) {
            auto const &w = widgets[ix];
            if (auto as_W = std::dynamic_pointer_cast<W>(w); as_W) {
                if (ix > 0) {
                    widgets[0] = w;
                    widgets[ix] = current;
                }
                as_W->resize();
                return as_W;
            }
        }
        return nullptr;
    }
};

struct Spacer : public Widget {
    Spacer(pWidget const &parent);
    Spacer(pWidget const &parent, SizePolicy policy, float policy_size);

    void resize() override
    {
        background = parent->background;
    }
};

struct Label : public Widget {
    Color       color;
    FontSize    size { FontSize::Medium };
    std::string text;

    Label(pWidget const &parent, std::string_view const &text, Color color = RAYWHITE);
    void draw() override;
};

template<>
inline char const *value_to_string(ContainerOrientation orientation)
{
    switch (orientation) {
#undef S
#define S(O, V)                       \
    case ST::ContainerOrientation::O: \
        return #O;
        CONTAINERORIENTATIONS(S)
#undef S
    default:
        UNREACHABLE();
    }
}

}

inline std::ostream &
operator<<(std::ostream &os, ST::ContainerOrientation value)
{
    os << ST::value_to_string<ST::ContainerOrientation>(value);
    return os;
}

template<>
struct std::formatter<ST::ContainerOrientation> : std::formatter<std::string> {
    template<class FmtContext>
    typename FmtContext::iterator format(ST::ContainerOrientation const &value, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << ST::value_to_string<ST::ContainerOrientation>(value);
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
