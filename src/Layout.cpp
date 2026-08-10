/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <print>

#include <App.h>

namespace ST {

char const *ContainerOrientation_name(ContainerOrientation orientation)
{
    switch (orientation) {
#undef S
#define S(o)                      \
    case ContainerOrientation::o: \
        return #o;
        CONTAINERORIENTATIONS(S)
#undef S
    default:
        UNREACHABLE();
    }
}

void Layout::resize()
{
    trace(LAYOUT, "Resizing layout {} {}", typeid(*this).name(), viewport.to_string());
    on_resize();
    float allocated = 0.0f;
    int   stretch_count = 0;
    int   fixed_coord = (orientation == ContainerOrientation::Vertical) ? 0 : 1;
    int   var_coord = 1 - fixed_coord;
    float total = viewport.size.coords[var_coord];
    float fixed_size = viewport.size.coords[fixed_coord];
    float fixed_pos = viewport.position.coords[fixed_coord];
    float var_offset = viewport.position.coords[var_coord];

    trace(LAYOUT, "Total available {}, orientation {}", total, ContainerOrientation_name(orientation));
    // printf("Fixed %s: %f, fixed %s position: %f\n",
    //     (layout->orientation == CO_VERTICAL) ? "width" : "height",
    //     fixed_size,
    //     (layout->orientation == CO_VERTICAL) ? "x" : "y",
    //     fixed_pos);
    for (auto &w : widgets) {
        w->viewport.size.coords[fixed_coord] = fixed_size - w->padding.coords[fixed_coord] - w->padding.coords[fixed_coord + 2];
        w->viewport.position.coords[fixed_coord] = fixed_pos + w->padding.coords[fixed_coord];
        float sz = 0;
        trace(LAYOUT, "Component widget {} has policy {}", typeid(w).name(), SizePolicy_name(w->policy));
        switch (w->policy) {
        case SizePolicy::Absolute:
            sz = w->policy_size;
            break;
        case SizePolicy::Relative: {
            sz = (total * w->policy_size) / 100.0f;
        } break;
        case SizePolicy::Characters: {
            sz = ceilf(1.2 * w->policy_size * ((orientation == ContainerOrientation::Vertical) ? App::the()->char_size.y : App::the()->char_size.x));
        } break;
        case SizePolicy::Calculated: {
            fatal("SP_CALCULATED not yet supported");
        };
        case SizePolicy::Stretch: {
            sz = -1.0f;
            stretch_count++;
        } break;
        }
        assert(sz != 0);
        w->viewport.size.coords[var_coord] = sz - w->padding.coords[var_coord] - w->padding.coords[var_coord + 2];
        if (sz > 0) {
            allocated += sz;
            trace(LAYOUT, "Allocating {}, now allocated {}", sz, allocated);
        }
    }

    if (stretch_count) {
        trace(LAYOUT, "Stretch count {}", stretch_count);
        assert(total > allocated);
        float stretch = floorf((total - allocated) / (float) stretch_count);
        for (auto &w : widgets) {
            if (w->policy == SizePolicy::Stretch) {
                trace(LAYOUT, "Allocating {} to stretchable {}", stretch, typeid(w).name());
                w->viewport.size.coords[var_coord] = stretch - w->padding.coords[var_coord] - w->padding.coords[var_coord + 2];
            }
        }
    }

    for (auto &w : widgets) {
        w->viewport.position.coords[var_coord] = var_offset + w->padding.coords[var_coord];
        var_offset += w->viewport.size.coords[var_coord] + w->padding.coords[var_coord] + w->padding.coords[var_coord + 2];
        trace(LAYOUT, "Resizing {} to {}", typeid(w).name(), w->viewport.to_string());
        w->resize();
    }
    after_resize();
}

void Layout::draw()
{
    on_draw();
    for (auto &w : widgets) {
        if (w->viewport.width > 0.0f && w->viewport.height > 0.0f) {
            DrawRectangle(w->viewport.x - w->padding.left, w->viewport.y - w->padding.top,
                w->viewport.width + w->padding.left + w->padding.right,
                w->viewport.height + w->padding.top + w->padding.bottom,
                w->background);
            w->draw();
        }
    }
    after_draw();
}

void Layout::process_input()
{
    on_process_input();
    for (auto &w : widgets) {
        if (w->viewport.width > 0.0f && w->viewport.height > 0.0f) {
            w->process_input();
        }
    }
    after_process_input();
}

void Layout::dump()
{
    int  dump_indent = 0;
    auto dump_fnc = [&dump_indent](pWidget const &w) {
        if (w == nullptr) {
            dump_indent -= 2;
            return;
        }
        if (auto layout = std::dynamic_pointer_cast<Layout>(w); layout) {
            std::println("{:{}s}+ | {:} {:} {}", "", dump_indent, layout->widgets.size(), w->viewport.to_string(), layout->orientation);
            dump_indent += 2;
            return;
        }
        auto &obj = *w;
        std::println("{:{}s}+-> {:} {:}", "", dump_indent, typeid(obj).name(), w->viewport.to_string());
    };
    std::string_view s;
    traverse(dump_fnc);
}

}
