// ps5-homebrew-ui - Emulated systems: RetroArch's cores and content as library entries.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/emulators.hpp"

#include "core/save_file.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>

#include <sys/stat.h>

namespace hui::library
{

namespace
{

constexpr std::size_t kMaxInfo = 64u << 10;
constexpr std::size_t kMaxRomsPerSystem = 5000;

struct ShortName
{
    const char *database;
    const char *name;
    const char *esde; // ES-DE's folder for it (downloaded_media/<esde>/covers)
};
constexpr ShortName kShortNames[] = {
    {"Nintendo - Game Boy Advance", "GBA", "gba"},
    {"Nintendo - Game Boy Color", "GBC", "gbc"},
    {"Nintendo - Game Boy", "Game Boy", "gb"},
    {"Nintendo - Nintendo DS", "DS", "nds"},
    {"Nintendo - Nintendo 3DS", "3DS", "n3ds"},
    {"Nintendo - Nintendo 64", "N64", "n64"},
    {"Nintendo - Nintendo Entertainment System", "NES", "nes"},
    {"Nintendo - Family Computer Disk System", "FDS", "fds"},
    {"Nintendo - Super Nintendo Entertainment System", "SNES", "snes"},
    {"Nintendo - GameCube", "GameCube", "gc"},
    {"Nintendo - Wii", "Wii", "wii"},
    {"Nintendo - Virtual Boy", "Virtual Boy", "virtualboy"},
    {"Nintendo - Pokemon Mini", "Pokemon Mini", "pokemini"},
    {"Sony - PlayStation", "PS1", "psx"},
    {"Sony - PlayStation 2", "PS2", "ps2"},
    {"Sony - PlayStation Portable", "PSP", "psp"},
    {"Sega - Mega Drive - Genesis", "Genesis", "genesis"},
    {"Sega - Master System - Mark III", "Master System", "mastersystem"},
    {"Sega - Mega-CD - Sega CD", "Sega CD", "segacd"},
    {"Sega - Dreamcast", "Dreamcast", "dreamcast"},
    {"Sega - Game Gear", "Game Gear", "gamegear"},
    {"Sega - Saturn", "Saturn", "saturn"},
    {"Sega - 32X", "32X", "sega32x"},
    {"Sega - Naomi", "Naomi", "naomi"},
    {"Sega - SG-1000", "SG-1000", "sg-1000"},
    {"NEC - PC Engine - TurboGrafx 16", "PC Engine", "pcengine"},
    {"NEC - PC Engine SuperGrafx", "SuperGrafx", "supergrafx"},
    {"NEC - PC-FX", "PC-FX", "pcfx"},
    {"SNK - Neo Geo Pocket Color", "NGPC", "ngpc"},
    {"SNK - Neo Geo Pocket", "NGP", "ngp"},
    {"SNK - Neo Geo CD", "Neo Geo CD", "neogeocd"},
    {"FBNeo - Arcade Games", "Arcade", "fbneo"},
    {"The 3DO Company - 3DO", "3DO", "3do"},
    {"Bandai - WonderSwan Color", "WonderSwan C", "wonderswancolor"},
    {"Bandai - WonderSwan", "WonderSwan", "wonderswan"},
    {"Commodore - 64", "C64", "c64"},
    {"Commodore - Amiga", "Amiga", "amiga"},
};

std::string trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
        text.remove_suffix(1);
    return std::string(text);
}

std::vector<std::string> split(std::string_view text, char separator, bool lower)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string part = trim(text.substr(start, end - start));
        if (lower)
        {
            for (char &c : part)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (!part.empty())
            parts.push_back(std::move(part));
        start = end + 1;
    }
    return parts;
}

std::string extension_of(std::string_view file)
{
    const std::size_t dot = file.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= file.size())
        return "";
    std::string ext(file.substr(dot + 1));
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool takes(const Core &core, const std::string &ext)
{
    return std::find(core.extensions.begin(), core.extensions.end(), ext) != core.extensions.end();
}

} // namespace

