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

// GameTDB (art.gametdb.com) has covers the libretro sets lack (Switch) or
// that an emulator names its games by (PS5-ECAM's Wii U, 3DS and DS IDs).
// Its platform folder for a system ("wiiu", "3ds", "ds", "switch"), or empty.
std::string gametdb_platform(std::string_view system);
// The covers for a game's GameTDB ID, most likely first: the HQ cover, then
// the medium one, in the region the ID says (its fourth letter: E USA, P
// Europe, J Japan, K Korea), then USA, Europe and Japan.
std::vector<std::string> gametdb_covers(std::string_view platform, std::string_view id);
// GameTDB's Switch list (switchtdb.xml) as "<art_key of the title>\t<ID>\n"
// lines: Switch games are known by title ID, which the list does not have,
// so they are matched by name. The first ID of a title is kept.
std::string gametdb_title_index(std::string_view xml);
// The ID for a game's name in such an index, or empty.
std::string gametdb_id(std::string_view index, std::string_view name);

} // namespace hui::library
