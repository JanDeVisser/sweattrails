/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <vector>

#include <Logging.h>

#include <FontCache.h>

namespace ST {

FontCache::~FontCache()
{
    clear();
}

std::optional<Font> FontCache::get(std::filesystem::path const &file, int size)
{
    auto const key = std::pair { file.string(), size };
    if (auto const it = m_fonts.find(key); it != m_fonts.end()) {
        return it->second;
    }
    std::vector<int> codepoints;
    for (auto ch = 32; ch < 127; ++ch) {
        codepoints.push_back(ch);
    }
    codepoints.insert(codepoints.end(), m_extra.begin(), m_extra.end());
    trace(App, "Loading font '{}', size {}, {} characters", key.first, size, codepoints.size());
    auto const font = LoadFontEx(key.first.c_str(), size, codepoints.data(), static_cast<int>(codepoints.size()));
    // raylib gives its default font when it can't load one, which mustn't
    // be unloaded with the others.
    if (font.baseSize == 0 || font.texture.id == 0 || font.texture.id == GetFontDefault().texture.id) {
        return { };
    }
    m_fonts.emplace(key, font);
    return font;
}

void FontCache::set_codepoints(std::set<int> extra)
{
    if (extra != m_extra) {
        clear();
        m_extra = std::move(extra);
    }
}

void FontCache::clear()
{
    for (auto const &[key, font] : m_fonts) {
        UnloadFont(font);
    }
    m_fonts.clear();
}

}
