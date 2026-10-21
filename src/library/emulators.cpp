// ps5-homebrew-ui - Emulated systems: the emulators' games as library entries.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/emulators.hpp"

#include "core/save_file.hpp"
#include "library/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include <sys/stat.h>

namespace hui::library
{

namespace
{

constexpr std::size_t kMaxInfo = 64u << 10;
constexpr std::size_t kMaxRomsPerSystem = 5000;
constexpr std::size_t kMaxPlaylist = 16u << 20; // a few thousand entries is a few megabytes

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

bool is_folder(const std::string &path)
{
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
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

std::string retroarch_args(const std::string &core_file, const std::string &rom,
                           const std::string &token)
{
    return "# ORION " + token + ": this start only\n-L\n/app0/cores/" + core_file + "\n" + rom +
           "\n";
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

// The games in one folder of one system, added to its System (made when it
// has none yet). dir is the folder as ORION sees it, seen as RetroArch does
// (its own content is /app0/content/... to it; anything else is the same
// path). Games already listed (the same file) are skipped.
static int add_system_folder(Emulator &emulator, const std::string &name, const std::string &dir,
                             const std::string &seen, std::vector<Title> *roms)
{
    auto at = std::find_if(emulator.systems.begin(), emulator.systems.end(),
                           [&](const System &s) { return s.name == name; });
    if (at == emulator.systems.end())
    {
        System system;
        system.name = name;
        system.short_name = short_system_name(name);
        for (const Core &core : emulator.cores)
        {
            if (std::find(core.databases.begin(), core.databases.end(), name) !=
                core.databases.end())
                system.cores.push_back(core.file);
        }
        emulator.systems.push_back(std::move(system));
        at = emulator.systems.end() - 1;
    }
    System &system = *at;
    std::vector<Title> found;
    for (const save::DirectoryEntry &entry : save::list_directory(dir))
    {
        if (entry.directory || entry.name.front() == '.' ||
            system.games + static_cast<int>(found.size()) >= static_cast<int>(kMaxRomsPerSystem))
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
                                   std::find(system.cores.begin(), system.cores.end(), core.file) !=
                                       system.cores.end();
                        });
        if (!taken || ext.empty())
            continue;
        Title rom;
        rom.rom = seen + "/" + entry.name;
        rom.id = rom_id(rom.rom);
        if (std::any_of(roms->begin(), roms->end(), [&](const Title &t) { return t.id == rom.id; }))
            continue;
        rom.name = rom_display_name(entry.name);
        rom.system = system.name;
        const EmulatorApp *retroarch = emulator.app("retroarch");
        rom.launcher = retroarch != nullptr ? retroarch->title_id : std::string();
        rom.category = Category::game;
        rom.platform = Platform::unknown;
        rom.source = Source::image;
        rom.path = dir + "/" + entry.name;
        found.push_back(std::move(rom));
    }
    std::sort(found.begin(), found.end(),
              [](const Title &a, const Title &b) { return a.name < b.name; });
    system.games += static_cast<int>(found.size());
    const int added = static_cast<int>(found.size());
    for (Title &rom : found)
        roms->push_back(std::move(rom));
    // A system with nothing to play has no tab.
    if (system.games == 0)
        emulator.systems.erase(at);
    return added;
}

static void sort_systems(Emulator &emulator)
{
    std::sort(emulator.systems.begin(), emulator.systems.end(),
              [](const System &a, const System &b) { return a.short_name < b.short_name; });
}

Emulator scan_retroarch(const Title &retroarch, std::vector<Title> *roms)
{
    Emulator emulator;
    EmulatorApp app;
    app.key = "retroarch";
    app.name = "RetroArch";
    app.title_id = retroarch.id;
    app.root = retroarch.path;
    app.starts_games = true;
    if (is_folder(retroarch.path + "/content"))
        app.folders.push_back(retroarch.path + "/content");
    emulator.apps.push_back(std::move(app));

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
        if (folder.directory && folder.name.front() != '.')
            add_system_folder(emulator, folder.name, retroarch.path + "/content/" + folder.name,
                              "/app0/content/" + folder.name, roms);
    }
    sort_systems(emulator);
    return emulator;
}

std::string system_of_folder(std::string_view name, const Emulator &emulator)
{
    std::string folded(name);
    for (char &c : folded)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const ShortName &entry : kShortNames)
    {
        std::string database(entry.database);
        std::string short_name(entry.name);
        for (char &c : database)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char &c : short_name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (folded == database || folded == entry.esde || folded == short_name)
            return entry.database;
    }
    // A system a core plays that the table does not know, by its full name.
    for (const std::string &system : known_systems(emulator))
    {
        std::string database = system;
        for (char &c : database)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (folded == database)
            return system;
    }
    return "";
}

