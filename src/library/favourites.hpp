// ps5-homebrew-ui - The titles the player starred, kept beside ORION's other state.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace hui::library
{

// <state_dir>/favourites.json holds {"favourites": ["PPSA01234", ...]}: plain
// enough to edit by hand. Unknown keys and anything that is not a title ID
// are ignored; a missing or broken file is an empty list.
std::vector<std::string> load_favourites(const std::string &state_dir);

// Replaces the file atomically. False when state_dir is empty or unwritable.
bool save_favourites(const std::string &state_dir, const std::vector<std::string> &ids);

} // namespace hui::library
