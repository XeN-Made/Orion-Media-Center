// ORION Media Center - A PS5 game copied off a network drive, or unpacked from an archive on it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "platform/ps5/smb.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace hui::sys
{

enum class GameSourceKind : std::uint8_t
{
    none,
    folder,  // a game's folder (sce_sys/param.json inside): copied whole
    file,    // an image ShadowMountPlus mounts (.ffpkg, .ffpfs, .exfat): copied
    archive, // a ZIP, 7z or RAR, in one file or several volumes: unpacked
};

struct GameSource
{
    SmbLogin login;
    GameSourceKind kind = GameSourceKind::none;
    // On the share: the folder, the file, or the archive's volumes in order.
    std::vector<std::string> paths;
};

struct CopyProgress
{
    std::atomic<std::uint64_t> done{0};  // bytes read off the share
    std::atomic<std::uint64_t> total{0}; // what there is to read (an archive's size)
    std::atomic<bool> cancel{false};
};

// The game copied, or unpacked, into the folder `into` (which exists): a
// folder's contents, the file by its own name, an archive's tree. Archives
// are read as they come off the network, so nothing but what they hold is
// written. Blocking: call it from a worker thread. On a PC: false.
bool copy_game(const GameSource &source, const std::string &into, CopyProgress *progress,
               std::string *error);

} // namespace hui::sys
