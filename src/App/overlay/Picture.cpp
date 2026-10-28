/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <vector>

#include <overlay/Picture.h>

namespace ST {

std::optional<Image> decode_picture(std::filesystem::path const &path)
{
    auto image = LoadImage(path.string().c_str());
    if (image.data == nullptr) {
        return { };
    }
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    bleed_colors(image);
    return image;
}

// Outwards from the shown pixels, a ring at a time: each pixel of a ring
// gets the average color of its neighbours in the rings before it. Every
// pixel is visited once.
void bleed_colors(Image &image)
{
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    auto *const pixels = static_cast<Color *>(image.data);
    auto const  width = image.width;
    auto const  height = image.height;
    auto const  count = static_cast<size_t>(width) * static_cast<size_t>(height);

    auto const for_neighbours = [width, height](size_t ix, auto const &visit) {
        auto const x = static_cast<int>(ix % static_cast<size_t>(width));
        auto const y = static_cast<int>(ix / static_cast<size_t>(width));
        for (auto ny = y - 1; ny <= y + 1; ++ny) {
            for (auto nx = x - 1; nx <= x + 1; ++nx) {
                if ((nx != x || ny != y) && nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    visit(static_cast<size_t>(ny * width + nx));
                }
            }
        }
    };

    std::vector<bool> colored(count);
    for (size_t ix = 0; ix < count; ++ix) {
        colored[ix] = pixels[ix].a > 0;
    }
    auto                queued = colored;
    std::vector<size_t> ring;
    for (size_t ix = 0; ix < count; ++ix) {
        auto next_to_colored = false;
        for_neighbours(ix, [&](size_t neighbour) { next_to_colored = next_to_colored || colored[neighbour]; });
        if (!colored[ix] && next_to_colored) {
            queued[ix] = true;
            ring.push_back(ix);
        }
    }

    while (!ring.empty()) {
        for (auto const ix : ring) {
            int r = 0, g = 0, b = 0, n = 0;
            for_neighbours(ix, [&](size_t neighbour) {
                if (colored[neighbour]) {
                    r += pixels[neighbour].r;
                    g += pixels[neighbour].g;
                    b += pixels[neighbour].b;
                    ++n;
                }
            });
            pixels[ix] = Color {
                .r = static_cast<unsigned char>(r / n),
                .g = static_cast<unsigned char>(g / n),
                .b = static_cast<unsigned char>(b / n),
                .a = pixels[ix].a,
            };
        }
        std::vector<size_t> next;
        for (auto const ix : ring) {
            colored[ix] = true;
        }
        for (auto const ix : ring) {
            for_neighbours(ix, [&](size_t neighbour) {
                if (!queued[neighbour]) {
                    queued[neighbour] = true;
                    next.push_back(neighbour);
                }
            });
        }
        ring = std::move(next);
    }
}

}
