/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <vector>

#include <Logging.h>
#include <Utf8.h>

#include <FontCache.h>

namespace ST {

std::optional<Font> load_font(std::filesystem::path const &file, int size, std::span<int const> codepoints)
{
    SetTraceLogLevel(LOG_ERROR);
    // raylib takes the characters as a pointer that isn't const.
    auto const font = LoadFontEx(file.string().c_str(), size, const_cast<int *>(codepoints.data()), static_cast<int>(codepoints.size()));
    SetTraceLogLevel(LOG_WARNING);
    // raylib gives its default font when it can't load one, which mustn't
    // be unloaded with the others.
    if (font.baseSize == 0 || font.texture.id == 0 || font.texture.id == GetFontDefault().texture.id) {
        warning(App, "Can't load font `{}` at size {}", file.string(), size);
        return { };
    }
    return font;
}

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
    if (m_failed.contains(key)) {
        return { };
    }
    std::vector<int> codepoints;
    for (auto ch = 32; ch < 127; ++ch) {
        codepoints.push_back(ch);
    }
    codepoints.insert(codepoints.end(), m_extra.begin(), m_extra.end());
    trace(App, "Loading font '{}', size {}, {} characters", key.first, size, codepoints.size());
    auto const font = load_font(file, size, codepoints);
    if (!font) {
        m_failed.insert(key);
        return { };
    }
    m_fonts.emplace(key, *font);
    return font;
}

void FontCache::set_codepoints(std::set<int> extra)
{
    if (extra != m_extra) {
        clear();
        m_extra = std::move(extra);
    }
}

bool FontCache::add_codepoints(std::string_view text)
{
    auto extra = m_extra;
    for (auto const ch : to_utf32(text).value_or(std::u32string { })) {
        if (ch > 126) {
            extra.insert(static_cast<int>(ch));
        }
    }
    if (extra.size() == m_extra.size()) {
        return false;
    }
    set_codepoints(std::move(extra));
    return true;
}

void FontCache::clear()
{
    for (auto const &[key, font] : m_fonts) {
        UnloadFont(font);
    }
    m_fonts.clear();
    m_failed.clear();
}

}