std::vector<std::string> known_systems(const Emulator &emulator)
{
    std::vector<std::string> systems;
    for (const Core &core : emulator.cores)
    {
        for (const std::string &database : core.databases)
        {
            if (std::find(systems.begin(), systems.end(), database) == systems.end())
                systems.push_back(database);
        }
    }
    std::sort(systems.begin(), systems.end(), [](const std::string &a, const std::string &b)
              { return short_system_name(a) < short_system_name(b); });
    return systems;
}

int scan_rom_folder(Emulator &emulator, const std::string &path, const std::string &system,
                    std::vector<Title> *roms)
{
    std::string folder = path;
    while (folder.size() > 1 && folder.back() == '/')
        folder.pop_back();
    const std::size_t slash = folder.rfind('/');
    const std::string name = slash == std::string::npos ? folder : folder.substr(slash + 1);
    const std::string own = !system.empty() ? system : system_of_folder(name, emulator);
    int added = 0;
    if (!own.empty())
        added = add_system_folder(emulator, own, folder, folder, roms);
    else
    {
        // A folder of system folders, as ES-DE keeps them: roms/gba, roms/nds.
        for (const save::DirectoryEntry &entry : save::list_directory(folder))
        {
            if (!entry.directory || entry.name.front() == '.')
                continue;
            const std::string inner = system_of_folder(entry.name, emulator);
            if (!inner.empty())
                added += add_system_folder(emulator, inner, folder + "/" + entry.name,
                                           folder + "/" + entry.name, roms);
        }
    }
    sort_systems(emulator);
    return added;
}

namespace
{

constexpr KnownEmulator kKnownEmulators[] = {
    {"retroarch", "RetroArch", "Every system its cores play"},
    {"porpoise", "Porpoise", "GameCube, Wii"},
    {"ps5sx2", "PS5SX2", "PS2"},
};
constexpr const char *kGameCube = "Nintendo - GameCube";
constexpr const char *kWii = "Nintendo - Wii";
constexpr const char *kPs2 = "Sony - PlayStation 2";
constexpr std::size_t kDiscHead = 1024; // enough for every header read_nintendo_disc knows

std::uint32_t big32(std::string_view bytes, std::size_t at)
{
    if (at + 4 > bytes.size())
        return 0;
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 24 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3]));
}

// The header GameCube and Wii discs start with: the game's ID, a magic word
// that says which console, and the title.
bool disc_header(std::string_view head, std::size_t at, Disc *disc)
{
    if (head.size() < at + 0x60)
        return false;
    if (big32(head, at + 0x1c) == 0xC2339F3Du)
        disc->system = kGameCube;
    else if (big32(head, at + 0x18) == 0x5D1C9EA3u)
        disc->system = kWii;
    else
        return false;
    disc->id.clear();
    for (std::size_t i = 0; i < 6; ++i)
    {
        const char c = head[at + i];
        if (std::isalnum(static_cast<unsigned char>(c)) == 0)
        {
            disc->id.clear();
            break;
        }
        disc->id += c;
    }
    const std::string_view name = head.substr(at + 0x20, 0x40);
    disc->title = trim(name.substr(0, std::min(name.find('\0'), name.size())));
    // Some titles are Shift-JIS; a name that is not plain text is left out.
    if (std::any_of(disc->title.begin(), disc->title.end(),
                    [](char c) {
                        return static_cast<unsigned char>(c) < 0x20 ||
                               static_cast<unsigned char>(c) > 0x7e;
                    }))
        disc->title.clear();
    return true;
}

