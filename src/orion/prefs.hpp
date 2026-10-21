// ORION Media Center - ORION's library preferences: custom tabs, placements, the last tab.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/emulators.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hui::orion
{

// The orders a tab can be in: the keys library.json keeps, and their names.
// "tag" groups them under labels (Games of every system, Homebrew, Media);
// "played" and "most_played" follow ORION's play records (orion/playtime.hpp).
inline constexpr int kSortCount = 7;
inline constexpr const char *kSortKeys[kSortCount] = {
    "name", "name_reversed", "recent", "platform", "tag", "played", "most_played"};
inline constexpr const char *kSortNames[kSortCount] = {
    "Name A–Z", "Name Z–A", "Recently added", "Platform", "Tag", "Recently played", "Most played"};

// A shelf the player made. The id is stable across renames; the name is shown.
struct CustomTab
{
    std::string id;
    std::string name;
};

// A folder of games the player added for one emulator: under /data or /mnt.
// An empty system means "worked out from the folder's name", an empty
// emulator RetroArch (library/emulators.hpp, GameFolder).
struct RomFolder
{
    std::string path;
    std::string system;
    std::string emulator; // "porpoise", "ps5sx2"; empty: RetroArch
};

// What the player chose for one emulated system. Systems not listed are on,
// with the first core that plays them.
struct SystemPref
{
    std::string system;  // "Nintendo - Game Boy Advance"
    bool enabled = true; // its tab shows, its games are listed
    std::string core;    // "mgba_libretro.so"; empty: the first that plays it
};

// The shape of the covers on a tab. Automatic follows the pictures: square
// for console icons, the box's own shape for box art.
enum class CoverShape : std::uint8_t
{
    automatic,
    square,
    portrait,  // 5:7, a PS2 or DVD case
    landscape, // 7:5, a GBA or SNES box
};
const char *cover_shape_key(CoverShape shape); // "auto", "square", ...
CoverShape cover_shape_of(std::string_view key);

// What one tab does differently from the rest. Tabs not listed wear the
// main look, follow the Library's sort, fit their covers to the pictures and
// show.
struct TabPrefs
{
    std::string tab;   // a tab key: "games", "sys:Nintendo - Game Boy Advance", "tab:t1"
    std::string theme; // a theme file's id; empty: the main look
    CoverShape cover = CoverShape::automatic;
    std::string sort; // a sort key; empty: the Library's
    std::string view; // a view key; empty: the Library's
    bool hidden = false;
};

// How a tab shows its titles: covers in a grid, big cases three to a screen
// with their system's band across the top, or a list of rows.
inline constexpr int kViewCount = 3;
inline constexpr const char *kViewKeys[kViewCount] = {"grid", "cases", "list"};
inline constexpr const char *kViewNames[kViewCount] = {"Grid", "Cases", "List"};

// Where a title was moved to.
struct Placement
{
    std::string title; // "PPSA01234"
    std::string tab;   // a CustomTab id
};

// Everything the Library remembers between launches, kept in
// <state_dir>/library.json:
//
//   {
//     "last_tab": "tab:t1",
//     "sort": "recent",
//     "view": "grid",
//     "reopen_last_tab": true,
//     "group_systems": true,
//     "continue_row": true,
//     "ask_player": true,
//     "setup_done": true,
//     "autostart": true,
//     "moved_leave_shelf": true,
//     "auto_art": true,
//     "return_on_close": true,
//     "game_backgrounds": true,
//     "blurred_covers": false,
//     "background_strength": 1.0,
//     "rom_folders": [{"path": "/mnt/usb0/roms", "system": "", "emulator": ""}],
//     "art_dir": "/mnt/usb0/ORION/art",
//     "tabs": [{"id": "t1", "name": "RPGs"}],
//     "placements": {"PPSA01234": "t1"},
//     "hidden_titles": ["CUSA00001", "rom-0123456789ab"],
//     "game_players": {"rom-0123456789ab": "retroarch:snes9x_libretro.so"},
//     "systems": {"Nintendo - Game Boy Advance": {"enabled": true, "core": "mgba_libretro.so"}},
//     "tab_settings": {"sys:Nintendo - Sony - PlayStation 2": {"theme": "sunset-nebula",
//                      "cover": "portrait", "sort": "recent", "view": "cases",
//                      "hidden": false}}
//
// (Files from before tab_settings kept themes alone in "tab_themes", which is
// still read.)
//   }
//
// Unknown keys are ignored, so the file can be edited by hand.
struct LibraryPrefs
{
    std::vector<CustomTab> tabs;
    std::vector<Placement> placements;
    std::vector<SystemPref> systems;
    std::vector<RomFolder> rom_folders;
    std::vector<TabPrefs> tab_prefs;
    // "all", "games", "homebrew", "media", "favourites" or "tab:<id>".
    std::string last_tab = "all";
    std::string sort = "name"; // "name", "name_reversed", "recent", "platform"
    std::string view = "grid"; // a kViewKeys entry
    bool reopen_last_tab = true;
    // The emulated systems share one Emulators tab, a row of their own under it.
    bool group_systems = true;
    // The first-run setup was finished (or skipped): it does not show again.
    // A row of what was played last over the All tab's covers.
    bool continue_row = true;
    // A game more than one emulator plays asks which, the first time it starts.
    bool ask_player = true;
    bool setup_done = false;
    // The autostart payload opens ORION after the jailbreak; off writes
    // <state_dir>/autostart-off, which the payload honours.
    bool autostart = true;
    // A title moved to a custom tab leaves its Games / Homebrew / Media
    // shelf; it always stays in All.
    bool moved_leave_shelf = true;
    // New games for an emulator get box art from the internet on start.
    bool auto_art = true;
    // A game started from ORION brings ORION back when it closes.
    bool return_on_close = true;
    // The focused game's home-screen picture behind the library; for games
    // with none, their cover blurred (blurred_covers). Strength 0.2 to 1:
    // below 1 the sky and its clouds show through.
    bool game_backgrounds = true;
    bool blurred_covers = false;
    float background_strength = 1.0f;
    // Where box art is looked for first and downloaded to; empty: ORION's
    // own <state_dir>/art. Always a folder under /data or /mnt.
    std::string art_dir;

    const CustomTab *tab(const std::string &id) const;
    // The tab a title was moved to, or nullptr.
    const CustomTab *placement(const std::string &title) const;
    // Moves a title to a tab (from any other); an unknown tab id is refused.
    bool place(const std::string &title, const std::string &tab_id);
    void unplace(const std::string &title);
    // Adds a tab with a fresh id and returns it. Names are trimmed and kept
    // to 24 bytes; an empty name becomes "New tab".
    const CustomTab &add_tab(const std::string &name);
    bool rename_tab(const std::string &id, const std::string &name);
    // Removes a tab and every placement in it; the titles go back to their shelves.
    bool remove_tab(const std::string &id);
    // Swaps a tab with its neighbour (-1 left, 1 right). False at an end.
    bool move_tab(const std::string &id, int direction);

    bool system_enabled(const std::string &system) const;
    // The chosen core's file, or empty for "the first that plays it".
    std::string system_core(const std::string &system) const;
    void set_system_enabled(const std::string &system, bool enabled);
    void set_system_core(const std::string &system, const std::string &core);

    // The theme a tab wears, or empty for the main look.
    std::string tab_theme(const std::string &tab) const;
    // Empty theme: the tab goes back to the main look.
    void set_tab_theme(const std::string &tab, const std::string &theme);
    CoverShape tab_cover(const std::string &tab) const;
    void set_tab_cover(const std::string &tab, CoverShape cover);
    // The tab's own sort key, or empty for the Library's.
    std::string tab_sort(const std::string &tab) const;
    void set_tab_sort(const std::string &tab, const std::string &sort);
    // The tab's own view key, or empty for the Library's.
    std::string tab_view(const std::string &tab) const;
    void set_tab_view(const std::string &tab, const std::string &view);
    // "all" never hides; a system's tab hides by switching the system off.
    bool tab_hidden(const std::string &tab) const;
    void set_tab_hidden(const std::string &tab, bool hidden);

    // Titles (and games for an emulator) the player hid: they leave every
    // tab but Hidden. By item id (library::is_item_id).
    std::vector<std::string> hidden_titles;
    // How the player chose to play a game for an emulator (library::Player
    // key), by its id; games not listed play as found.
    std::vector<std::pair<std::string, std::string>> game_players;
    std::string game_player(const std::string &id) const;
    // An empty key goes back to the default.
    void set_game_player(const std::string &id, const std::string &key);
    bool title_hidden(const std::string &id) const;
    // False for an id that is not an item id, or when too many are hidden.
    bool set_title_hidden(const std::string &id, bool hidden);
};

// A folder box art may live in: absolute, under /data/ or /mnt/, no "..",
// no control characters, at most 160 bytes.
bool valid_art_dir(std::string_view path);
// The places box art can live, as Settings and the setup offer them: ORION's
// folder (art_dir empty), a USB drive, the extended storage.
constexpr int kArtPlaceCount = 3;
constexpr const char *kArtPlaceNames[] = {"ORION's folder", "USB drive", "Extended storage"};
constexpr const char *kArtPlacePaths[] = {"", "/mnt/usb0/ORION/art", "/mnt/ext0/ORION/art"};
// Which of them art_dir is, or kArtPlaceCount for a folder of the player's.
int art_place(const std::string &art_dir);
// The folder box art goes in: the player's, else <state_dir>/art.
std::string art_folder(const LibraryPrefs &prefs, const std::string &state_dir);
// The player's game folders as the emulator scan takes them.
std::vector<library::GameFolder> game_folders(const LibraryPrefs &prefs);

LibraryPrefs load_prefs(const std::string &state_dir);
// Atomic; false when state_dir is empty or unwritable.
bool save_prefs(const std::string &state_dir, const LibraryPrefs &prefs);

} // namespace hui::orion
