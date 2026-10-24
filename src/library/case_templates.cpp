// ORION Media Center - Case templates: a player's case picture with the cover in its window.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/case_templates.hpp"

#include "library/emulators.hpp"

#include <cctype>

namespace hui::library
{

namespace
{

constexpr std::uint8_t kClear = 64; // alpha below this is the window

bool clear(const Image &picture, int x, int y)
{
    const std::size_t pixel =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(picture.width) +
        static_cast<std::size_t>(x);
    return picture.rgba[pixel * 4 + 3] < kClear; // its alpha
}

std::string simple(std::string_view name)
{
    std::string key;
    for (const char c : name)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0)
            key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return key;
}

} // namespace

bool find_case_window(const Image &picture, CaseWindow *window)
{
    *window = CaseWindow();
    if (picture.width < 8 || picture.height < 8 ||
        picture.rgba.size() != static_cast<std::size_t>(picture.width) * picture.height * 4)
        return false;
    const int cx = picture.width / 2;
    const int cy = picture.height / 2;
    if (!clear(picture, cx, cy))
        return false;
    int left = cx;
    int right = cx;
    int top = cy;
    int bottom = cy;
    while (left > 0 && clear(picture, left - 1, cy))
        --left;
    while (right < picture.width - 1 && clear(picture, right + 1, cy))
        ++right;
    while (top > 0 && clear(picture, cx, top - 1))
        --top;
    while (bottom < picture.height - 1 && clear(picture, cx, bottom + 1))
        ++bottom;
    const int w = right - left + 1;
    const int h = bottom - top + 1;
    if (w * 3 < picture.width || h * 3 < picture.height)
        return false;
    const float width = static_cast<float>(picture.width);
    const float height = static_cast<float>(picture.height);
    *window = {static_cast<float>(left) / width, static_cast<float>(top) / height,
               static_cast<float>(w) / width, static_cast<float>(h) / height};
    return true;
}

std::string case_key(const Title &title, std::string *alias)
{
    if (alias != nullptr)
        alias->clear();
    if (title.is_rom())
    {
        if (alias != nullptr)
            *alias = esde_system_name(title.system);
        return simple(short_system_name(title.system));
    }
    switch (shelf_of(title))
    {
    case Shelf::homebrew:
        return "homebrew";
    case Shelf::media:
        return "media";
    case Shelf::games:
        break;
    }
    return title.platform == Platform::ps4 ? "ps4" : "ps5";
}

} // namespace hui::library
