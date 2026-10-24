// ORION Media Center - Title icons: uploading them as textures (needs OpenGL).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "gfx/gl_batch.hpp"
#include "library/case_templates.hpp"
#include "library/icon.hpp"

#include <algorithm>
#include <cctype>
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

int load_wanted_icons(Library &library, std::vector<std::string> *ids, gfx::GlBatch &batch,
                      int budget)
{
    int uploaded = 0;
    std::size_t done = 0;
    for (; done < ids->size() && uploaded < budget; ++done)
    {
        const std::string &id = (*ids)[done];
        const auto title = std::find_if(library.titles.begin(), library.titles.end(),
                                        [&](const Title &t) { return t.id == id; });
        if (title != library.titles.end() && title->texture == 0)
            uploaded += load_icon(*title, batch) ? 1 : 0;
    }
    ids->erase(ids->begin(), ids->begin() + static_cast<std::ptrdiff_t>(done));
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

int load_case_templates(Library &library, const std::string &folder, gfx::GlBatch &batch)
{
    for (const CaseTemplate &shell : library.cases)
    {
        if (shell.texture != 0)
            batch.delete_texture(shell.texture);
    }
    library.cases.clear();
    for (const save::DirectoryEntry &entry : save::list_directory(folder))
    {
        const std::string &name = entry.name;
        if (entry.directory || name.size() < 5 || library.cases.size() >= 32)
            continue;
        std::string extension = name.substr(name.size() - 4);
        for (char &c : extension)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension != ".png")
            continue;
        std::string bytes;
        Image picture;
        CaseWindow window;
        if (!save::read_file(folder + "/" + name, &bytes, kMaxFile) ||
            !decode_png(bytes, kSharpSize, &picture) || !find_case_window(picture, &window))
            continue;
        CaseTemplate shell;
        for (const char c : name.substr(0, name.size() - 4))
            shell.key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        shell.texture = batch.create_texture(picture.width, picture.height, picture.rgba.data());
        if (shell.texture == 0)
            continue;
        shell.width = picture.width;
        shell.height = picture.height;
        shell.window_x = window.x;
        shell.window_y = window.y;
        shell.window_w = window.w;
        shell.window_h = window.h;
        library.cases.push_back(std::move(shell));
    }
    return static_cast<int>(library.cases.size());
}

int load_wide_icons(Library &library, const std::vector<std::string> &ids,
                    const std::string &folder, gfx::GlBatch &batch, int budget)
{
    int uploaded = 0;
    for (Title &title : library.titles)
    {
        const bool wanted = std::find(ids.begin(), ids.end(), title.id) != ids.end();
        if (!wanted)
        {
            if (title.wide_texture != 0)
                batch.delete_texture(title.wide_texture);
            title.wide_texture = 0;
            if (title.wide_width > 0)
                title.wide_width = title.wide_height = 0;
            continue;
        }
        if (title.wide_texture != 0 || title.wide_width < 0 || uploaded >= budget)
            continue;
        std::string bytes;
        Image image;
        if (!save::read_file(folder + "/" + title.id + ".tga", &bytes, kMaxFile) ||
            !decode_png(bytes, 1024, &image))
            continue; // not made yet: asked again next frame
        title.wide_texture = batch.create_texture(image.width, image.height, image.rgba.data());
        title.wide_width = title.wide_texture != 0 ? image.width : -1;
        title.wide_height = image.height;
        ++uploaded;
    }
    return uploaded;
}

} // namespace hui::library
