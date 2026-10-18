// ps5-homebrew-ui - Title icons: decoding (no OpenGL here; see icon_upload.cpp).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/icon.hpp"

#include "third_party/stb/image.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hui::library
{

bool decode_png(std::string_view bytes, int max_size, Image *out)
{
    *out = Image();
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT32_MAX) || max_size < 1)
        return false;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels =
        stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(bytes.data()),
                              static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (pixels == nullptr)
        return false;

    // The smallest whole factor that brings both sides within max_size.
    int factor = 1;
    while (width / factor > max_size || height / factor > max_size)
        ++factor;
    out->width = width / factor;
    out->height = height / factor;
    if (out->width < 1 || out->height < 1)
    {
        stbi_image_free(pixels);
        *out = Image();
        return false;
    }
    out->rgba.resize(static_cast<std::size_t>(out->width) * out->height * 4);
    if (factor == 1)
    {
        std::memcpy(out->rgba.data(), pixels, out->rgba.size());
    }
    else
    {
        const int area = factor * factor;
        for (int y = 0; y < out->height; ++y)
        {
            for (int x = 0; x < out->width; ++x)
            {
                int sum[4] = {0, 0, 0, 0};
                for (int dy = 0; dy < factor; ++dy)
                {
                    const stbi_uc *row =
                        pixels +
                        (static_cast<std::size_t>(y * factor + dy) * width + x * factor) * 4;
                    for (int dx = 0; dx < factor * 4; ++dx)
                        sum[dx % 4] += row[dx];
                }
                std::uint8_t *to =
                    out->rgba.data() + (static_cast<std::size_t>(y) * out->width + x) * 4;
                for (int c = 0; c < 4; ++c)
                    to[c] = static_cast<std::uint8_t>((sum[c] + area / 2) / area);
            }
        }
    }
    stbi_image_free(pixels);
    return true;
}

} // namespace hui::library
