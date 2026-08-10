/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <sstream>

#include <raylib.h>

#include <JSON.h>
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

#define CONTAINERORIENTATIONS(S) \
    S(Horizontal)                \
    S(Vertical)

enum class ContainerOrientation {
#undef S
#define S(o) o,
    CONTAINERORIENTATIONS(S)
#undef S
};

enum class SizePolicy {
    Absolute = 0,
    Relative,
    Characters,
    Calculated,
    Stretch,
};

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
    constexpr Rect(Rect const &) = default;
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

    constexpr std::string to_string()
    {
        return std::format("{:.1}x{:.1}@+{:.1},+{:.1}", width, height, x, y);
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
};

constexpr auto ZeroPadding = Rect<float> { 0.0 };
constexpr auto DefaultPadding = Rect<float> { 5.0 };

using pWidget = std::shared_ptr<struct Widget>;

template<typename T>
concept Application = std::derived_from<T, struct App>;

class Widget : public std::enable_shared_from_this<Widget> {
private:
    struct Private {
    };

public:
    using Handler = std::function<void(pWidget const &, JSONValue const &)>;
    struct WidgetCommand {
        std::string           command;
        pWidget               owner;
        Handler               handler;
        std::vector<KeyCombo> bindings { };

        WidgetCommand(std::string name, pWidget owner, Handler handler);
        WidgetCommand(WidgetCommand const &) = default;
        void           execute(JSONValue const &args) const;
        WidgetCommand &bind(KeyCombo combo);

        template<typename... Args>
        WidgetCommand &bind(KeyCombo combo, Args... args)
        {
            bindings.push_back(combo);
            return bind(std::forward<Args>(args)...);
        }

        auto operator<=>(WidgetCommand const &other) const
        {
            return command <=> other.command;
        }
    };

    struct PendingCommand {
        WidgetCommand const &command;
        pWidget              current_focus;
        JSONValue            arguments;

        PendingCommand(WidgetCommand const &command, JSONValue arguments);
        void execute(JSONValue const &args) const;
    };

    Rect<float>                          viewport { 0.0 };
    Rect<float>                          padding { ZeroPadding };
    Color                                background { BLACK };
    SizePolicy                           policy { SizePolicy::Absolute };
    float                                policy_size { 0 };
    pWidget                              parent { nullptr };
    pWidget                              delegate { nullptr };
    pWidget                              memo { nullptr };
    std::map<std::string, WidgetCommand> commands;
    static std::deque<PendingCommand>    pending_commands;
    static std::mutex                    commands_mutex;

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

    void render_sized_text_(float x, float y, rune_view const &text, Font font, float size, Color color) const;
    void render_text_bitmap_(float x, float y, std::string_view const &text, Color color) const;
    void draw_rectangle_(float x, float y, float width, float height, Color color) const;
    void draw_outline_(float x, float y, float width, float height, Color color) const;
    void draw_line_(float x0, float y0, float x1, float y1, Color color) const;
    void draw_hover_panel_(float x, float y, StringList const &text, Color bgcolor, Color textcolor) const;
    void draw_rectangle_no_normalize_(float x, float y, float width, float height, Color color) const;
    void draw_outline_no_normalize_(float x, float y, float width, float height, Color color) const;

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    void render_text(Tx x, Ty y, rune_view const &text, Font font, Color color) const
    {
        render_sized_text_(x, y, text, font, 1.0, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    void render_text(Tx x, Ty y, std::string_view const &text, Font font, Color color) const
    {
        render_sized_text_(x, y, MUST_EVAL(to_wstring(text)), font, 1.0, color);
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

    template<typename Tx, typename Ty, typename Ts>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Ts, float>)
    void render_sized_text(Tx x, Ty y, rune_view const &text, Font font, Ts size, Color color) const
    {
        render_sized_text_(x, y, text, font, size, color);
    }

    template<typename Tx, typename Ty, typename Ts>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float> && std::convertible_to<Ts, float>)
    void render_sized_text(Tx x, Ty y, std::string_view const &text, Font font, Ts size, Color color) const
    {
        render_sized_text_(x, y, MUST_EVAL(to_wstring(text)), font, size, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    void render_text_bitmap(Tx x, Ty y, std::string_view const &text, Color color) const
    {
        render_text_bitmap_(x, y, text, color);
    }

    template<typename Tx, typename Ty>
        requires(std::convertible_to<Tx, float> && std::convertible_to<Ty, float>)
    void render_texture(Tx x, Ty y, Texture2D texture, Color color) const
    {
        Vector2 const pos { viewport.x + x, viewport.y + y };
        DrawTextureV(texture, pos, color);
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

    template<Application A, typename... Args>
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

    WidgetCommand &bind(std::string_view const &command, KeyCombo combo)
    {
        auto command_name = std::string { command };
        assert(commands.contains(command_name));
        auto &cmd = commands.at(command_name);
        cmd.bind(combo);
        return cmd;
    }

    template<typename... Args>
    WidgetCommand &bind(std::string_view const &command, KeyCombo combo, Args... args)
    {
        auto &cmd = bind(command, combo);
        cmd.bind(std::forward<Args>(args)...);
        return cmd;
    }

    template<typename C>
    WidgetCommand &add_command(std::string_view const &command, std::function<void(std::shared_ptr<C> const &, JSONValue const &)> handler)
    {
        auto wrapper = [handler](pWidget const &target, JSONValue const &args) -> void {
            auto const &t = std::dynamic_pointer_cast<C>(target);
            assert(t != nullptr);
            handler(t, args);
        };

        auto command_name = std::string { command };
        commands.try_emplace(command_name, command_name, self(), wrapper);
        return commands.at(command_name);
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

    void submit(std::string_view const &command, JSONValue const &args)
    {
        auto lg = std::lock_guard(commands_mutex);
        auto name = std::string { command };
        trace(CMD, "Submitting {}::{}({})", typeid(this).name(), name, args.serialize());
        if (!commands.contains(name)) {
            return;
        }
        auto const &cmd = commands.at(name);
        pending_commands.emplace_back(cmd, args);
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
    Layout(pWidget const &parent, ContainerOrientation orientation = ContainerOrientation::Vertical)
        : Widget(parent)
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
    std::string text;

    Label(pWidget const &parent, std::string_view const &text, Color color);
    void draw() override;
};

extern char const      *SizePolicy_name(SizePolicy policy);
extern char const      *ContainerOrientation_name(ContainerOrientation orientation);
extern bool             is_modifier_down(KeyboardModifier modifier);
extern std::string      modifier_string(KeyboardModifier modifiers);
extern KeyboardModifier modifier_current();

}

template<>
struct std::formatter<ST::ContainerOrientation, char> {
    template<class ParseContext>
    constexpr typename ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("Invalid format args for ContainerOrientation.");
        }
        return it;
    }

    template<class FmtContext>
    typename FmtContext::iterator format(ST::ContainerOrientation orientation, FmtContext &ctx) const
    {
        std::ostringstream out;
        switch (orientation) {
#undef S
#define S(O)                          \
    case ST::ContainerOrientation::O: \
        out << #O;                    \
        break;
            CONTAINERORIENTATIONS(S)
#undef S
        default:
            UNREACHABLE();
        }
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
