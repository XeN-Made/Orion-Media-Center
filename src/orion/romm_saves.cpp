// ORION Media Center - Saves between RomM and RetroArch: which save, and where RetroArch keeps it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/romm_saves.hpp"

#include "core/save_file.hpp"

#include <cctype>
#include <cstdlib>

#include <sys/stat.h>

namespace hui::orion
{

namespace
{

std::string lowered(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

std::string retroarch_stem(std::string_view rom)
{
    rom = rom.substr(0, rom.find('#'));
    const std::size_t slash = rom.rfind('/');
    if (slash != std::string_view::npos)
        rom = rom.substr(slash + 1);
    const std::size_t dot = rom.rfind('.');
    return std::string(dot == std::string_view::npos || dot == 0 ? rom : rom.substr(0, dot));
}

std::string retroarch_core_key(std::string_view core_file)
{
    const std::size_t slash = core_file.rfind('/');
    if (slash != std::string_view::npos)
        core_file = core_file.substr(slash + 1);
    const std::size_t libretro = core_file.find("_libretro");
    return lowered(core_file.substr(0, libretro));
}

long long romm_id_from_url(std::string_view url)
{
    const std::size_t at = url.find("/api/roms/");
    if (at == std::string_view::npos)
        return 0;
    return std::atoll(std::string(url.substr(at + 10)).c_str());
}

std::unordered_map<std::string, long long> romm_ids_by_file(const std::vector<RommGame> &games)
{
    std::unordered_map<std::string, long long> ids;
    for (const RommGame &game : games)
        ids.emplace(lowered(game.fs_name), game.id);
    return ids;
}

const RommSave *latest_romm_save(const std::vector<RommSave> &saves, long long rom_id,
                                 const std::string &emulator, bool state)
{
    const RommSave *best = nullptr;
    for (const RommSave &save : saves)
    {
        if (save.rom_id != rom_id || save.state != state)
            continue;
        // The emulator's own first (another core's save may not load), then
        // the newest: RomM's dates are ISO 8601 and compare as text.
        const bool mine = lowered(save.emulator) == emulator;
        const bool best_mine = best != nullptr && lowered(best->emulator) == emulator;
        if (best == nullptr || (mine && !best_mine) ||
            (mine == best_mine && save.updated > best->updated))
            best = &save;
    }
    return best;
}

std::string romm_save_content_url(const std::string &base, const RommSave &save)
{
    return base + (save.state ? "/api/states/" : "/api/saves/") + std::to_string(save.id) +
           "/content";
}

std::string newest_state(const std::string &folder, const std::string &stem)
{
    std::string newest;
    long long newest_time = -1;
    const std::string prefix = stem + ".state";
    for (const save::DirectoryEntry &entry : save::list_directory(folder))
    {
        if (entry.directory || entry.name.compare(0, prefix.size(), prefix) != 0)
            continue;
        const std::string path = folder + "/" + entry.name;
        struct stat info = {};
        if (::stat(path.c_str(), &info) != 0 || info.st_size <= 0)
            continue;
        if (static_cast<long long>(info.st_mtime) > newest_time)
        {
            newest_time = static_cast<long long>(info.st_mtime);
            newest = path;
        }
    }
    return newest;
}

} // namespace hui::orion
