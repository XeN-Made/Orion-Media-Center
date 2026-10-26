// ORION Media Center - The console's own library: what ShadowMountPlus can mount.
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
    installed, // registered with the system, nowhere ORION looks (see merge_registered)
    payload,   // an ELF for the console's ELF loader, from a payloads folder (scan_payloads)
};

struct Title
{
    std::string id;       // "PPSA01234"
    std::string name;     // as the home screen shows it, or as the player renamed it
    std::string own_name; // the name it came with, once renamed (apply_names); else empty
    std::string version;  // "01.000.010"; empty when unknown
    Category category = Category::game;
    Platform platform = Platform::unknown;
    Source source = Source::folder;
    // Homebrew is what declares no DRM ("applicationDrmType": "free");
    // retail titles say "standard" or "upgradable", and images are dumps.
    bool homebrew = false;
    std::string path; // the folder or image file it was found at
    std::string icon; // a PNG to show for it; empty when there is none
    // The picture it came with while one of the player's shows instead
    // (apply_custom_art); else empty.
    std::string own_icon;
    // When the folder or image last changed (seconds since 1970): the best
    // "recently added" the file system offers. 0 when unknown.
    std::int64_t added = 0;
    // The icon's texture, once something has uploaded it (0 until then), and
    // its size: box art is seldom square.
    std::uint32_t texture = 0;
    int icon_width = 0;
    int icon_height = 0;
    // The same picture kept sharp (up to kSharpSize) while a design shows it
    // large, then released (load_sharp_icons). sharp_width < 0: it could not
    // be read; not tried again.
    std::uint32_t sharp_texture = 0;
    // A wide copy of its home-screen picture (orion/wide_copies.hpp), for
    // tabs with wide covers, kept only while such a tab shows it
    // (load_wide_icons). wide_width < 0: there is none to load.
    std::uint32_t wide_texture = 0;
    int wide_width = 0;
    int wide_height = 0;
    int sharp_width = 0;
    int sharp_height = 0;
    // A game for an emulator (library/emulators.hpp): the system it is for
    // ("Nintendo - Game Boy Advance"), the file as the emulator sees it, and
    // the app that plays it. Empty for the console's own titles.
    std::string system;
    std::string rom;
    std::string launcher;
    // GameTDB's ID for it, where its emulator keeps one ("ALZE01"): its box
    // art is fetched by it (orion/art_fetcher.hpp).
    std::string serial;
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

// One emulator app found on this console (library/emulators.hpp knows which).
struct EmulatorApp
{
    std::string key;      // "retroarch", "porpoise", "ps5sx2": what the prefs name it by
    std::string name;     // "RetroArch"
    std::string title_id; // the app to start ("PPSA99169")
    std::string root;     // its folder as ORION sees it
    std::vector<std::string> systems; // what it plays ("Nintendo - GameCube")
    // The folders it looks in by itself that exist here, as ORION sees them.
    std::vector<std::string> folders;
    int games = 0;
    // ORION can start a chosen game in it; otherwise it only opens the app.
    bool starts_games = false;
};

// Every emulator found, and the systems they have games for.
struct Emulator
{
    std::vector<EmulatorApp> apps; // in the order emulators.hpp knows them
    std::vector<Core> cores;       // RetroArch's
    std::vector<System> systems;   // with games, sorted by name
    const EmulatorApp *app(std::string_view key) const
    {
        for (const EmulatorApp &app : apps)
        {
            if (app.key == key)
                return &app;
        }
        return nullptr;
    }
    // The app a game for an emulator starts in (Title::launcher).
    const EmulatorApp *app_of(const Title &title) const
    {
        for (const EmulatorApp &app : apps)
        {
            if (app.title_id == title.launcher)
                return &app;
        }
        return nullptr;
    }
};

// What the designs receive: the titles found at start-up.
// A picture of a case from <state_dir>/cases/<key>.png (library/case_templates.hpp),
// uploaded: Cases view draws a cover in its window, then the picture over it.
struct CaseTemplate
{
    std::string key; // "ps5", "ps4", "ps2", "homebrew", ...
    std::uint32_t texture = 0;
    int width = 0;
    int height = 0;
    float window_x = 0.0f; // the cover's window, as fractions of the picture
    float window_y = 0.0f;
    float window_w = 0.0f;
    float window_h = 0.0f;
    bool window_solid = false; // a flat colour, not clear: the cover goes over it
};

struct Library
{
    std::vector<Title> titles;
    // The player's case pictures, uploaded at start (load_case_templates).
    std::vector<CaseTemplate> cases;
    // True once a scan ran (an empty list then means "nothing installed").
    bool scanned = false;
    // Where ORION keeps what the player chooses (favourites, ...); empty
    // keeps nothing. /data/orion on the console, so it survives a reinstall.
    std::string state_dir;
    // ORION's own folder as the launch helper sees it ("/data/homebrew/PPSA99062"):
    // where its play log and backdrops go. ORION is not in titles itself.
    std::string self_path;
    // ORION's own title ID: never listed (merge_registered skips it).
    std::string self_id;
    // The app's own theme files (orion/theme_file.hpp); the player's are in
    // <state_dir>/themes. Empty: the kit's themes, built in.
    std::string themes_dir;
    // The emulators installed and the systems they have games for. Their
    // games are in `titles` too, marked by Title::system.
    Emulator emulator;
    // ORION shows its first-run setup until it is finished (orion/setup_panel.hpp).
    // The app sets this; tests and the PC tour leave it off unless asked.
    bool offer_setup = false;
    // Grows each time the library is scanned again while ORION runs: a design
    // that keeps indices into titles rebuilds when it changes.
    int generation = 0;
};

// The three shelves the Library groups titles on.
enum class Shelf : std::uint8_t
{
    games,    // retail titles in the Games area
    homebrew, // DRM-free apps in the Games area, and titles with no retail ID
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
// The console's titles whose sharper picture ORION saved (<folder>/<ID>.png,
// the home screen's 512-pixel icon0.png; ShadowMountPlus caches 128 pixels)
// show that instead. Returns how many.
int prefer_saved_icons(Library &library, const std::string &folder);
// The player's names for titles (id, name); an empty name gives a title the
// name it came with back. Returns how many titles changed.
int apply_names(Library &library, const std::vector<std::pair<std::string, std::string>> &names);
// The player's own pictures, in <folder> (/data/orion/custom-art): a PNG or
// JPEG named after a title's ID, its name, or a game's file shows instead of
// the title's picture; names match as box art does (art_key), so case and
// punctuation do not matter. A title whose file is gone gets its own picture
// back. Returns the IDs whose picture changed (their textures are stale).
std::vector<std::string> apply_custom_art(Library &library, const std::string &folder);
// The titles the system has registered that ORION's own scan does not find:
// installed packages, and images ShadowMountPlus mounted from wherever it
// found them. orion/registered_titles.hpp keeps them in a cache, one line a
// title: "ID\tplatform\tcategory\thomebrew\tname".
std::string registered_line(const Title &title);
// Adds the cache's titles that are not listed yet (nor skip_id, ORION's own),
// as Source::installed, with <icons>/<ID>.png as their picture when there is
// one. Returns how many were added.
int merge_registered(Library &library, std::string_view cache, const std::string &icons,
                     const std::string &skip_id);

// "PPSA01234" style: four capital letters and five digits.
bool is_title_id(std::string_view text);
// A title ID, an emulated game's ID ("rom-" and twelve hex digits) or an
// ELF program's ("elf-" and twelve): what favourites and placements may name.
bool is_item_id(std::string_view text);

// The ELF programs in these folders (file managers, uploaders...: payloads
// the ELF loader runs, nothing installed), as Homebrew titles. Each is named
// for its file; a PNG or JPEG of the same name beside it is its picture.
std::vector<Title> scan_payloads(const std::vector<std::string> &folders);

// The parts of a title that come from its metadata file. Exposed for tests.
bool read_param_json(std::string_view text, Title *title);
bool read_param_sfo(std::string_view bytes, Title *title);
// The ID and display name an image's file name carries:
// "PPSA01234 - Some Title.ffpfsc" -> PPSA01234, "Some Title".
bool parse_image_name(std::string_view file_name, Title *title);

const char *category_name(Category category);
const char *platform_name(Platform platform);

} // namespace hui::library