std::string read_head(const std::string &file)
{
    std::string head(kDiscHead, '\0');
    std::FILE *in = std::fopen(file.c_str(), "rb");
    if (in == nullptr)
        return "";
    const std::size_t got = std::fread(head.data(), 1, head.size(), in);
    std::fclose(in);
    head.resize(got);
    return head;
}

bool porpoise_takes(const std::string &ext)
{
    for (const char *known : {"iso", "gcm", "rvz", "wia", "wbfs", "ciso", "gcz"})
    {
        if (ext == known)
            return true;
    }
    return false;
}

bool ps2_takes(const std::string &ext)
{
    for (const char *known : {"iso", "chd", "cso", "zso"})
    {
        if (ext == known)
            return true;
    }
    return false;
}

// The folders an emulator looks in by itself, as ORION sees them.
std::vector<std::string> own_folders(const std::string &key, const std::string &root)
{
    if (key == "porpoise")
        return {root + "/porpoise/games", "/data/porpoise/games", "/data/games", "/data/roms",
                "/data/iso"};
    if (key != "ps5sx2")
        return {};
    std::vector<std::string> folders = {"/data/PCSX2/games", "/data/PCSX2"};
    for (const char *drive : {"/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/ext0"})
    {
        folders.push_back(std::string(drive) + "/PS2");
        folders.push_back(std::string(drive) + "/PCSX2/games");
    }
    return folders;
}

// The file as the emulator sees it: its own folder is /app0 to it.
std::string seen_by(const EmulatorApp &app, const std::string &path)
{
    if (!app.root.empty() && path.size() > app.root.size() &&
        path.compare(0, app.root.size(), app.root) == 0 && path[app.root.size()] == '/')
        return "/app0" + path.substr(app.root.size());
    return path;
}

// Games found, sorted by name, onto their systems (made when there is none
// yet, with the RetroArch cores that play it) and appended to *roms.
int place_games(Emulator &emulator, std::vector<Title> &found, std::vector<Title> *roms)
{
    std::sort(found.begin(), found.end(),
              [](const Title &a, const Title &b) { return a.name < b.name; });
    int added = 0;
    for (Title &rom : found)
    {
        auto at = std::find_if(emulator.systems.begin(), emulator.systems.end(),
                               [&](const System &s) { return s.name == rom.system; });
        if (at == emulator.systems.end())
        {
            System made;
            made.name = rom.system;
            made.short_name = short_system_name(rom.system);
            for (const Core &core : emulator.cores)
            {
                if (std::find(core.databases.begin(), core.databases.end(), made.name) !=
                    core.databases.end())
                    made.cores.push_back(core.file);
            }
            emulator.systems.push_back(std::move(made));
            at = emulator.systems.end() - 1;
        }
        if (at->games >= static_cast<int>(kMaxRomsPerSystem))
            continue;
        ++at->games;
        ++added;
        roms->push_back(std::move(rom));
    }
    return added;
}

