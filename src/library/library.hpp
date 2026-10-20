// ps5-homebrew-ui - The console's own library: what ShadowMountPlus can mount.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hui::library
{

// Which home-screen area the title declares (param.json applicationCategoryType,
// or the PS4 param.sfo CATEGORY).
enum class Category : std::uint8_t
{
    game,
    media,
    app, // anything else a title may declare
};

enum class Platform : std::uint8_t
{
    ps5,     // PPSA..., and anything with a param.json
    ps4,     // CUSA... with a param.sfo
    unknown, // an image whose ID prefix says nothing
};

// How the title reaches the console: an unpacked folder or a disk image that
// ShadowMountPlus mounts on demand.
enum class Source : std::uint8_t
{
    folder,
    image,
};

struct Title
{
    std::string id;      // "PPSA01234"
    std::string name;    // as the home screen shows it
    std::string version; // "01.000.010"; empty when unknown
    Category category = Category::game;
    Platform platform = Platform::unknown;
    Source source = Source::folder;
    // Homebrew is what declares no DRM ("applicationDrmType": "free");
    // retail titles say "standard" or "upgradable", and images are dumps.
    bool homebrew = false;
    std::string path; // the folder or image file it was found at
    std::string icon; // a PNG to show for it; empty when there is none
    // When the folder or image last changed (seconds since 1970): the best
    // "recently added" the file system offers. 0 when unknown.
    std::int64_t added = 0;
    // The icon's texture, once something has uploaded it (0 until then), and
    // its size: box art is seldom square.
    std::uint32_t texture = 0;
    int icon_width = 0;
    int icon_height = 0;
    // A game for an emulator (library/emulators.hpp): the system it is for
    // ("Nintendo - Game Boy Advance"), the file as the emulator sees it, and
    // the app that plays it. Empty for the console's own titles.
    std::string system;
    std::string rom;
    std::string launcher;
    bool is_rom() const
    {
        return !system.empty();
    }
};

// One libretro core, from its .info file.
struct Core
{
    std::string file;                    // "mgba_libretro.so"
    std::string name;                    // "mGBA"
    std::vector<std::string> databases;  // "Nintendo - Game Boy Advance", ...
    std::vector<std::string> extensions; // "gba", "gb", ... (lowercase)
};

// One system RetroArch keeps content for: a folder under content/ named as
// the libretro databases name it.
struct System
{
    std::string name;               // "Nintendo - Game Boy Advance"
    std::string short_name;         // "GBA": the tab and the banner
    std::vector<std::string> cores; // core files that play it, as RetroArch lists them
    int games = 0;
};

// RetroArch as found on this console: where it is and what it can play.
struct Emulator
{
    std::string title_id; // the app to start ("PPSA99169")
    std::string root;     // its folder as ORION sees it
    std::vector<Core> cores;
    std::vector<System> systems; // with content, sorted by name
};

// What the designs receive: the titles found at start-up.
struct Library
{
    std::vector<Title> titles;
    // True once a scan ran (an empty list then means "nothing installed").
    bool scanned = false;
    // Where ORION keeps what the player chooses (favourites, ...); empty
    // keeps nothing. /data/orion on the console, so it survives a reinstall.
    std::string state_dir;
    // ORION's own folder as the launch helper sees it ("/data/homebrew/PPSA99061"):
    // where its play log and backdrops go. ORION is not in titles itself.
    std::string self_path;
    // The app's own theme files (orion/theme_file.hpp); the player's are in
    // <state_dir>/themes. Empty: the kit's themes, built in.
    std::string themes_dir;
    // RetroArch, when there is one, and the systems it has games for. Its
    // games are in `titles` too, marked by Title::system.
    Emulator emulator;
};

// The three shelves the Library groups titles on.
enum class Shelf : std::uint8_t
{
    games,    // retail titles in the Games area
    homebrew, // DRM-free apps in the Games area
    media,    // anything in the Media area
};
Shelf shelf_of(const Title &title);

struct ScanOptions
{
    // Folders whose children are titles, in priority order: the first copy of
    // a title ID wins, and a folder always wins over an image of the same ID.
    std::vector<std::string> roots;
    // ShadowMountPlus's icon cache: "<ID>-<size>-<stamp>-<n>.png" files, the
    // only artwork an image title has until it is mounted.
    std::string icon_cache;
    // Titles to leave out (ORION itself, for one).
    std::vector<std::string> exclude;
    // When set, receives one line per root that could be listed:
    // "<root>: <entries> entries, <titles> titles".
    std::vector<std::string> *report = nullptr;
};

// The places ShadowMountPlus scans by default on a console, and its cache.
ScanOptions console_defaults();

// Reads every root. Unreadable roots, folders without metadata and unknown
// files are skipped silently; the result is sorted by name.
std::vector<Title> scan(const ScanOptions &options);

// "PPSA01234" style: four capital letters and five digits.
bool is_title_id(std::string_view text);
// A title ID or an emulated game's ID ("rom-" and twelve hex digits): what
// favourites and placements may name.
bool is_item_id(std::string_view text);

// The parts of a title that come from its metadata file. Exposed for tests.
bool read_param_json(std::string_view text, Title *title);
bool read_param_sfo(std::string_view bytes, Title *title);
// The ID and display name an image's file name carries:
// "PPSA01234 - Some Title.ffpfsc" -> PPSA01234, "Some Title".
bool parse_image_name(std::string_view file_name, Title *title);

const char *category_name(Category category);
const char *platform_name(Platform platform);

} // namespace hui::library
