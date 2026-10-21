// ORION Media Center - Box art matching: a ROM's file name against the libretro thumbnail sets.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hui::library
{

// The libretro thumbnail sets (thumbnails.libretro.com/<system>/Named_Boxarts/)
// name pictures after No-Intro releases: "Chrono Trigger (USA).png". ROMs on a
// console are often named otherwise ("3055 - Chrono Trigger (U)(XenoPhobia).nds"),
// so a match ignores tags, a scene release number, punctuation, "The" and
// "&", and then picks the release of the ROM's own region, else USA, World,
// Europe, Japan, else the one with the fewest tags.

// The pictures a thumbnail folder's index page links to, URL-decoded:
// "Chrono Trigger (USA).png".
std::vector<std::string> parse_art_index(std::string_view html);

// The picture for a ROM file among those, or empty.
std::string match_art(std::string_view rom_file, const std::vector<std::string> &pictures);

// The title alone, folded for comparison: "Legend of Zelda, The - The Minish
// Cap (USA)" and "The Legend of Zelda - The Minish Cap (U)" both give
// "legendofzeldatheminishcap".
std::string art_key(std::string_view name);

// Percent-encodes a path segment for a URL (everything but A-Z a-z 0-9 - _ . ~).
std::string url_encode(std::string_view text);

} // namespace hui::library
