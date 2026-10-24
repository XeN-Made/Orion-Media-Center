// ORION Media Center - The drives: how full each one is and how many titles it holds.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/drives.hpp"

#include "core/i18n.hpp"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

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

SpaceOf space_from_report(const std::string &report)
{
    std::unordered_map<std::string, DriveSpace> spaces;
    std::size_t at = 0;
    while (at < report.size())
    {
        std::size_t end = report.find('\n', at);
        if (end == std::string::npos)
            end = report.size();
        const std::string line = report.substr(at, end - at);
        at = end + 1;
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (second == std::string::npos)
            continue;
        char *rest = nullptr;
        DriveSpace space;
        space.capacity = std::strtoull(line.c_str() + first + 1, &rest, 10);
        space.free = std::strtoull(line.c_str() + second + 1, &rest, 10);
        if (space.capacity > 0 && space.free <= space.capacity)
            spaces[line.substr(0, first)] = space;
    }
    return [spaces = std::move(spaces)](const std::string &path, DriveSpace *space)
    {
        const auto found = spaces.find(path);
        if (found == spaces.end())
            return false;
        *space = found->second;
        return true;
    };
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
