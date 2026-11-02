// ORION Media Center - Titles' sizes as the system keeps them.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The play-time helper (payload/playtime) copies each registered title's
// size from the system's list (app.db's tbl_contentinfo) to
// <state>/sizes.txt, "<ID>\t<bytes>" a line: installed packages and the
// folders and images ShadowMountPlus registered, none of them added up.
//
//   const auto sizes = orion::read_sizes(state);
//   if (const auto at = sizes.find("PPSA01234"); at != sizes.end()) ... at->second bytes

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hui::orion
{

// The lines of a sizes.txt; anything that is not "<ID>\t<bytes>" is left out.
std::unordered_map<std::string, std::uint64_t> parse_sizes(std::string_view text);
// <state_dir>/sizes.txt, or nothing before the helper first wrote it.
std::unordered_map<std::string, std::uint64_t> read_sizes(const std::string &state_dir);

} // namespace hui::orion
