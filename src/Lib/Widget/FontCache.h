/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Fonts loaded at the exact pixel size they're drawn at, since a font
 * scaled up from a smaller size is blurred. Each file at each size is
 * loaded once, with ASCII and the extra characters asked for: all of
 * Unicode would make a huge glyph atlas at a large size.
 */

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <raylib.h>

namespace ST {

struct FontCache {
    FontCache() = default;
    ~FontCache();
    FontCache(FontCache const &) = delete;
    FontCache &operator=(FontCache const &) = delete;

    // The font at this size, loaded if it isn't yet. Nothing if raylib
    // can't load it.
    std::optional<Font> get(std::filesystem::path const &file, int size);

    // The characters besides ASCII that the fonts have. Different ones
    // unload the fonts, to be loaded again with them.
    void set_codepoints(std::set<int> extra);

    void clear();

private:
    std::map<std::pair<std::string, int>, Font> m_fonts { };
    std::set<int>                               m_extra { };
};

}
