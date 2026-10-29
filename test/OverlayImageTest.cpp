/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

#include <raylib.h>

#include <JSON.h>

#include <overlay/Picture.h>
#include <overlay/Renderer.h>

#include <TempDir.h>
#include <TestApp.h>

// The pictures an overlay's image shows, as the renderer reads them:
// decoded with raylib's LoadImage(), which needs no window. Only making a
// texture of one does.

using namespace ST;

namespace {

namespace fs = std::filesystem;

fs::path const Icons = fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / "icons";

void put(fs::path const &path, std::string_view bytes)
{
    std::ofstream out { path, std::ios::binary };
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string rgba(Color c)
{
    return std::format("{} {} {} {}", c.r, c.g, c.b, c.a);
}

Color pixel(Image const &image, int x, int y)
{
    return static_cast<Color const *>(image.data)[y * image.width + x];
}

// The built-in icons are 96 px, and white where they show, so that a
// tint gives them its color.
void test_icons()
{
    for (auto const *name : { "heart.png", "bolt.png", "speedometer.png", "cadence.png", "mountain.png" }) {
        auto image = LoadImage((Icons / name).string().c_str());
        CHECK(image.data != nullptr);
        if (image.data == nullptr) {
            continue;
        }
        CHECK_EQ(image.width, 96);
        CHECK_EQ(image.height, 96);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        auto const *pixels = static_cast<Color const *>(image.data);
        auto        shown = 0;
        auto        not_white = 0;
        for (auto ix = 0; ix < image.width * image.height; ++ix) {
            auto const p = pixels[ix];
            if (p.a > 0) {
                ++shown;
                if (p.r != 255 || p.g != 255 || p.b != 255) {
                    ++not_white;
                }
            }
        }
        CHECK(shown > 0);
        CHECK_EQ(std::format("{} {}", name, not_white), std::format("{} 0", name));
        UnloadImage(image);
    }
}

// A file that's there but isn't a picture raylib reads: no data, which
// the renderer reports.
void test_not_a_picture()
{
    Test::TempDir dir { "overlay-image-broken" };
    put(dir.path / "logo.svg", R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"/>)");
    put(dir.path / "logo.png", "not a PNG");
    put(dir.path / "empty.png", "");
    for (auto const *name : { "logo.svg", "logo.png", "empty.png", "missing.png" }) {
        auto image = LoadImage((dir.path / name).string().c_str());
        CHECK_EQ(std::format("{} {}", name, image.data == nullptr), std::format("{} true", name));
        UnloadImage(image);
    }
}

// A transparent pixel gets its shown neighbours' color, and keeps its
// alpha; the ones further out get the color of the ring before them.
void test_bleed_colors()
{
    auto image = GenImageColor(5, 1, BLANK);
    ImageDrawPixel(&image, 0, 0, Color { 255, 255, 255, 255 });
    ImageDrawPixel(&image, 4, 0, Color { 255, 0, 0, 128 });
    bleed_colors(image);
    CHECK_EQ(rgba(pixel(image, 0, 0)), "255 255 255 255");
    CHECK_EQ(rgba(pixel(image, 1, 0)), "255 255 255 0");
    // Between the two, as far from both: their average.
    CHECK_EQ(rgba(pixel(image, 2, 0)), "255 127 127 0");
    CHECK_EQ(rgba(pixel(image, 3, 0)), "255 0 0 0");
    CHECK_EQ(rgba(pixel(image, 4, 0)), "255 0 0 128");
    UnloadImage(image);

    // Diagonals count, and a ring is coloured from the rings before it.
    auto square = GenImageColor(3, 3, BLANK);
    ImageDrawPixel(&square, 0, 0, Color { 0, 0, 255, 255 });
    bleed_colors(square);
    CHECK_EQ(rgba(pixel(square, 1, 1)), "0 0 255 0");
    CHECK_EQ(rgba(pixel(square, 2, 2)), "0 0 255 0");
    UnloadImage(square);

    // Nothing shown: nothing to bleed.
    auto blank = GenImageColor(2, 2, BLANK);
    bleed_colors(blank);
    CHECK_EQ(rgba(pixel(blank, 1, 1)), "0 0 0 0");
    UnloadImage(blank);
}

// The icons' transparent pixels were black: decoded, all of them are
// white, so that no edge darkens in the mipmaps.
void test_decoded_icon()
{
    auto const image = decode_picture(Icons / "heart.png");
    CHECK(image.has_value());
    if (!image) {
        return;
    }
    auto not_white = 0;
    for (auto y = 0; y < image->height; ++y) {
        for (auto x = 0; x < image->width; ++x) {
            auto const p = pixel(*image, x, y);
            not_white += (p.r != 255 || p.g != 255 || p.b != 255) ? 1 : 0;
        }
    }
    CHECK_EQ(not_white, 0);
    UnloadImage(*image);
    CHECK(!decode_picture("/nowhere/logo.png").has_value());
}

OverlayLayout layout_of(std::string_view elements)
{
    auto const json = JSONValue::deserialize(std::format(R"({{ "elements": [ {} ] }})", elements));
    auto const layout = json ? decode_layout(*json) : std::unexpected(LayoutError { .message = "not JSON" });
    CHECK_EQ(layout ? std::string { "ok" } : layout.error().to_string(), "ok");
    return layout.value_or(OverlayLayout { });
}

std::optional<fs::path> any_font(std::string_view)
{
    return fs::path { "/nowhere/font.ttf" };
}

// A picture that's there but doesn't decode is an error as soon as the
// layout is set, also when it only shows later in the video, so that a
// render doesn't start.
void test_picture_that_doesnt_decode()
{
    Test::TempDir dir { "overlay-image-renderer" };
    put(dir.path / "broken.png", "not a PNG");
    auto const      next_to_layout = [](std::string_view src, fs::path const &layout_dir) { return std::optional { layout_dir / src }; };
    OverlayRenderer renderer { any_font, next_to_layout };
    renderer.set_layout(layout_of(R"({ "type": "text", "text": "a" },
        { "type": "image", "src": "broken.png", "visible": { "from": 60 } })"),
        dir.path / "mine.overlay.json");
    CHECK_EQ(renderer.error().value_or("none"),
        std::format("mine.overlay.json: element 2: the picture `broken.png` can't be loaded from `{}`", (dir.path / "broken.png").string()));

    renderer.set_layout(layout_of(R"({ "type": "image", "src": "icons/heart.png" })"), Icons.parent_path() / "builtin.overlay.json");
    CHECK_EQ(renderer.error().value_or("none"), "none");
}

// One in a group's child, also in a hidden group, is named by the child's
// number.
void test_picture_in_group_that_doesnt_decode()
{
    Test::TempDir dir { "overlay-image-group" };
    put(dir.path / "broken.png", "not a PNG");
    auto const      next_to_layout = [](std::string_view src, fs::path const &layout_dir) { return std::optional { layout_dir / src }; };
    OverlayRenderer renderer { any_font, next_to_layout };
    renderer.set_layout(layout_of(R"({ "type": "text", "text": "a" },
        { "type": "group", "size": { "width": 100, "height": 100 }, "visible": { "from": 60 }, "children": [
            { "type": "text", "text": "b" }, { "type": "image", "src": "broken.png" } ] })"),
        dir.path / "mine.overlay.json");
    CHECK_EQ(renderer.error().value_or("none"),
        std::format("mine.overlay.json: element 2.2: the picture `broken.png` can't be loaded from `{}`", (dir.path / "broken.png").string()));
}

}

int main()
{
    SetTraceLogLevel(LOG_ERROR);
    test_icons();
    test_not_a_picture();
    test_bleed_colors();
    test_decoded_icon();
    test_picture_that_doesnt_decode();
    test_picture_in_group_that_doesnt_decode();
    return Test::test_result();
}