bool is_retroarch(const Title &title)
{
    std::string name = title.name;
    for (char &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name.find("retroarch") == std::string::npos || title.source != Source::folder)
        return false;
    for (const save::DirectoryEntry &entry : save::list_directory(title.path))
    {
        if (entry.directory && entry.name == "cores")
            return true;
    }
    return false;
}

bool read_core_info(std::string_view text, const std::string &file, Core *core)
{
    Core out;
    out.file = file;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || line.empty() || line.front() == '#')
            continue;
        const std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (key == "corename")
            out.name = value;
        else if (key == "database")
            out.databases = split(value, '|', false);
        else if (key == "supported_extensions")
            out.extensions = split(value, '|', true);
    }
    if (out.databases.empty() || out.extensions.empty())
        return false;
    if (out.name.empty())
        out.name = file;
    *core = std::move(out);
    return true;
}

std::string rom_display_name(std::string_view file)
{
    const std::size_t dot = file.rfind('.');
    std::string name(dot == std::string_view::npos ? file : file.substr(0, dot));
    // Trailing tags of No-Intro and GoodTools names: "(USA)", "(En,Ja)", "[!]".
    for (;;)
    {
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        if (name.empty() || (name.back() != ')' && name.back() != ']'))
            break;
        const char open = name.back() == ')' ? '(' : '[';
        const std::size_t at = name.rfind(open);
        if (at == std::string::npos || at == 0)
            break;
        name.erase(at);
    }
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    // A release number in front, as scene sets name DS games: "3055 - ".
    if (name.size() > 7 && std::isdigit(static_cast<unsigned char>(name[0])) != 0 &&
        std::isdigit(static_cast<unsigned char>(name[1])) != 0 &&
        std::isdigit(static_cast<unsigned char>(name[2])) != 0 &&
        std::isdigit(static_cast<unsigned char>(name[3])) != 0 && name.compare(4, 3, " - ") == 0)
        name.erase(0, 7);
    return name.empty() ? std::string(file) : name;
}

std::string short_system_name(std::string_view database)
{
    for (const ShortName &entry : kShortNames)
    {
        if (database == entry.database)
            return entry.name;
    }
    const std::size_t dash = database.find(" - ");
    return std::string(dash == std::string_view::npos ? database : database.substr(dash + 3));
}

std::string esde_system_name(std::string_view database)
{
    for (const ShortName &entry : kShortNames)
    {
        if (database == entry.database)
            return entry.esde;
    }
    return "";
}

std::string thumbnail_name(std::string_view stem)
{
    std::string name(stem);
    for (char &c : name)
    {
        if (std::string_view("&*/:`<>?\\|\"").find(c) != std::string_view::npos)
            c = '_';
    }
    return name;
}

int attach_rom_art(std::vector<Title> &roms, const std::string &root, const std::string &art_dir)
{
    const auto is_file = [](const std::string &file)
    {
        struct stat info = {};
        return ::stat(file.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    };
    int found = 0;
    for (Title &rom : roms)
    {
        if (!rom.is_rom() || !rom.icon.empty())
            continue;
        const std::size_t slash = rom.rom.rfind('/');
        const std::string file = slash == std::string::npos ? rom.rom : rom.rom.substr(slash + 1);
        const std::size_t dot = file.rfind('.');
        const std::string stem = dot == std::string::npos || dot == 0 ? file : file.substr(0, dot);
        const std::string es = esde_system_name(rom.system);
        std::vector<std::string> candidates;
        if (!art_dir.empty())
            candidates.push_back(art_dir + "/" + rom.system + "/" + stem + ".png");
        candidates.push_back(root + "/thumbnails/" + rom.system + "/Named_Boxarts/" +
                             thumbnail_name(stem) + ".png");
        if (!es.empty())
            candidates.push_back(root + "/es-de/ES-DE/downloaded_media/" + es + "/covers/" + stem +
                                 ".png");
        for (const std::string &candidate : candidates)
        {
            if (is_file(candidate))
            {
                rom.icon = candidate;
                ++found;
                break;
            }
        }
    }
    return found;
}

std::string retroarch_args(const std::string &core_file, const std::string &rom)
{
    return "# ORION: this start only; put back when RetroArch is up\n-L\n/app0/cores/" + core_file +
           "\n" + rom + "\n";
}

std::string rom_id(std::string_view path)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (char c : path)
        hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    char text[24];
    std::snprintf(text, sizeof(text), "rom-%012llx",
                  static_cast<unsigned long long>(hash & 0xffffffffffffull));
    return text;
}

