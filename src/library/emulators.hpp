// ps5-homebrew-ui - Emulated systems: the emulators' games as library entries.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/library.hpp"

#include <span>
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

// The emulators ORION knows. Each is found by its title's name (or a folder
// only it has) and brings its own folders, which are looked in when they exist:
//
//   retroarch  RetroArch: <root>/content/<system>/, any system a core plays.
//              Starts a game through args.txt and test-run.txt (below).
//   porpoise   Porpoise (DolphinPS5): GameCube and Wii discs in
//              <root>/porpoise/games (/app0/porpoise/games to it),
//              /data/porpoise/games, /data/games, /data/roms, /data/iso.
//              Starts a game from its command line, as its own home-screen
//              tiles do: <title id> --rom <disc> [--exit-after-game].
//   ps5sx2     PS5SX2: PS2 discs in /data/PCSX2/games, /data/PCSX2 and a
//              drive's PS2/ or PCSX2/games/. It takes no game from outside
//              that ORION can use yet, so its games open PS5SX2.
struct KnownEmulator
{
    const char *key;   // what the prefs name it by
    const char *name;  // as Settings shows it
    const char *plays; // "GameCube, Wii"
};
std::span<const KnownEmulator> known_emulators();
// Which of them an installed title is ("porpoise"), or empty.
std::string emulator_key(const Title &title);

// A folder of games the player added for one emulator: a system's folder or
// a folder of them, the system given or worked out (see scan_rom_folder).
struct GameFolder
{
    std::string path;
    std::string system;   // empty: worked out
    std::string emulator; // a key above; empty is RetroArch
};

// Everything at once: finds the emulators among titles, reads their own
// folders and the player's, and appends their games to *roms. What
// start-up and Settings' Scan for emulators both run.
Emulator scan_emulators(const std::vector<Title> &titles, const std::vector<GameFolder> &folders,
                        std::vector<Title> *roms);
// The library's emulators and their games, (re)made: games found by an
// earlier scan leave library.titles, scan_emulators runs over the rest, box
// art is attached (attach_rom_art, art_dir) and the games are appended.
// Returns how many there are.
int add_emulator_games(Library &library, const std::vector<GameFolder> &folders,
                       const std::string &art_dir);
// One of RetroArch's playlists (<root>/playlists/<name>, JSON): every game it
// has scanned, wherever the files are (/mnt/usb0/roms/snes), so ORION lists
// what RetroArch lists without being told the folder. The system comes from
// the playlist's name (snes.lpl, or the database's "Nintendo - Super Nintendo
// Entertainment System.lpl"), else the entry's database or core, else the
// playlist's default core. Paths are as RetroArch sees them (/app0 is its
// folder); the folder it scanned joins its app's folders. Returns how many
// games were added (ones already listed are not).
int read_retroarch_playlist(Emulator &emulator, std::string_view text, const std::string &name,
                            std::vector<Title> *roms);
// A way to play a game here: an installed emulator (and for RetroArch, one
// of its cores) and the game's file as that emulator sees it.
struct Player
{
    std::string key;   // "retroarch:snes9x_libretro.so", "porpoise", "ps5sx2"
    std::string label; // "RetroArch · Snes9x"
    const EmulatorApp *app = nullptr;
    std::string core; // RetroArch's core file; empty for the others
    std::string rom;  // the file, as app sees it
};
// Every way this game can be played: each RetroArch core for its system,
// Porpoise for a GameCube or Wii disc, PS5SX2 for a PS2 disc. In that order.
std::vector<Player> players_for(const Emulator &emulator, const Title &game);
// One of the player's folders, for its emulator when that one is installed.
// Returns how many games were added.
int scan_game_folder(Emulator &emulator, const GameFolder &folder, std::vector<Title> *roms);

// A GameCube or Wii disc's header: plain (.iso, .gcm), inside a WIA or RVZ
// file, or the kind a GCZ or WBFS file says. head is the file's first
// kilobyte. Empty id and title when the format keeps them elsewhere.
struct Disc
{
    std::string id;     // "GATP51"
    std::string title;  // "ATV: Quad Power Racing 2"
    std::string system; // "Nintendo - GameCube" or "Nintendo - Wii"
};
bool read_nintendo_disc(std::string_view head, Disc *disc);

// The command line Porpoise starts a disc with (one argument a line, the
// launch helper's request format after the title ID).
std::string porpoise_arguments(const std::string &title_id, const std::string &disc,
                               bool exit_after_game);

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
// What one emulator plays: RetroArch's cores, the others' own systems.
std::vector<std::string> known_systems(const Emulator &emulator, std::string_view key);

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
