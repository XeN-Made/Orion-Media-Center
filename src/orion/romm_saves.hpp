// ORION Media Center - Saves between RomM and RetroArch: which save, and where RetroArch keeps it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "orion/romm.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hui::orion
{

// RetroArch names a game's saves after its file, without the extension (an
// archive's member: after the archive): "Moon Garden (USA).gba" saves as
// "Moon Garden (USA).srm" and "Moon Garden (USA).state1".
std::string retroarch_stem(std::string_view rom);

// "mgba_libretro.so" as RomM names the emulator a save came from: "mgba".
std::string retroarch_core_key(std::string_view core_file);

// RomM's id for a game it lists, from the game's download address
// (".../api/roms/1234/content/..."); 0 when it is none.
long long romm_id_from_url(std::string_view url);

// RomM's games by their file names (lowercase), for a game the console has
// under the same name.
std::unordered_map<std::string, long long> romm_ids_by_file(const std::vector<RommGame> &games);

// The newest save (state = false) or state of a game, preferring those the
// given emulator made ("mgba"); nullptr when there is none.
const RommSave *latest_romm_save(const std::vector<RommSave> &saves, long long rom_id,
                                 const std::string &emulator, bool state);

// Where a save's or a state's content is downloaded from.
std::string romm_save_content_url(const std::string &base, const RommSave &save);

// The newest of a game's save states in a folder (<stem>.state, .state1...,
// .state.auto), or empty.
std::string newest_state(const std::string &folder, const std::string &stem);

} // namespace hui::orion
