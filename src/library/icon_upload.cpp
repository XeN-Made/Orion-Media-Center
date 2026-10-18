// ps5-homebrew-ui - Title icons: uploading them as textures (needs OpenGL).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "gfx/gl_batch.hpp"
#include "library/icon.hpp"

#include <string>

namespace hui::library
{

int load_icons(Library &library, gfx::GlBatch &batch)
{
    // A launcher icon is a few hundred kilobytes; refuse anything absurd.
    constexpr std::size_t kMaxFile = 8u << 20;
    int uploaded = 0;
    for (Title &title : library.titles)
    {
        if (title.icon.empty() || title.texture != 0)
            continue;
        std::string bytes;
        Image image;
        if (!save::read_file(title.icon, &bytes, kMaxFile) || !decode_png(bytes, kIconSize, &image))
            continue;
        title.texture = batch.create_texture(image.width, image.height, image.rgba.data());
        uploaded += title.texture != 0 ? 1 : 0;
    }
    return uploaded;
}

} // namespace hui::library
