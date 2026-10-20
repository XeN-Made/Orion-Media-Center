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
    EXPECT_EQ(emulator.title_id, "PPSA90120");
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

} // namespace
} // namespace hui