// One folder's discs for Porpoise or PS5SX2 (system: the player's choice, or
// empty to tell from each disc). Games already listed are skipped.
int add_app_folder(Emulator &emulator, std::size_t app_index, const std::string &dir,
                   const std::string &system, std::vector<Title> *roms)
{
    const EmulatorApp &app = emulator.apps[app_index];
    const bool porpoise = app.key == "porpoise";
    std::vector<Title> found;
    for (const save::DirectoryEntry &entry : save::list_directory(dir))
    {
        const std::string ext = extension_of(entry.name);
        if (entry.directory || entry.name.front() == '.' ||
            !(porpoise ? porpoise_takes(ext) : ps2_takes(ext)))
            continue;
        const std::string path = dir + "/" + entry.name;
        Title rom;
        rom.rom = seen_by(app, path);
        rom.id = rom_id(rom.rom);
        if (std::any_of(roms->begin(), roms->end(),
                        [&](const Title &t) { return t.id == rom.id; }) ||
            std::any_of(found.begin(), found.end(), [&](const Title &t) { return t.id == rom.id; }))
            continue;
        Disc disc;
        const bool nintendo = read_nintendo_disc(read_head(path), &disc);
        // A shared folder (/data/roms, /data/iso) holds both kinds of .iso.
        if (porpoise != nintendo)
            continue;
        rom.system = !system.empty() ? system : porpoise ? disc.system : std::string(kPs2);
        rom.name = !disc.title.empty() ? disc.title : rom_display_name(entry.name);
        // Porpoise keeps the covers it downloads by the disc's ID.
        if (porpoise && !disc.id.empty())
        {
            for (const std::string &covers :
                 {std::string("/data/porpoise/covers"), app.root + "/porpoise/covers"})
            {
                struct stat info = {};
                const std::string cover = covers + "/" + disc.id + ".png";
                if (rom.icon.empty() && ::stat(cover.c_str(), &info) == 0 && S_ISREG(info.st_mode))
                    rom.icon = cover;
            }
        }
        rom.launcher = app.title_id;
        rom.category = Category::game;
        rom.platform = Platform::unknown;
        rom.source = Source::image;
        rom.path = path;
        found.push_back(std::move(rom));
    }
    const int added = place_games(emulator, found, roms);
    emulator.apps[app_index].games += added;
    return added;
}

// A folder and the folders in it, one level down: a subfolder named for
// another system (roms/gba) is not this emulator's.
int add_app_tree(Emulator &emulator, std::size_t app_index, const std::string &path,
                 const std::string &system, std::vector<Title> *roms)
{
    int added = add_app_folder(emulator, app_index, path, system, roms);
    const std::vector<std::string> plays = known_systems(emulator, emulator.apps[app_index].key);
    for (const save::DirectoryEntry &entry : save::list_directory(path))
    {
        if (!entry.directory || entry.name.front() == '.')
            continue;
        std::string inner = system;
        const std::string named = system_of_folder(entry.name, emulator);
        if (!named.empty())
        {
            if (std::find(plays.begin(), plays.end(), named) == plays.end())
                continue;
            if (inner.empty())
                inner = named;
        }
        added += add_app_folder(emulator, app_index, path + "/" + entry.name, inner, roms);
    }
    return added;
}

} // namespace

std::span<const KnownEmulator> known_emulators()
{
    return kKnownEmulators;
}

std::string emulator_key(const Title &title)
{
    if (title.source != Source::folder)
        return "";
    if (is_retroarch(title))
        return "retroarch";
    const std::string name = lower(title.name);
    if (name.find("porpoise") != std::string::npos || name.find("dolphin") != std::string::npos ||
        is_folder(title.path + "/porpoise"))
        return "porpoise";
    if (name.find("ps5sx2") != std::string::npos || name.find("pcsx2") != std::string::npos)
        return "ps5sx2";
    return "";
}

bool read_nintendo_disc(std::string_view head, Disc *disc)
{
    *disc = Disc();
    // WIA and RVZ: the disc's type at 0x48 (1 GameCube, 2 Wii), a copy of its
    // header at 0x58.
    if (head.size() >= 0x58 && (head.substr(0, 4) == std::string_view("WIA\x01", 4) ||
                                head.substr(0, 4) == std::string_view("RVZ\x01", 4)))
    {
        if (disc_header(head, 0x58, disc))
            return true;
        const std::uint32_t type = big32(head, 0x48);
        if (type != 1 && type != 2)
            return false;
        disc->system = type == 1 ? kGameCube : kWii;
        return true;
    }
    // GCZ: a magic word, then the kind (0 GameCube, 1 Wii), little-endian.
    if (head.size() >= 8 && big32(head, 0) == 0x01C00BB1u)
    {
        disc->system = head[4] == 1 ? kWii : kGameCube;
        return true;
    }
    // WBFS keeps Wii discs only, its header a sector in.
    if (head.substr(0, 4) == "WBFS")
    {
        if (!disc_header(head, 0x200, disc))
            disc->system = kWii;
        return true;
    }
    // CISO: blocks of a disc, its header 32 KiB in; nearly always GameCube.
    if (head.substr(0, 4) == "CISO")
    {
        disc->system = kGameCube;
        return true;
    }
    return disc_header(head, 0, disc);
}

