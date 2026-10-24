// ORION Media Center - Title icons: decoding (no OpenGL here; see icon_upload.cpp).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/icon.hpp"

#include "third_party/stb/image.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// WebP is decoded by libwebp where the build has it (the PS5's, from
// PacBrew); elsewhere a WebP file is refused like any unreadable picture.
#if defined(__PROSPERO__)
#include <webp/decode.h>
#define HUI_WEBP 1
#endif

namespace hui::library
{

const char *png_failure()
{
    const char *reason = stbi_failure_reason();
    return reason != nullptr ? reason : "unknown";
}

namespace
{

bool is_webp(std::string_view bytes)
{
    return bytes.size() > 12 && bytes.compare(0, 4, "RIFF") == 0 &&
           bytes.compare(8, 4, "WEBP") == 0;
}

// The decoded pixels (RGBA8), freed the way their decoder wants.
struct Pixels
{
    std::uint8_t *data = nullptr;
    bool webp = false;
    Pixels() = default;
    Pixels(const Pixels &) = delete;
    Pixels &operator=(const Pixels &) = delete;
    ~Pixels()
    {
        if (data == nullptr)
            return;
#if defined(HUI_WEBP)
        if (webp)
        {
            WebPFree(data);
            return;
        }
#endif
        stbi_image_free(data);
    }
};

} // namespace

bool decode_png(std::string_view bytes, int max_size, Image *out)
{
    *out = Image();
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT32_MAX) || max_size < 1)
        return false;
    int width = 0;
    int height = 0;
    int channels = 0;
    Pixels decoded;
    if (is_webp(bytes))
    {
#if defined(HUI_WEBP)
        decoded.webp = true;
        decoded.data = WebPDecodeRGBA(reinterpret_cast<const std::uint8_t *>(bytes.data()),
                                      bytes.size(), &width, &height);
#endif
    }
    else
        decoded.data =
            stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(bytes.data()),
                                  static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    const std::uint8_t *pixels = decoded.data;
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
                    const std::uint8_t *row =
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
    return true;
}

} // namespace hui::library
