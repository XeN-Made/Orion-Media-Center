// ORION Media Center - The drives: how full each one is and how many titles it holds.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/library.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::orion
{

enum class DriveKind : std::uint8_t
{
    console,  // the console's own storage (/data)
    extended, // M.2 or extended USB storage (/mnt/ext0, /mnt/ext1)
    usb,      // a USB drive (/mnt/usb0 ... /mnt/usb7)
};

struct DriveSpace
{
    std::uint64_t capacity = 0; // bytes
    std::uint64_t free = 0;     // bytes the player can still fill
};

// How full the file system holding path is; false when it cannot be asked
// (not mounted, not reachable).
using SpaceOf = std::function<bool(const std::string &path, DriveSpace *space)>;
// The space the launch helper wrote (payload/launcher, "@space"): a line per
// drive, "<mount>\t<bytes>\t<free bytes>". ORION cannot ask itself: inside
// its sandbox the system refuses statfs (EPERM).
SpaceOf space_from_report(const std::string &report);

struct Drive
{
    DriveKind kind = DriveKind::console;
    int number = 0;   // which of its kind, from 0 (usb0 is USB drive 1)
    std::string path; // where it is mounted
    DriveSpace space;
    int titles = 0; // of the library's titles, the ones kept on it (games for emulators included)
};

// The drive a title's folder, image or game file is on, as a mount point
// ("/mnt/usb1"), "/data" for the console's own storage, or empty when the path
// says nothing (a title without one).
std::string drive_of(const std::string &path);

// Every drive that answers, the console's storage first, then extended
// storage and the USB drives in order, each with the titles on it.
std::vector<Drive> find_drives(const std::vector<library::Title> &titles, const SpaceOf &space_of);

// A drive as ORION names it: "The console's storage", "Extended storage",
// "Extended storage 2", "USB drive 1" (usb0) ...; empty for no drive.
std::string drive_name(const std::string &root);

// A size as the drive rows show it: "512 MB", "312 GB", "1.8 TB" (powers of
// 1000, as drives are sold).
std::string format_bytes(std::uint64_t bytes);

} // namespace hui::orion