std::string porpoise_arguments(const std::string &title_id, const std::string &disc,
                               bool exit_after_game)
{
    // Its own tiles pass the title ID first, as a program's name.
    return title_id + "\n--rom\n" + disc + "\n" + (exit_after_game ? "--exit-after-game\n" : "");
}

std::string ps5sx2_launch_note(const std::string &game)
{
    return game + "\n";
}

std::vector<std::string> known_systems(const Emulator &emulator, std::string_view key)
{
    if (key == "porpoise")
        return {kGameCube, kWii};
    if (key == "ps5sx2")
        return {kPs2};
    return known_systems(emulator);
}

int read_retroarch_playlist(Emulator &emulator, std::string_view text, const std::string &name,
                            std::vector<Title> *roms)
{
    EmulatorApp *app = nullptr;
    for (EmulatorApp &candidate : emulator.apps)
    {
        if (candidate.key == "retroarch")
            app = &candidate;
    }
    json::Value root;
    if (app == nullptr || !json::parse(text, &root) || !root.is_object())
        return 0;
    const auto stem = [](std::string file)
    {
        const std::size_t slash = file.rfind('/');
        if (slash != std::string::npos)
            file.erase(0, slash + 1);
        const std::size_t dot = file.rfind('.');
        return dot == std::string::npos ? file : file.substr(0, dot);
    };
    // A core's file ("/app0/cores/snes9x_libretro.so") to the system it plays.
    const auto core_system = [&](const std::string &core_path)
    {
        const std::size_t slash = core_path.rfind('/');
        const std::string file =
            slash == std::string::npos ? core_path : core_path.substr(slash + 1);
        for (const Core &core : emulator.cores)
        {
            if (core.file == file && !core.databases.empty())
                return core.databases.front();
        }
        return std::string();
    };
    const std::string system = system_of_folder(stem(name), emulator);
    const std::string default_core = root["default_core_path"].string_or("");
    const std::string scanned = root["scan_content_dir"].string_or("");
    if (!scanned.empty() &&
        std::find(app->folders.begin(), app->folders.end(), scanned) == app->folders.end())
        app->folders.push_back(scanned);

    std::unordered_set<std::string> listed;
    for (const Title &rom : *roms)
        listed.insert(rom.id);
    std::vector<Title> found;
    for (const json::Value &item : root["items"].elements())
    {
        Title rom;
        rom.rom = item["path"].string_or("");
        if (rom.rom.empty() || rom.rom.size() > 1024)
            continue;
        rom.id = rom_id(rom.rom);
        if (!listed.insert(rom.id).second)
            continue;
        rom.system = system_of_folder(stem(item["db_name"].string_or("")), emulator);
        if (rom.system.empty())
            rom.system = core_system(item["core_path"].string_or(""));
        if (rom.system.empty())
            rom.system = system;
        // An archive's member ("game.zip#game.sfc") is RetroArch's to open.
        const std::string file = rom.rom.substr(0, rom.rom.find('#'));
        // The extension, where it names a system (.gba, .nds), before a core
        // that plays several (mGBA: Game Boy, Color and Advance).
        if (rom.system.empty())
            rom.system = system_of_folder(extension_of(rom.rom), emulator);
        if (rom.system.empty())
            rom.system = core_system(default_core);
        if (rom.system.empty())
            continue; // nothing says what plays it
        rom.path = file.rfind("/app0/", 0) == 0 ? app->root + file.substr(5) : file;
        // A game deleted since RetroArch listed it (its folder is there, the
        // file is not) is not listed; one on a drive not plugged in still is.
        {
            const std::size_t slash = rom.path.rfind('/');
            struct stat info = {};
            if (slash != std::string::npos && slash > 0 && is_folder(rom.path.substr(0, slash)) &&
                ::stat(rom.path.c_str(), &info) != 0)
                continue;
        }
        const std::string label = item["label"].string_or("");
        // Labels have no extension; the dot keeps rom_display_name from
        // taking one off ("Super Mario Bros. 3").
        rom.name = rom_display_name(label.empty() ? file.substr(file.rfind('/') + 1) : label + ".");
        rom.launcher = app->title_id;
        rom.category = Category::game;
        rom.platform = Platform::unknown;
        rom.source = Source::image;
        found.push_back(std::move(rom));
    }
    const int added = place_games(emulator, found, roms);
    app->games += added;
    return added;
}

