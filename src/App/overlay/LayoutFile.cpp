/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <format>
#include <map>
#include <system_error>

#include <IO.h>
#include <JSON.h>

#include <overlay/LayoutFile.h>

namespace ST {

LayoutFile::LayoutFile(fs::path the_path)
    : path(std::move(the_path))
{
}

bool LayoutFile::reload_if_changed()
{
    std::error_code ec { };
    auto const      time = fs::last_write_time(path, ec);
    auto const      now = ec ? std::optional<fs::file_time_type> { } : std::optional { time };
    if (loaded && now == changed) {
        return false;
    }
    loaded = true;
    changed = now;
    auto const name = path.filename().string();
    auto const contents = read_file_by_name(path.string());
    if (!contents) {
        layout = std::unexpected(std::format("{}: {}", name, contents.error().to_string()));
        return true;
    }
    auto const json = JSONValue::deserialize(*contents);
    if (!json) {
        layout = std::unexpected(std::format("{}: {}", name, json.error().to_string()));
        return true;
    }
    auto decoded = decode_layout(*json);
    if (!decoded) {
        layout = std::unexpected(std::format("{}: {}", name, decoded.error().to_string()));
        return true;
    }
    layout = std::move(*decoded);
    return true;
}

std::expected<std::map<std::string, fs::path>, LayoutError> layout_fonts(OverlayLayout const &layout, FontFinder const &find)
{
    std::map<std::string, fs::path> ret;
    for (auto const &element : layout.elements) {
        std::vector<std::string> names { std::visit([](auto const &e) { return e.style.font; }, element) };
        if (auto const *value = std::get_if<ValueElement>(&element)) {
            names.push_back(value->unit_style.font);
        }
        for (auto const &name : names) {
            if (ret.contains(name)) {
                continue;
            }
            auto const path = find(name);
            if (!path) {
                return std::unexpected(LayoutError {
                    .element = element_number(element),
                    .message = std::format("the font `{}` isn't in any of the font directories", name),
                });
            }
            ret[name] = *path;
        }
    }
    return ret;
}

std::vector<LayoutEntry> overlay_layouts(fs::path const &builtin_dir, fs::path const &user_dir)
{
    std::map<std::string, fs::path> found;
    for (auto const &dir : { builtin_dir, user_dir }) {
        std::error_code ec { };
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            auto const file_name = it->path().filename().string();
            if (!file_name.ends_with(LayoutSuffix) || file_name.length() == LayoutSuffix.length()) {
                continue;
            }
            // The user's directory comes second, so its layouts win.
            found[file_name.substr(0, file_name.length() - LayoutSuffix.length())] = it->path();
        }
    }
    std::vector<LayoutEntry> ret;
    for (auto const &[name, path] : found) {
        ret.push_back(LayoutEntry { .name = name, .path = path });
    }
    return ret;
}

std::optional<std::string> read_chosen_layout(fs::path const &state_file)
{
    std::error_code ec { };
    if (!fs::exists(state_file, ec)) {
        return { };
    }
    auto const contents = read_file_by_name(state_file.string());
    if (!contents) {
        return { };
    }
    auto const json = JSONValue::deserialize(*contents);
    if (!json) {
        return { };
    }
    auto const name = json->try_get<std::string>("layout");
    if (!name || name->empty()) {
        return { };
    }
    return *name;
}

std::expected<void, LibCError> save_chosen_layout(fs::path const &state_file, std::string_view name)
{
    TRY(ensure_directory(state_file.parent_path()));
    auto json = JSONValue::object();
    json.set("layout", JSONValue { std::string { name } });
    return write_file_atomically(state_file, json.serialize());
}

}
