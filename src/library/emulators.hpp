// ps5-homebrew-ui - Emulated systems: RetroArch's cores and content as library entries.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/library.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hui::library
{

// The RetroArch port keeps cores in <root>/cores (with their .info files)
// and content in <root>/content/<database name>/. It takes a game the way
// its own ES-DE starts one: `-L /app0/cores/<core> /app0/content/<system>/<file>`,
// /app0 being its folder as it sees it. A title is taken for RetroArch when
// its name says so and its folder has cores.
bool is_retroarch(const Title &title);

// Reads the cores and the content of a RetroArch folder. ROMs become Titles
// (system, rom and launcher set; see Title), sorted by name within each
// system, and are appended to *roms. Folders that hold no file a core takes
// are left out of the systems.
Emulator scan_retroarch(const Title &retroarch, std::vector<Title> *roms);

// Parts of the scan, exposed for tests.
bool read_core_info(std::string_view text, const std::string &file, Core *core);
// "007 - Nightfire (UE) (M3) [!].gba" -> "007 - Nightfire";
// "3055 - Chrono Trigger (U)(XenoPhobia).nds" -> "Chrono Trigger".
std::string rom_display_name(std::string_view file);
// "Nintendo - Game Boy Advance" -> "GBA"; unknown systems keep what follows
// the maker ("Sega - Saturn" -> "Saturn").
std::string short_system_name(std::string_view database);
// "nds" for "Nintendo - Nintendo DS": ES-DE's name for a system; empty when
// unknown.
std::string esde_system_name(std::string_view database);
// RetroArch's thumbnail file names: &*/:`<>?\|" in a game's name become _.
std::string thumbnail_name(std::string_view stem);

// Box art for games for an emulator: sets the icon of each ROM in roms that
// has a picture, from the first of these that exists (stem: the ROM's file
// name without its extension):
//   <art_dir>/<system>/<stem>.png                 ORION's own (tools/fetch-rom-art.py)
//   <root>/thumbnails/<system>/Named_Boxarts/<thumbnail_name(stem)>.png
//                                                 RetroArch's thumbnails
//   <root>/es-de/ES-DE/downloaded_media/<esde_system_name>/covers/<stem>.png
//                                                 what ES-DE's scraper saved
// root is RetroArch's folder. Returns how many ROMs got one.
int attach_rom_art(std::vector<Title> &roms, const std::string &root, const std::string &art_dir);

// The RetroArch port starts a game in "game mode" (`--ps5-mode=game`): it
// reads <root>/game-request.txt, a command line as its ES-DE writes one,
// paths escaped with backslashes as a shell would take them:
//   /app0/eboot.bin -L /app0/cores/mgba_libretro.so /app0/content/Nintendo\ -\ ...gba
// Started any other way it shows its frontend picker and ignores the game.
// The mode decides before anything else is read (its args.txt only adds to
// RetroArch's own arguments later), so it must come with the launch. A
// launch with the mode as its only argument still reached the picker: the
// system seems to take the first argument for the program's name, so ORION
// passes the mode twice.
std::string retroarch_game_request(const std::string &core_file, const std::string &rom);
constexpr const char *kRetroArchGameMode = "--ps5-mode=game";

// "rom-" and twelve hex digits from the ROM's path: stable across scans.
std::string rom_id(std::string_view path);

} // namespace hui::library
