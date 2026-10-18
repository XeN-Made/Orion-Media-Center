// ps5-homebrew-ui - The console's own library: what ShadowMountPlus can mount.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hui::library
{

// Which home-screen area the title declares (param.json applicationCategoryType,
// or the PS4 param.sfo CATEGORY).
enum class Category : std::uint8_t
{
    game,
    media,
    app, // anything else a title may declare
};

enum class Platform : std::uint8_t
{
    ps5,     // PPSA..., and anything with a param.json
    ps4,     // CUSA... with a param.sfo
    unknown, // an image whose ID prefix says nothing
};

// How the title reaches the console: an unpacked folder or a disk image that
// ShadowMountPlus mounts on demand.
enum class Source : std::uint8_t
{
    folder,
    image,
};

struct Title
{
    std::string id;      // "PPSA01234"
    std::string name;    // as the home screen shows it
    std::string version; // "01.000.010"; empty when unknown
    Category category = Category::game;
    Platform platform = Platform::unknown;
    Source source = Source::folder;
    std::string path; // the folder or image file it was found at
    std::string icon; // a PNG to show for it; empty when there is none
    // The icon's texture, once something has uploaded it (0 until then).
    std::uint32_t texture = 0;
};

// What the designs receive: the titles found at start-up.
struct Library
{
    std::vector<Title> titles;
    // True once a scan ran (an empty list then means "nothing installed").
    bool scanned = false;
};

struct ScanOptions
{
    // Folders whose children are titles, in priority order: the first copy of
    // a title ID wins, and a folder always wins over an image of the same ID.
    std::vector<std::string> roots;
    // ShadowMountPlus's icon cache: "<ID>-<size>-<stamp>-<n>.png" files, the
    // only artwork an image title has until it is mounted.
    std::string icon_cache;
    // Titles to leave out (ORION itself, for one).
    std::vector<std::string> exclude;
    // When set, receives one line per root that could be listed:
    // "<root>: <entries> entries, <titles> titles".
    std::vector<std::string> *report = nullptr;
};

// The places ShadowMountPlus scans by default on a console, and its cache.
ScanOptions console_defaults();

// Reads every root. Unreadable roots, folders without metadata and unknown
// files are skipped silently; the result is sorted by name.
std::vector<Title> scan(const ScanOptions &options);

// "PPSA01234" style: four capital letters and five digits.
bool is_title_id(std::string_view text);

// The parts of a title that come from its metadata file. Exposed for tests.
bool read_param_json(std::string_view text, Title *title);
bool read_param_sfo(std::string_view bytes, Title *title);
// The ID and display name an image's file name carries:
// "PPSA01234 - Some Title.ffpfsc" -> PPSA01234, "Some Title".
bool parse_image_name(std::string_view file_name, Title *title);

const char *category_name(Category category);
const char *platform_name(Platform platform);

} // namespace hui::library