Emulator scan_retroarch(const Title &retroarch, std::vector<Title> *roms)
{
    Emulator emulator;
    emulator.title_id = retroarch.id;
    emulator.root = retroarch.path;

    // The cores and what they take. .info files sit beside the cores here;
    // info/ is read too, for ports that keep them apart.
    for (const char *dir : {"/cores", "/info"})
    {
        for (const save::DirectoryEntry &entry : save::list_directory(retroarch.path + dir))
        {
            const std::string &name = entry.name;
            if (entry.directory || name.size() < 6 ||
                name.compare(name.size() - 5, 5, ".info") != 0)
                continue;
            const std::string core_file = name.substr(0, name.size() - 5) + ".so";
            if (std::any_of(emulator.cores.begin(), emulator.cores.end(),
                            [&](const Core &c) { return c.file == core_file; }))
                continue;
            std::string text;
            Core core;
            if (save::read_file(retroarch.path + dir + "/" + name, &text, kMaxInfo) &&
                read_core_info(text, core_file, &core))
                emulator.cores.push_back(std::move(core));
        }
    }

    for (const save::DirectoryEntry &folder : save::list_directory(retroarch.path + "/content"))
    {
        if (!folder.directory || folder.name.front() == '.')
            continue;
        System system;
        system.name = folder.name;
        system.short_name = short_system_name(folder.name);
        for (const Core &core : emulator.cores)
        {
            if (std::find(core.databases.begin(), core.databases.end(), system.name) !=
                core.databases.end())
                system.cores.push_back(core.file);
        }
        const std::string dir = retroarch.path + "/content/" + folder.name;
        std::vector<Title> found;
        for (const save::DirectoryEntry &entry : save::list_directory(dir))
        {
            if (entry.directory || entry.name.front() == '.' || found.size() >= kMaxRomsPerSystem)
                continue;
            // With a core for the system, only what a core takes is a game;
            // without one, every file is listed (the core may come later).
            const std::string ext = extension_of(entry.name);
            const bool taken =
                system.cores.empty() ||
                std::any_of(emulator.cores.begin(), emulator.cores.end(),
                            [&](const Core &core)
                            {
                                return takes(core, ext) &&
                                       std::find(system.cores.begin(), system.cores.end(),
                                                 core.file) != system.cores.end();
                            });
            if (!taken || ext.empty())
                continue;
            Title rom;
            rom.rom = "/app0/content/" + folder.name + "/" + entry.name;
            rom.id = rom_id(rom.rom);
            rom.name = rom_display_name(entry.name);
            rom.system = system.name;
            rom.launcher = retroarch.id;
            rom.category = Category::game;
            rom.platform = Platform::unknown;
            rom.source = Source::image;
            rom.path = dir + "/" + entry.name;
            found.push_back(std::move(rom));
        }
        if (found.empty())
            continue;
        std::sort(found.begin(), found.end(),
                  [](const Title &a, const Title &b) { return a.name < b.name; });
        system.games = static_cast<int>(found.size());
        emulator.systems.push_back(std::move(system));
        for (Title &rom : found)
            roms->push_back(std::move(rom));
    }
    std::sort(emulator.systems.begin(), emulator.systems.end(),
              [](const System &a, const System &b) { return a.short_name < b.short_name; });
    return emulator;
}

} // namespace hui::library
