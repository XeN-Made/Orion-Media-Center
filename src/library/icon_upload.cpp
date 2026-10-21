// ps5-homebrew-ui - Title icons: uploading them as textures (needs OpenGL).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "gfx/gl_batch.hpp"
#include "library/icon.hpp"

#include <algorithm>
#include <string>

namespace hui::library
{

namespace
{

// A launcher icon is a few hundred kilobytes; refuse anything absurd.
constexpr std::size_t kMaxFile = 8u << 20;

bool load_icon(Title &title, gfx::GlBatch &batch)
{
    if (title.icon.empty() || title.texture != 0)
        return false;
    std::string bytes;
    Image image;
    if (!save::read_file(title.icon, &bytes, kMaxFile) || !decode_png(bytes, kIconSize, &image))
        return false;
    title.texture = batch.create_texture(image.width, image.height, image.rgba.data());
    title.icon_width = image.width;
    title.icon_height = image.height;
    return title.texture != 0;
}

} // namespace

int load_icons(Library &library, gfx::GlBatch &batch)
{
    int uploaded = 0;
    for (Title &title : library.titles)
    {
        if (!title.is_rom())
            uploaded += load_icon(title, batch) ? 1 : 0;
    }
    return uploaded;
}

int load_wanted_icons(Library &library, const std::vector<std::string> &ids, gfx::GlBatch &batch)
{
    int uploaded = 0;
    for (Title &title : library.titles)
    {
        if (std::find(ids.begin(), ids.end(), title.id) != ids.end())
            uploaded += load_icon(title, batch) ? 1 : 0;
    }
    return uploaded;
}

int load_sharp_icons(Library &library, const std::vector<std::string> &ids, gfx::GlBatch &batch,
                     int budget)
{
    int uploaded = 0;
    for (Title &title : library.titles)
    {
        const bool wanted = std::find(ids.begin(), ids.end(), title.id) != ids.end();
        if (!wanted)
        {
            if (title.sharp_texture != 0)
                batch.delete_texture(title.sharp_texture);
            title.sharp_texture = 0;
            title.sharp_width = title.sharp_height = 0;
            continue;
        }
        if (title.sharp_texture != 0 || title.sharp_width < 0 || title.icon.empty() ||
            uploaded >= budget)
            continue;
        std::string bytes;
        Image image;
        if (!save::read_file(title.icon, &bytes, kMaxFile) ||
            !decode_png(bytes, kSharpSize, &image))
        {
            title.sharp_width = -1;
            continue;
        }
        title.sharp_texture = batch.create_texture(image.width, image.height, image.rgba.data());
        title.sharp_width = title.sharp_texture != 0 ? image.width : -1;
        title.sharp_height = image.height;
        ++uploaded;
    }
    return uploaded;
}

} // namespace hui::library
