// ps5-homebrew-ui - ORION's library preferences: custom tabs, placements, the last tab.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace hui::orion
{

// A shelf the player made. The id is stable across renames; the name is shown.
struct CustomTab
{
    std::string id;
    std::string name;
};

// What the player chose for one emulated system. Systems not listed are on,
// with the first core that plays them.
struct SystemPref
{
    std::string system;  // "Nintendo - Game Boy Advance"
    bool enabled = true; // its tab shows, its games are listed
    std::string core;    // "mgba_libretro.so"; empty: the first that plays it
};

// A tab that wears its own theme instead of the main look.
struct TabTheme
{
    std::string tab;   // a tab key: "games", "sys:Nintendo - Game Boy Advance", "tab:t1"
    std::string theme; // a theme file's id
};

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
//     "reopen_last_tab": true,
//     "moved_leave_shelf": true,
//     "auto_art": true,
//     "tabs": [{"id": "t1", "name": "RPGs"}],
//     "placements": {"PPSA01234": "t1"},
//     "systems": {"Nintendo - Game Boy Advance": {"enabled": true, "core": "mgba_libretro.so"}},
//     "tab_themes": {"sys:Nintendo - Game Boy Advance": "sunset-nebula"}
//   }
//
// Unknown keys are ignored, so the file can be edited by hand.
struct LibraryPrefs
{
    std::vector<CustomTab> tabs;
    std::vector<Placement> placements;
    std::vector<SystemPref> systems;
    std::vector<TabTheme> tab_themes;
    // "all", "games", "homebrew", "media", "favourites" or "tab:<id>".
    std::string last_tab = "all";
    std::string sort = "name"; // "name", "name_reversed", "recent", "platform"
    bool reopen_last_tab = true;
    // A title moved to a custom tab leaves its Games / Homebrew / Media
    // shelf; it always stays in All.
    bool moved_leave_shelf = true;
    // New games for an emulator get box art from the internet on start.
    bool auto_art = true;

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
};

LibraryPrefs load_prefs(const std::string &state_dir);
// Atomic; false when state_dir is empty or unwritable.
bool save_prefs(const std::string &state_dir, const LibraryPrefs &prefs);

} // namespace hui::orion
