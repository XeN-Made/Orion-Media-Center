// ORION Media Center - The drives: how full each one is and how many titles it holds.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/drives.hpp"

#include "core/i18n.hpp"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include <sys/statvfs.h>

namespace hui::orion
{

namespace
{

constexpr int kUsbDrives = 8;
constexpr int kExtendedDrives = 2;

bool starts_with(const std::string &path, const std::string &root)
{
    return path.size() >= root.size() && path.compare(0, root.size(), root) == 0 &&
           (path.size() == root.size() || path[root.size()] == '/');
}

} // namespace

bool statvfs_space(const std::string &path, DriveSpace *space)
{
    struct statvfs info = {};
    if (::statvfs(path.c_str(), &info) != 0 || info.f_blocks == 0)
        return false;
    const std::uint64_t unit = info.f_frsize != 0 ? info.f_frsize : info.f_bsize;
    space->capacity = static_cast<std::uint64_t>(info.f_blocks) * unit;
    space->free = static_cast<std::uint64_t>(info.f_bavail) * unit;
    return true;
}

std::string drive_of(const std::string &path)
{
    for (const char *kind : {"/mnt/usb", "/mnt/ext"})
    {
        const std::string prefix = kind;
        if (path.compare(0, prefix.size(), prefix) != 0 || path.size() <= prefix.size())
            continue;
        const std::size_t end = path.find('/', prefix.size());
        return path.substr(0, end);
    }
    // Everything else ORION lists lives on the console's own storage: its
    // homebrew folders, installed packages and the system's copies.
    for (const char *root : {"/data", "/user", "/system_ex", "/app0"})
    {
        if (starts_with(path, root))
            return "/data";
    }
    return "";
}

std::vector<Drive> find_drives(const std::vector<library::Title> &titles, const SpaceOf &space_of)
{
    std::vector<Drive> drives;
    const auto add = [&](DriveKind kind, int number, std::string path)
    {
        Drive drive;
        drive.kind = kind;
        drive.number = number;
        drive.path = std::move(path);
        if (space_of && space_of(drive.path, &drive.space) && drive.space.capacity > 0)
            drives.push_back(std::move(drive));
    };
    add(DriveKind::console, 0, "/data");
    for (int i = 0; i < kExtendedDrives; ++i)
        add(DriveKind::extended, i, "/mnt/ext" + std::to_string(i));
    for (int i = 0; i < kUsbDrives; ++i)
        add(DriveKind::usb, i, "/mnt/usb" + std::to_string(i));
    // Each title counts on the drive its folder, image or game file is on.
    std::unordered_map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < drives.size(); ++i)
        at.emplace(drives[i].path, i);
    for (const library::Title &title : titles)
    {
        const std::string root = drive_of(title.path);
        if (const auto found = at.find(root); found != at.end())
            ++drives[found->second].titles;
    }
    return drives;
}

std::string drive_name(const std::string &root)
{
    if (root == "/data")
        return i18n::tr("The console's storage");
    const auto number = [&](std::size_t at) { return std::atoi(root.c_str() + at) + 1; };
    if (root.rfind("/mnt/usb", 0) == 0 && root.size() > 8)
        return i18n::trf("USB drive {0}", {std::to_string(number(8))});
    if (root == "/mnt/ext0")
        return i18n::tr("Extended storage");
    if (root.rfind("/mnt/ext", 0) == 0 && root.size() > 8)
        return i18n::trf("Extended storage {0}", {std::to_string(number(8))});
    return "";
}

std::string format_bytes(std::uint64_t bytes)
{
    constexpr double kKilo = 1000.0;
    const double value = static_cast<double>(bytes);
    char text[32];
    if (value >= kKilo * kKilo * kKilo * kKilo)
        std::snprintf(text, sizeof(text), "%.1f TB", value / (kKilo * kKilo * kKilo * kKilo));
    else if (value >= kKilo * kKilo * kKilo)
        std::snprintf(text, sizeof(text), "%.0f GB", value / (kKilo * kKilo * kKilo));
    else
        std::snprintf(text, sizeof(text), "%.0f MB", value / (kKilo * kKilo));
    return text;
}

} // namespace hui::orion