std::vector<Player> players_for(const Emulator &emulator, const Title &game)
{
    std::vector<Player> players;
    if (!game.is_rom())
        return players;
    const EmulatorApp *found_by = emulator.app_of(game);
    // The file as another emulator sees it: its own folder is /app0 to it.
    const auto file_for = [&](const EmulatorApp &app)
    { return &app == found_by ? game.rom : seen_by(app, game.path); };
    const std::string ext = extension_of(game.path);
    if (const EmulatorApp *retroarch = emulator.app("retroarch"))
    {
        for (const Core &core : emulator.cores)
        {
            if (std::find(core.databases.begin(), core.databases.end(), game.system) ==
                core.databases.end())
                continue;
            Player player;
            player.key = "retroarch:" + core.file;
            player.label = "RetroArch · " + core.name;
            player.app = retroarch;
            player.core = core.file;
            player.rom = file_for(*retroarch);
            players.push_back(std::move(player));
        }
    }
    const auto standalone = [&](const char *key, const char *label, bool takes)
    {
        const EmulatorApp *app = emulator.app(key);
        if (app == nullptr || !takes)
            return;
        Player player;
        player.key = key;
        player.label = label;
        player.app = app;
        player.rom = file_for(*app);
        players.push_back(std::move(player));
    };
    standalone("porpoise", "Porpoise",
               (game.system == kGameCube || game.system == kWii) && porpoise_takes(ext));
    standalone("ps5sx2", "PS5SX2", game.system == kPs2 && ps2_takes(ext));
    // PS5SX2 reads its launch note after its jailbreak, where /data is /data.
    if (!players.empty() && players.back().key == "ps5sx2")
        players.back().rom = game.path;
    return players;
}

int scan_game_folder(Emulator &emulator, const GameFolder &folder, std::vector<Title> *roms)
{
    const std::string key = folder.emulator.empty() ? std::string("retroarch") : folder.emulator;
    for (std::size_t i = 0; i < emulator.apps.size(); ++i)
    {
        if (emulator.apps[i].key != key)
            continue;
        if (key == "retroarch")
        {
            const std::size_t before = roms->size();
            const int added = scan_rom_folder(emulator, folder.path, folder.system, roms);
            emulator.apps[i].games += static_cast<int>(roms->size() - before);
            return added;
        }
        std::string path = folder.path;
        while (path.size() > 1 && path.back() == '/')
            path.pop_back();
        const int added = add_app_tree(emulator, i, path, folder.system, roms);
        sort_systems(emulator);
        return added;
    }
    return 0;
}

