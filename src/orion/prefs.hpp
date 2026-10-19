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
//     "tabs": [{"id": "t1", "name": "RPGs"}],
//     "placements": {"PPSA01234": "t1"}
//   }
//
// Unknown keys are ignored, so the file can be edited by hand.
struct LibraryPrefs
{
    std::vector<CustomTab> tabs;
    std::vector<Placement> placements;
    // "all", "games", "homebrew", "media", "favourites" or "tab:<id>".
    std::string last_tab = "all";
    std::string sort = "name"; // "name", "name_reversed", "recent", "platform"
    bool reopen_last_tab = true;
    // A title moved to a custom tab leaves its Games / Homebrew / Media
    // shelf; it always stays in All.
    bool moved_leave_shelf = true;

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
};

LibraryPrefs load_prefs(const std::string &state_dir);
// Atomic; false when state_dir is empty or unwritable.
bool save_prefs(const std::string &state_dir, const LibraryPrefs &prefs);

} // namespace hui::orion
