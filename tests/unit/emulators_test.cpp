// ps5-homebrew-ui - Tests: RetroArch's cores and content as library entries.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// tests/fixtures/retroarch is an invented RetroArch folder: three cores, two
// systems with games, one system with no core and one with only a file no
// core takes. Starfall Racer has RetroArch box art, Moon Garden an ES-DE
// cover, Comet Post none.

#include "library/emulators.hpp"
#include "library/library.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace hui
{
namespace
{

library::Title retroarch()
{
    library::ScanOptions options;
    options.roots = {std::string(HUI_SOURCE_DIR) + "/tests/fixtures/retroarch/homebrew"};
    const std::vector<library::Title> titles = library::scan(options);
    EXPECT_EQ(titles.size(), 1u);
    return titles.empty() ? library::Title() : titles.front();
}

TEST(Emulators, FindsRetroArchByNameAndCores)
{
    const library::Title title = retroarch();
    EXPECT_EQ(title.id, "PPSA90120");
    EXPECT_TRUE(library::is_retroarch(title));
    library::Title other = title;
    other.name = "Starfall Arcade";
    EXPECT_FALSE(library::is_retroarch(other));
}

TEST(Emulators, TurnsContentFoldersIntoSystemsAndGames)
{
    std::vector<library::Title> roms;
    const library::Emulator emulator = library::scan_retroarch(retroarch(), &roms);
    ASSERT_EQ(emulator.apps.size(), 1u);
    EXPECT_EQ(emulator.apps.front().key, "retroarch");
    EXPECT_EQ(emulator.apps.front().title_id, "PPSA90120");
    EXPECT_EQ(emulator.cores.size(), 3u);
    ASSERT_EQ(emulator.systems.size(), 2u); // Saturn and Game Boy have no games
    const library::System &ds = emulator.systems[0];
    const library::System &gba = emulator.systems[1];
    EXPECT_EQ(ds.short_name, "DS");
    EXPECT_EQ(ds.games, 1);
    EXPECT_EQ(gba.short_name, "GBA");
    EXPECT_EQ(gba.games, 2);
    EXPECT_EQ(gba.cores.size(), 2u); // mGBA and VBA Next both play it

    ASSERT_EQ(roms.size(), 3u);
    const library::Title *starfall = nullptr;
    for (const library::Title &rom : roms)
    {
        EXPECT_TRUE(rom.is_rom());
        EXPECT_TRUE(library::is_item_id(rom.id)) << rom.id;
        EXPECT_EQ(rom.launcher, "PPSA90120");
        EXPECT_EQ(rom.rom.rfind("/app0/content/", 0), 0u) << rom.rom;
        if (rom.name == "Starfall Racer")
            starfall = &rom;
    }
    ASSERT_NE(starfall, nullptr);
    EXPECT_EQ(starfall->system, "Nintendo - Game Boy Advance");
    EXPECT_EQ(starfall->rom, "/app0/content/Nintendo - Game Boy Advance/Starfall Racer (USA).gba");
    // The same file always gets the same ID.
    EXPECT_EQ(starfall->id, library::rom_id(starfall->rom));
}

TEST(Emulators, ReadsCoreInfo)
{
    library::Core core;
    ASSERT_TRUE(
        library::read_core_info("display_name = \"Nintendo - Game Boy Advance (mGBA)\"\n"
                                "supported_extensions = \"gb|GBC|gba\"\n"
                                "corename = \"mGBA\"\n"
                                "database = \"Nintendo - Game Boy|Nintendo - Game Boy Advance\"\n",
                                "mgba_libretro.so", &core));
    EXPECT_EQ(core.name, "mGBA");
    EXPECT_EQ(core.file, "mgba_libretro.so");
    ASSERT_EQ(core.databases.size(), 2u);
    EXPECT_EQ(core.databases[1], "Nintendo - Game Boy Advance");
    EXPECT_EQ(core.extensions, (std::vector<std::string>{"gb", "gbc", "gba"}));
    EXPECT_FALSE(library::read_core_info("corename = \"Nothing\"\n", "x.so", &core));
}

TEST(Emulators, FindsBoxArtWhereRetroArchAndEsDeKeepIt)
{
    const library::Title title = retroarch();
    std::vector<library::Title> roms;
    library::scan_retroarch(title, &roms);
    ASSERT_EQ(roms.size(), 3u);
    const auto icon_of = [&](const std::string &name)
    {
        for (const library::Title &rom : roms)
        {
            if (rom.name == name)
                return rom.icon;
        }
        return std::string("missing");
    };
    // No ORION art folder: RetroArch's thumbnails, then ES-DE's covers.
    EXPECT_EQ(library::attach_rom_art(roms, title.path, ""), 2);
    EXPECT_EQ(icon_of("Starfall Racer"),
              title.path +
                  "/thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/Starfall Racer (USA).png");
    EXPECT_EQ(icon_of("Moon Garden"),
              title.path + "/es-de/ES-DE/downloaded_media/nds/covers/Moon Garden (Japan).png");
    EXPECT_EQ(icon_of("Comet Post"), "");

    // ORION's own folder comes first, named after the ROM file itself.
    const std::filesystem::path art = std::filesystem::temp_directory_path() / "hui-emulators-art";
    std::filesystem::remove_all(art);
    const std::filesystem::path mine =
        art / "Nintendo - Game Boy Advance" / "Comet Post (Europe) (En,Fr,De) [!].png";
    std::filesystem::create_directories(mine.parent_path());
    std::filesystem::copy_file(
        title.path +
            "/thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/Starfall Racer (USA).png",
        mine);
    for (library::Title &rom : roms)
        rom.icon.clear();
    EXPECT_EQ(library::attach_rom_art(roms, title.path, art.string()), 3);
    EXPECT_EQ(icon_of("Comet Post"), mine.string());
    std::filesystem::remove_all(art);
}

TEST(Emulators, TellsRetroArchsLauncherWhichGame)
{
    // One argument per line, spaces and all: the launcher splits on lines.
    EXPECT_EQ(library::retroarch_args(
                  "mgba_libretro.so",
                  "/app0/content/Nintendo - Game Boy Advance/007 - Nightfire (UE) (M3) [!].gba",
                  "1792457611-42"),
              "# ORION 1792457611-42: this start only\n-L\n"
              "/app0/cores/mgba_libretro.so\n"
              "/app0/content/Nintendo - Game Boy Advance/007 - Nightfire (UE) (M3) [!].gba\n");
}

TEST(Emulators, ReadsThePlayersOwnRomFolders)
{
    const library::Title title = retroarch();
    std::vector<library::Title> roms;
    library::Emulator emulator = library::scan_retroarch(title, &roms);
    const std::size_t content = roms.size(); // RetroArch's own: two GBA games, one DS game

    // An ES-DE style folder of system folders on a "drive".
    const std::filesystem::path drive = std::filesystem::temp_directory_path() / "hui-rom-drive";
    std::filesystem::remove_all(drive);
    const auto touch = [](const std::filesystem::path &file)
    {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file) << "x";
    };
    touch(drive / "roms/gba/Lunar Lift (USA).gba");
    touch(drive / "roms/gba/notes.txt"); // no core takes .txt
    touch(drive / "roms/nds/Paper Moon (Europe).nds");
    touch(drive / "roms/unknown-system/whatever.bin"); // not a system: ignored
    EXPECT_EQ(library::scan_rom_folder(emulator, (drive / "roms").string(), "", &roms), 2);
    ASSERT_EQ(roms.size(), content + 2);
    const library::Title &lunar = roms[content];
    EXPECT_EQ(lunar.name, "Lunar Lift");
    EXPECT_EQ(lunar.system, "Nintendo - Game Boy Advance");
    // RetroArch reads it where it is: the same path, not one in its own folder.
    EXPECT_EQ(lunar.rom, (drive / "roms/gba/Lunar Lift (USA).gba").string());
    EXPECT_EQ(lunar.launcher, title.id);
    for (const library::System &system : emulator.systems)
    {
        if (system.short_name == "GBA")
            EXPECT_EQ(system.games, 3);
    }

    // The same folder again adds nothing; a single system's folder by its
    // short name works too, and a system can be named outright.
    EXPECT_EQ(library::scan_rom_folder(emulator, (drive / "roms").string(), "", &roms), 0);
    touch(drive / "more/Starfall 2 (USA).gba");
    EXPECT_EQ(library::scan_rom_folder(emulator, (drive / "more").string(),
                                       "Nintendo - Game Boy Advance", &roms),
              1);
    EXPECT_EQ(library::system_of_folder("GBA", emulator), "Nintendo - Game Boy Advance");
    EXPECT_EQ(library::system_of_folder("nds", emulator), "Nintendo - Nintendo DS");
    EXPECT_EQ(library::system_of_folder("Nintendo - Nintendo DS", emulator),
              "Nintendo - Nintendo DS");
    EXPECT_EQ(library::system_of_folder("photos", emulator), "");
    std::filesystem::remove_all(drive);
}

TEST(Emulators, NamesThumbnailsAsRetroArchDoes)
{
    EXPECT_EQ(library::thumbnail_name("Ratchet & Clank: Going Commando (USA)"),
              "Ratchet _ Clank_ Going Commando (USA)");
    EXPECT_EQ(library::thumbnail_name("Plain (Europe)"), "Plain (Europe)");
    EXPECT_EQ(library::esde_system_name("Nintendo - Nintendo DS"), "nds");
    EXPECT_EQ(library::esde_system_name("Atari - Lynx"), "");
}

TEST(Emulators, CleansNamesAndShortensSystems)
{
    EXPECT_EQ(library::rom_display_name("007 - Nightfire (UE) (M3) [!].gba"), "007 - Nightfire");
    EXPECT_EQ(library::rom_display_name("Comet Post (Europe) (En,Fr,De).gba"), "Comet Post");
    EXPECT_EQ(library::rom_display_name("Plain.nds"), "Plain");
    EXPECT_EQ(library::rom_display_name("3055 - Chrono Trigger (U)(XenoPhobia).nds"),
              "Chrono Trigger");
    EXPECT_EQ(library::rom_display_name("1942 (Japan).nes"), "1942"); // a title, not a number
    EXPECT_EQ(library::rom_display_name("(Only Tags).iso"), "(Only Tags)");
    EXPECT_EQ(library::short_system_name("Nintendo - Game Boy Advance"), "GBA");
    EXPECT_EQ(library::short_system_name("Sony - PlayStation 2"), "PS2");
    EXPECT_EQ(library::short_system_name("Atari - Lynx"), "Lynx");
    EXPECT_EQ(library::short_system_name("ScummVM"), "ScummVM");
    EXPECT_TRUE(library::is_item_id("rom-0123456789ab"));
    EXPECT_FALSE(library::is_item_id("rom-0123456789aB"));
    EXPECT_FALSE(library::is_item_id("rom-123"));
}

// A GameCube or Wii disc header at `at` in a kilobyte of zeros.
std::string disc_head(std::size_t at, const std::string &id, const std::string &title, bool wii)
{
    std::string head(1024, '\0');
    head.replace(at, id.size(), id);
    const std::size_t magic = at + (wii ? 0x18 : 0x1c);
    const char gamecube_magic[] = {'\xC2', '\x33', '\x9F', '\x3D'};
    const char wii_magic[] = {'\x5D', '\x1C', '\x9E', '\xA3'};
    head.replace(magic, 4, wii ? wii_magic : gamecube_magic, 4);
    head.replace(at + 0x20, title.size(), title);
    return head;
}

TEST(Emulators, ReadsNintendoDiscHeaders)
{
    library::Disc disc;
    ASSERT_TRUE(library::read_nintendo_disc(disc_head(0, "GQBE01", "Quiet Bay", false), &disc));
    EXPECT_EQ(disc.id, "GQBE01");
    EXPECT_EQ(disc.title, "Quiet Bay");
    EXPECT_EQ(disc.system, "Nintendo - GameCube");

    // RVZ (and WIA) keep a copy of the header at 0x58.
    std::string rvz = disc_head(0x58, "RLNP01", "Lantern Night", true);
    rvz.replace(0, 4, std::string("RVZ\x01", 4));
    ASSERT_TRUE(library::read_nintendo_disc(rvz, &disc));
    EXPECT_EQ(disc.id, "RLNP01");
    EXPECT_EQ(disc.title, "Lantern Night");
    EXPECT_EQ(disc.system, "Nintendo - Wii");

    // GCZ says which console only; WBFS holds Wii discs.
    std::string gcz(64, '\0');
    gcz.replace(0, 4, std::string("\x01\xC0\x0B\xB1", 4));
    ASSERT_TRUE(library::read_nintendo_disc(gcz, &disc));
    EXPECT_EQ(disc.system, "Nintendo - GameCube");
    EXPECT_TRUE(disc.id.empty());
    gcz[4] = 1;
    ASSERT_TRUE(library::read_nintendo_disc(gcz, &disc));
    EXPECT_EQ(disc.system, "Nintendo - Wii");
    ASSERT_TRUE(library::read_nintendo_disc(std::string("WBFS") + std::string(64, '\0'), &disc));
    EXPECT_EQ(disc.system, "Nintendo - Wii");

    // A PS2 disc (ISO 9660) is none of them.
    EXPECT_FALSE(library::read_nintendo_disc(std::string(1024, '\0'), &disc));
    EXPECT_FALSE(library::read_nintendo_disc("", &disc));
}

TEST(Emulators, PorpoiseTakesItsGameAsItsTilesGiveIt)
{
    EXPECT_EQ(library::porpoise_arguments("PPSA90130", "/data/games/Quiet Bay.rvz", false),
              "PPSA90130\n--rom\n/data/games/Quiet Bay.rvz\n");
    EXPECT_EQ(library::porpoise_arguments("PPSA90130", "/app0/porpoise/games/a.iso", true),
              "PPSA90130\n--rom\n/app0/porpoise/games/a.iso\n--exit-after-game\n");
}

library::Title app_title(const std::string &id, const std::string &name, const std::string &path)
{
    library::Title title;
    title.id = id;
    title.name = name;
    title.path = path;
    title.source = library::Source::folder;
    return title;
}

void write_file(const std::filesystem::path &path, const std::string &bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

TEST(Emulators, FindsPorpoiseAndPs5sx2AndTheirGames)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hui-emulators-test";
    fs::remove_all(root);
    const fs::path porpoise = root / "PPSA90130";
    const fs::path ps5sx2 = root / "PPSA90131";
    fs::create_directories(ps5sx2);
    write_file(porpoise / "porpoise/games/Quiet Bay (Europe).iso",
               disc_head(0, "GQBP01", "Quiet Bay", false));
    std::string rvz = disc_head(0x58, "RLNP01", "Lantern Night", true);
    rvz.replace(0, 4, std::string("RVZ\x01", 4));
    write_file(porpoise / "porpoise/games/wii/lantern.rvz", rvz);
    write_file(porpoise / "porpoise/games/notes.txt", "not a disc");
    write_file(porpoise / "porpoise/games/gba/Starfall.iso", disc_head(0, "GSFE01", "X", false));
    // The player's own folder for PS5SX2, beside a GameCube disc it must not take.
    write_file(root / "drive/PS2/Paper Tiger (USA).iso", std::string(1024, '\0'));
    write_file(root / "drive/PS2/Cube.iso", disc_head(0, "GCBE01", "Cube", false));
    write_file(root / "drive/PS2/Tower.chd", "chd");

    const std::vector<library::Title> titles = {
        app_title("PPSA90130", "Porpoise (DolphinPS5)", porpoise.string()),
        app_title("PPSA90131", "PS5SX2", ps5sx2.string()),
        app_title("PPSA90132", "Starfall Arcade", (root / "other").string()),
    };
    EXPECT_EQ(library::emulator_key(titles[0]), "porpoise");
    EXPECT_EQ(library::emulator_key(titles[1]), "ps5sx2");
    EXPECT_EQ(library::emulator_key(titles[2]), "");

    std::vector<library::Title> roms;
    const library::Emulator emulator =
        library::scan_emulators(titles, {{(root / "drive/PS2").string(), "", "ps5sx2"}}, &roms);
    ASSERT_EQ(emulator.apps.size(), 2u);
    const library::EmulatorApp *dolphin = emulator.app("porpoise");
    const library::EmulatorApp *sx2 = emulator.app("ps5sx2");
    ASSERT_NE(dolphin, nullptr);
    ASSERT_NE(sx2, nullptr);
    EXPECT_TRUE(dolphin->starts_games);
    EXPECT_FALSE(sx2->starts_games);
    EXPECT_EQ(dolphin->games, 2); // the gba folder is another system's
    EXPECT_EQ(sx2->games, 2);     // the GameCube disc is Porpoise's kind
    ASSERT_FALSE(dolphin->folders.empty());
    EXPECT_EQ(dolphin->folders.front(), (porpoise / "porpoise/games").string());

    std::vector<std::string> systems;
    for (const library::System &system : emulator.systems)
        systems.push_back(system.short_name);
    EXPECT_EQ(systems, (std::vector<std::string>{"GameCube", "PS2", "Wii"}));

    const library::Title *quiet = nullptr;
    const library::Title *lantern = nullptr;
    const library::Title *paper = nullptr;
    for (const library::Title &rom : roms)
    {
        if (rom.name == "Quiet Bay")
            quiet = &rom;
        if (rom.name == "Lantern Night")
            lantern = &rom;
        if (rom.name == "Paper Tiger")
            paper = &rom;
    }
    ASSERT_NE(quiet, nullptr);
    ASSERT_NE(lantern, nullptr);
    ASSERT_NE(paper, nullptr);
    // Porpoise's own folder is /app0 to it; names come from the discs.
    EXPECT_EQ(quiet->rom, "/app0/porpoise/games/Quiet Bay (Europe).iso");
    EXPECT_EQ(quiet->system, "Nintendo - GameCube");
    EXPECT_EQ(quiet->launcher, "PPSA90130");
    EXPECT_EQ(lantern->system, "Nintendo - Wii");
    EXPECT_EQ(paper->system, "Sony - PlayStation 2");
    EXPECT_EQ(paper->launcher, "PPSA90131");
    EXPECT_EQ(paper->rom, (root / "drive/PS2/Paper Tiger (USA).iso").string());
    EXPECT_EQ(emulator.app_of(*paper), sx2);

    // Scanning again into a library replaces the games, never doubles them.
    library::Library library;
    library.titles = titles;
    EXPECT_EQ(library::add_emulator_games(library, {}, (root / "art").string()), 2);
    EXPECT_EQ(library::add_emulator_games(library, {}, (root / "art").string()), 2);
    EXPECT_EQ(library.titles.size(), titles.size() + 2u);
    fs::remove_all(root);
}

} // namespace
} // namespace hui
