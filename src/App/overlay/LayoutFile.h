/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Overlay layout files: loading one, and again when it changes, which
 * layouts there are, and which one the user picked.
 */

#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <Error.h>

#include <overlay/Layout.h>

namespace ST {

namespace fs = std::filesystem;

// Layout files are called `<name>.overlay.json`.
constexpr std::string_view LayoutSuffix { ".overlay.json" };

struct LayoutFile {
    fs::path                                  path { };
    bool                                      loaded { false };
    std::optional<fs::file_time_type>         changed { }; // When it was, as last loaded; none if it wasn't there
    std::expected<OverlayLayout, std::string> layout { std::unexpected(std::string { "not loaded" }) };

    explicit LayoutFile(fs::path the_path);

    // Loads the file if it changed since it was last loaded, or was never
    // loaded. Returns whether it did. A file that can't be read, isn't JSON,
    // or isn't a layout gives an error, starting with the file's name.
    bool reload_if_changed();
};

using FontFinder = std::function<std::optional<fs::path>(std::string_view name)>;

// The font files the layout's elements use, by the layout's names for them,
// as `find` finds them. A font it doesn't find is an error, with the first
// element that uses it.
std::expected<std::map<std::string, fs::path>, LayoutError> layout_fonts(OverlayLayout const &layout, FontFinder const &find);

// Where a layout's picture is: `src` as the layout says it, and the
// folder the layout is in.
using ImageFinder = std::function<std::optional<fs::path>(std::string_view src, fs::path const &layout_dir)>;

// An absolute `src` as it is; else next to the layout, or in the built-in
// layouts' folder, as `exists` says. None if neither has it.
std::optional<fs::path> find_image(std::string_view src, fs::path const &layout_dir, fs::path const &builtin_dir,
    std::function<bool(fs::path const &)> const &exists);

// The picture files of the layout's images, by their `src`, as `find`
// finds them. One it doesn't find is an error, with the first element
// that shows it.
std::expected<std::map<std::string, fs::path>, LayoutError> layout_images(OverlayLayout const &layout, fs::path const &layout_dir, ImageFinder const &find);

struct LayoutEntry {
    std::string name { };
    fs::path    path { };
};

// The layouts in both directories, by name, sorted. A user's layout hides a
// built-in one of the same name. A directory that isn't there has none.
std::vector<LayoutEntry> overlay_layouts(fs::path const &builtin_dir, fs::path const &user_dir);

// The name of the layout the user picked last, kept in `state_file`.
std::optional<std::string>     read_chosen_layout(fs::path const &state_file);
std::expected<void, LibCError> save_chosen_layout(fs::path const &state_file, std::string_view name);

}
