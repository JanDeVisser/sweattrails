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

#include <TempDir.h>
#include <TestApp.h>

// The pictures an overlay's image shows, as the renderer reads them:
// raylib's LoadImage(), which needs no window. Only making a texture of
// one does.

using namespace ST;

namespace {

namespace fs = std::filesystem;

fs::path const Icons = fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "overlays" / "icons";

void put(fs::path const &path, std::string_view bytes)
{
    std::ofstream out { path, std::ios::binary };
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
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

}

int main()
{
    SetTraceLogLevel(LOG_ERROR);
    test_icons();
    test_not_a_picture();
    return Test::test_result();
}
