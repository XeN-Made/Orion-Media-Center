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

// The player's own ROM folders (as ORION and RetroArch both see them, under
// /data or /mnt): a folder of one system, the system given or worked out from
// its name ("gba", "GBA" or "Nintendo - Game Boy Advance"), or a folder of
// such folders (ES-DE's layout: roms/gba, roms/nds). Their games join the
// systems' tabs; a system RetroArch has no content for gets one. Returns how
// many games were added.
int scan_rom_folder(Emulator &emulator, const std::string &path, const std::string &system,
                    std::vector<Title> *roms);
// The system a folder's name stands for, or empty.
std::string system_of_folder(std::string_view name, const Emulator &emulator);
// Every system a core of RetroArch's plays, by short name.
std::vector<std::string> known_systems(const Emulator &emulator);

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
//   <art_dir>/<system>/<stem>.png                 ORION's own (it downloads there)
//   <root>/thumbnails/<system>/Named_Boxarts/<thumbnail_name(stem)>.png
//                                                 RetroArch's thumbnails
//   <root>/es-de/ES-DE/downloaded_media/<esde_system_name>/covers/<stem>.png
//                                                 what ES-DE's scraper saved
// root is RetroArch's folder. Returns how many ROMs got one.
int attach_rom_art(std::vector<Title> &roms, const std::string &root, const std::string &art_dir);

// How the RetroArch port's launcher can be made to start a game. It picks a
// mode (picker, RetroArch, ES-DE, game) from its own command line, which a
// launch from outside cannot set: the system hands the title no arguments.
// So ORION uses the launcher's own test run, two files in its folder:
//   <root>/test-run.txt   any content: this start is a test run, which goes
//                         straight to RetroArch (no picker, whatever
//                         frontend.cfg remembers); the launcher deletes it
//   <root>/args.txt       one argument per line (up to 255 bytes, '#'
//                         comments, "--ps5-" lines are the launcher's), added
//                         to RetroArch's own: -L <core> <game>
// Without test-run.txt the launcher deletes args.txt, with its other test
// files, before reading it. RetroArch then starts the game as its own command
// line asks. args.txt may be the player's too: ORION keeps what it replaced
// and the launch helper puts it back once RetroArch is up (payload/launcher).
//
// The args.txt for a game: "-L", the core in /app0/cores, the game. The
// token (unique to the start) sits in a comment, so the launch helper can tell
// this start's file from a later one's.
std::string retroarch_args(const std::string &core_file, const std::string &rom,
                           const std::string &token);
// Lines RetroArch's launcher reads no longer than this.
constexpr std::size_t kRetroArchArgMax = 255;

// "rom-" and twelve hex digits from the ROM's path: stable across scans.
std::string rom_id(std::string_view path);

} // namespace hui::library