Emulator scan_emulators(const std::vector<Title> &titles, const std::vector<GameFolder> &folders,
                        std::vector<Title> *roms)
{
    Emulator emulator;
    for (const Title &title : titles)
    {
        if (emulator_key(title) == "retroarch")
        {
            emulator = scan_retroarch(title, roms);
            emulator.apps.front().games = static_cast<int>(roms->size());
            // And what its playlists list; the history ones are only what was
            // played (builtin/content_history.lpl).
            for (const save::DirectoryEntry &entry :
                 save::list_directory(title.path + "/playlists"))
            {
                std::string text;
                if (entry.directory || extension_of(entry.name) != "lpl" ||
                    entry.name.rfind("content_", 0) == 0 ||
                    !save::read_file(title.path + "/playlists/" + entry.name, &text, kMaxPlaylist))
                    continue;
                read_retroarch_playlist(emulator, text, entry.name, roms);
            }
            break;
        }
    }
    for (const KnownEmulator &known : kKnownEmulators)
    {
        if (std::string_view(known.key) == "retroarch")
            continue;
        for (const Title &title : titles)
        {
            if (emulator.app(known.key) != nullptr || emulator_key(title) != known.key)
                continue;
            EmulatorApp app;
            app.key = known.key;
            app.name = known.name;
            app.title_id = title.id;
            app.root = title.path;
            app.systems = known_systems(emulator, known.key);
            app.starts_games = app.key == "porpoise" || app.key == "ps5sx2";
            for (const std::string &folder : own_folders(app.key, app.root))
            {
                if (is_folder(folder))
                    app.folders.push_back(folder);
            }
            emulator.apps.push_back(std::move(app));
            const std::size_t index = emulator.apps.size() - 1;
            const std::vector<std::string> own = emulator.apps[index].folders;
            for (const std::string &folder : own)
                add_app_tree(emulator, index, folder, "", roms);
        }
    }
    for (const GameFolder &folder : folders)
        scan_game_folder(emulator, folder, roms);
    if (EmulatorApp *retroarch = emulator.apps.empty() ? nullptr : &emulator.apps.front();
        retroarch != nullptr && retroarch->key == "retroarch")
        retroarch->systems = known_systems(emulator);
    sort_systems(emulator);
    return emulator;
}

int attach_emulator_covers(std::vector<Title> &roms, const std::string &pcsx2_dir,
                           const std::string &porpoise_dir)
{
    const auto is_file = [](const std::string &file)
    {
        struct stat info = {};
        return ::stat(file.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    };
    // PS5SX2's serials, by the file as ORION sees it (its paths are the same).
    std::unordered_map<std::string, std::string> serials;
    std::string cache;
    if (save::read_file(pcsx2_dir + "/chd-serials.txt", &cache, 4u << 20))
    {
        for (const std::string &line : split(cache, '\n', false))
        {
            const std::vector<std::string> fields = split(line, '\t', false);
            if (fields.size() >= 4 && !fields[3].empty())
                serials[fields[0]] = fields[3];
        }
    }
    int found = 0;
    for (Title &rom : roms)
    {
        if (!rom.is_rom() || !rom.icon.empty())
            continue;
        std::vector<std::string> candidates;
        if (rom.system == kPs2)
        {
            const auto serial = serials.find(rom.path);
            if (serial != serials.end())
            {
                candidates.push_back(pcsx2_dir + "/covers/" + serial->second + ".jpg");
                candidates.push_back(pcsx2_dir + "/covers/" + serial->second + ".png");
            }
        }
        else if (rom.system == kGameCube || rom.system == kWii)
        {
            Disc disc;
            if (read_nintendo_disc(read_head(rom.path), &disc) && !disc.id.empty())
                candidates.push_back(porpoise_dir + "/covers/" + disc.id + ".png");
        }
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

int add_emulator_games(Library &library, const std::vector<GameFolder> &folders,
                       const std::string &art_dir)
{
    std::erase_if(library.titles, [](const Title &title) { return title.is_rom(); });
    std::vector<Title> roms;
    library.emulator = scan_emulators(library.titles, folders, &roms);
    // The emulators' own covers first: high quality, and nothing to download.
    attach_emulator_covers(roms, "/data/PCSX2", "/data/porpoise");
    const EmulatorApp *retroarch = library.emulator.app("retroarch");
    attach_rom_art(roms, retroarch != nullptr ? retroarch->root : std::string(), art_dir);
    const int count = static_cast<int>(roms.size());
    for (Title &rom : roms)
        library.titles.push_back(std::move(rom));
    return count;
}

} // namespace hui::library
