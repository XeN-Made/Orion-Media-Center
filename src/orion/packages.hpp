// ORION Media Center - PS4 and PS5 packages (.pkg): what one is, and whether it is installed.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>

namespace hui::orion
{

// The first bytes of a .pkg read for what it holds. Every package names its
// content by an ID of one shape, "UP0000-PPSA08560_00-FULLGAMEUNLOCK00": the
// publisher, the game's title ID, and a label (the add-on's, or the game's
// own). It lies in the PS4 header ("\x7fCNT", at 0x40), in the PS4 header
// inside a PS5 package ("\x7fFIH"), and in a package in parts ("PS5MPKG1").
inline constexpr std::size_t kPackageHead = 64 * 1024; // bytes read to find it

enum class PackageKind : unsigned char
{
    none, // not a package
    ps4,
    ps5,
    parts, // a PS5 package in several files
};

struct PackageId
{
    PackageKind kind = PackageKind::none;
    std::string content_id; // "UP0000-PPSA08560_00-FULLGAMEUNLOCK00"
    std::string title_id;   // "PPSA08560"
    std::string label;      // "FULLGAMEUNLOCK00"
};

// The first content ID in some bytes (a package's head, a param.json, a
// param.sfo), or empty.
std::string find_content_id(std::string_view bytes);

// What a package's first bytes (kPackageHead of them, or fewer) say it is.
bool read_package(std::string_view head, PackageId *id);

// What the console has: each app's content ID (from its param.json or
// param.sfo, as ORION's helper copies them into <state>/system-apps) and
// the add-ons installed (<state>/system-apps/addcont.txt).
struct Installed
{
    std::map<std::string, std::string> apps; // title ID -> content ID
    std::set<std::string> addcont;           // "<title ID>/<label>"
};
Installed read_installed(const std::string &state_dir);

enum class PackageState : unsigned char
{
    unknown,       // not a package, or nothing is known yet
    installed,     // the game itself, or this add-on
    not_installed, // its game is here, it is not (an add-on or an update)
    no_game,       // neither it nor its game is on the console
};
PackageState package_state(const PackageId &id, const Installed &installed);

} // namespace hui::orion
