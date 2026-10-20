// ps5-homebrew-ui - ORION's folders: counting and copying what they hold.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace hui::orion
{

// Copies the files under from into to, folders and all, making what is
// missing. Files already there are kept. Returns how many were copied.
int copy_tree(const std::string &from, const std::string &to);

// How many files a folder holds, in it and below; with a suffix, only those
// whose names end in it (".png").
int count_files(const std::string &folder, std::string_view suffix = "");

// Removes the files in a folder (not below it) whose names end in suffix.
// Returns how many went.
int remove_files(const std::string &folder, std::string_view suffix);

} // namespace hui::orion
