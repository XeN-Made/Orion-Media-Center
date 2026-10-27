// ORION Media Center - Tests: RetroArch's cores and content as library entries.
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

#include <cstdint>
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

TEST(Emulators, ListsEachRealFolderOnce)
{
    // On an exFAT drive ps1 and PS1 are one folder; a link stands in for that
    // here, where the filesystem keeps case.
    const std::filesystem::path drive = std::filesystem::temp_directory_path() / "orion-one-folder";
    std::filesystem::remove_all(drive);
    std::filesystem::create_directories(drive / "ps1");
    std::filesystem::create_directories(drive / "psx");
    std::filesystem::create_directory_symlink(drive / "ps1", drive / "PS1");
    const std::vector<std::string> kept = library::distinct_folders(
        {(drive / "ps1").string(), (drive / "PS1").string(), (drive / "psx").string(),
         (drive / "missing").string(), (drive / "ps1").string()});
    ASSERT_EQ(kept.size(), 2u);
    EXPECT_EQ(kept[0], (drive / "ps1").string());
    EXPECT_EQ(kept[1], (drive / "psx").string());
    std::filesystem::remove_all(drive);
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

TEST(Emulators, ListsWhatRetroArchsPlaylistsList)
{
    std::vector<library::Title> roms;
    library::Emulator emulator = library::scan_retroarch(retroarch(), &roms);
    const std::size_t before = roms.size();
    // A manual scan of a drive's folder, as RetroArch writes one: the
    // playlist's name says the system.
    const std::string gba = R"json({
      "version": "1.5",
      "default_core_path": "/app0/cores/mgba_libretro.so",
      "scan_content_dir": "/mnt/usb0/roms/gba",
      "items": [
        {"path": "/mnt/usb0/roms/gba/Super Mario Bros. 3 (U) [!].gba", "label": "Super Mario Bros. 3 (U) [!]",
         "core_path": "DETECT", "core_name": "DETECT", "db_name": "gba.lpl"},
        {"path": "/mnt/usb0/roms/gba/Rune Song.zip#Rune Song.gba", "label": "",
         "core_path": "DETECT", "core_name": "DETECT", "db_name": "gba.lpl"},
        {"path": "/app0/content/Nintendo - Game Boy Advance/Starfall Racer (USA).gba",
         "label": "Starfall Racer (USA)", "db_name": "gba.lpl"}
      ]})json";
    EXPECT_EQ(library::read_retroarch_playlist(emulator, gba, "gba.lpl", &roms),
              2); // one was listed
    // A playlist named for nothing: the entry's database, else the default core.
    const std::string mixed = R"json({
      "default_core_path": "/app0/cores/mgba_libretro.so",
      "items": [
        {"path": "/data/roms/Paper Moon.nds", "label": "Paper Moon (E)",
         "db_name": "Nintendo - Nintendo DS.lpl"},
        {"path": "/data/roms/Lantern.gba", "label": "Lantern", "db_name": "Favourites.lpl"}
      ]})json";
    EXPECT_EQ(library::read_retroarch_playlist(emulator, mixed, "Favourites.lpl", &roms), 2);
    EXPECT_EQ(library::read_retroarch_playlist(emulator, "not json", "x.lpl", &roms), 0);
    // A file deleted from a folder that is there is left out; its neighbour,
    // still there, stays (the folder is listed once for both).
    const std::string here = std::string(HUI_SOURCE_DIR) + "/tests/fixtures/music";
    const std::string gone =
        R"json({"items": [{"path": ")json" + here +
        R"json(/no-such-game.gba", "label": "Gone", "db_name": "gba.lpl"},
        {"path": ")json" +
        here + R"json(/game_net.ogg", "label": "Still Here", "db_name": "gba.lpl"}]})json";
    EXPECT_EQ(library::read_retroarch_playlist(emulator, gone, "gba.lpl", &roms), 1);
    ASSERT_EQ(roms.size(), before + 5u);

    const auto find = [&](const std::string &name) -> const library::Title *
    {
        for (const library::Title &rom : roms)
        {
            if (rom.name == name)
                return &rom;
        }
        return nullptr;
    };
    const library::Title *mario = find("Super Mario Bros. 3");
    ASSERT_NE(mario, nullptr);
    EXPECT_EQ(mario->system, "Nintendo - Game Boy Advance");
    EXPECT_EQ(mario->rom, "/mnt/usb0/roms/gba/Super Mario Bros. 3 (U) [!].gba");
    EXPECT_EQ(mario->launcher, "PPSA90120");
    const library::Title *rune = find("Rune Song");
    ASSERT_NE(rune, nullptr);
    EXPECT_EQ(rune->rom, "/mnt/usb0/roms/gba/Rune Song.zip#Rune Song.gba");
    EXPECT_EQ(rune->path, "/mnt/usb0/roms/gba/Rune Song.zip");
    ASSERT_NE(find("Paper Moon"), nullptr);
    EXPECT_EQ(find("Paper Moon")->system, "Nintendo - Nintendo DS");
    ASSERT_NE(find("Lantern"), nullptr);
    EXPECT_EQ(find("Lantern")->system, "Nintendo - Game Boy Advance");
    EXPECT_EQ(emulator.apps.front().games, 5);
    EXPECT_EQ(emulator.apps.front().folders.back(), "/mnt/usb0/roms/gba");
}

// Every installed emulator that can play a game, each seeing the file where
// it does: /app0 is an app's own folder.
TEST(Emulators, ListsEveryWayToPlayAGame)
{
    library::Emulator emulator;
    library::EmulatorApp retroarch;
    retroarch.key = "retroarch";
    retroarch.name = "RetroArch";
    retroarch.title_id = "PPSA90120";
    retroarch.root = "/data/homebrew/PPSA90120";
    emulator.apps.push_back(retroarch);
    library::EmulatorApp porpoise;
    porpoise.key = "porpoise";
    porpoise.name = "Porpoise";
    porpoise.title_id = "PPSA90130";
    porpoise.root = "/data/homebrew/PPSA90130";
    emulator.apps.push_back(porpoise);
    library::Core dolphin;
    dolphin.file = "dolphin_libretro.so";
    dolphin.name = "Dolphin";
    dolphin.databases = {"Nintendo - GameCube", "Nintendo - Wii"};
    library::Core snes9x;
    snes9x.file = "snes9x_libretro.so";
    snes9x.name = "Snes9x";
    snes9x.databases = {"Nintendo - Super Nintendo Entertainment System"};
    emulator.cores = {dolphin, snes9x};

    library::Title disc;
    disc.id = "rom-000000000001";
    disc.system = "Nintendo - GameCube";
    disc.path = "/data/homebrew/PPSA90130/porpoise/games/Quiet Bay.rvz";
    disc.rom = "/app0/porpoise/games/Quiet Bay.rvz";
    disc.launcher = "PPSA90130";
    const std::vector<library::Player> players = library::players_for(emulator, disc);
    ASSERT_EQ(players.size(), 2u);
    EXPECT_EQ(players[0].key, "retroarch:dolphin_libretro.so");
    EXPECT_EQ(players[0].label, "RetroArch · Dolphin");
    EXPECT_EQ(players[0].core, "dolphin_libretro.so");
    // Porpoise's folder is not RetroArch's: RetroArch sees the full path.
    EXPECT_EQ(players[0].rom, disc.path);
    EXPECT_EQ(players[1].key, "porpoise");
    EXPECT_EQ(players[1].rom, disc.rom); // as Porpoise found it

    library::Title cart;
    cart.id = "rom-000000000002";
    cart.system = "Nintendo - Super Nintendo Entertainment System";
    cart.path = "/mnt/usb0/roms/snes/Lantern.sfc";
    cart.rom = cart.path;
    cart.launcher = "PPSA90120";
    ASSERT_EQ(library::players_for(emulator, cart).size(), 1u);
    EXPECT_EQ(library::players_for(emulator, cart)[0].key, "retroarch:snes9x_libretro.so");
    // A console title has none.
    library::Title console;
    console.id = "PPSA90101";
    EXPECT_TRUE(library::players_for(emulator, console).empty());
}

// PS5SX2 is handed a game through its disc helper's note, which it reads
// after its jailbreak: the full path even for a game in its own folder.
TEST(Emulators, PS5SX2IsHandedTheGameByItsFullPath)
{
    library::Emulator emulator;
    library::EmulatorApp sx2;
    sx2.key = "ps5sx2";
    sx2.name = "PS5SX2";
    sx2.title_id = "PPSA90131";
    sx2.root = "/data/homebrew/PPSA90131";
    emulator.apps.push_back(sx2);

    library::Title disc;
    disc.id = "rom-000000000003";
    disc.system = "Sony - PlayStation 2";
    disc.path = "/data/homebrew/PPSA90131/games/Paper Tiger (USA).iso";
    disc.rom = "/app0/games/Paper Tiger (USA).iso";
    disc.launcher = "PPSA90131";
    const std::vector<library::Player> players = library::players_for(emulator, disc);
    ASSERT_EQ(players.size(), 1u);
    EXPECT_EQ(players[0].label, "PS5SX2");
    EXPECT_EQ(players[0].rom, disc.path);
    EXPECT_EQ(library::ps5sx2_launch_note(players[0].rom), disc.path + "\n");
    EXPECT_STREQ(library::kPs5sx2LaunchNote, "/data/PCSX2/logs/disc-launch.txt");
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
    // No ORION art folder: RetroArch's thumbnails, its PS5 library folder,
    // then ES-DE's covers.
    EXPECT_EQ(library::attach_rom_art(roms, title.path, ""), 3);
    EXPECT_EQ(icon_of("Starfall Racer"),
              title.path +
                  "/thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/Starfall Racer (USA).png");
    EXPECT_EQ(icon_of("Moon Garden"),
              title.path + "/es-de/ES-DE/downloaded_media/nds/covers/Moon Garden (Japan).png");
    EXPECT_EQ(icon_of("Comet Post"),
              title.path +
                  "/library/Nintendo - Game Boy Advance/Comet Post (Europe) (En,Fr,De) [!].png");

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

// Games kept on a server join their systems' tabs before they are on the
// console, play in RetroArch, and a file listed already is not listed twice.
TEST(Emulators, ListsServerGamesOnTheirSystems)
{
    const library::Title title = retroarch();
    std::vector<library::Title> roms;
    library::Emulator emulator = library::scan_retroarch(title, &roms);
    const std::size_t before = roms.size();
    std::vector<library::RemoteGame> games(3);
    games[0].system_folder = "gba";
    games[0].name = "Moon Garden";
    games[0].path = "/data/orion/romm/games/gba/Moon Garden (USA).gba";
    games[0].remote = "http://nas:3000/api/roms/10/content/Moon%20Garden%20%28USA%29.gba";
    games[0].icon = "/data/orion/romm/covers/10.png";
    games[0].cover = "http://nas:3000/assets/romm/resources/10.png";
    games[1].system_folder = "not-a-system";
    games[1].name = "Nowhere";
    games[1].path = "/data/orion/romm/games/x/y.bin";
    games[2] = games[0]; // the same file again
    EXPECT_EQ(library::add_remote_games(emulator, games, &roms, false), 1);
    ASSERT_EQ(roms.size(), before + 1);
    const library::Title &moon = roms.back();
    EXPECT_EQ(moon.system, "Nintendo - Game Boy Advance");
    EXPECT_EQ(moon.name, "Moon Garden");
    EXPECT_EQ(moon.rom, games[0].path);
    EXPECT_EQ(moon.remote, games[0].remote);
    // No picture yet: an emulator's art is looked for first (attach_rom_art),
    // the server's cover is for the downloader when there is none.
    EXPECT_EQ(moon.icon, "");
    EXPECT_EQ(moon.remote_cover, games[0].cover);
    EXPECT_EQ(moon.launcher, title.id);
    EXPECT_TRUE(moon.is_rom());
    const std::vector<library::Player> players = library::players_for(emulator, moon);
    ASSERT_FALSE(players.empty());
    EXPECT_EQ(players[0].rom, games[0].path); // RetroArch reads it where it lands

    // Only the ones the console lacks: a game it has by the same file, or by
    // the same name on the same system, is left out.
    roms.resize(before);
    library::Emulator again = library::scan_retroarch(title, &roms);
    ASSERT_EQ(roms.size(), before);
    const library::Title &mine = roms.front(); // one of RetroArch's own games
    std::vector<library::RemoteGame> server(3);
    const std::string folder = mine.system == "Nintendo - Nintendo DS" ? "nds" : "gba";
    server[0].system_folder = folder; // the same file
    server[0].name = "Another Name";
    server[0].path =
        "/data/orion/romm/games/" + folder + "/" + mine.path.substr(mine.path.rfind('/') + 1);
    server[1].system_folder = folder; // the same name
    server[1].name = mine.name;
    server[1].path = "/data/orion/romm/games/" + folder + "/Other File.bin";
    server[2].system_folder = folder; // one it lacks
    server[2].name = "Brand New";
    server[2].path = "/data/orion/romm/games/" + folder + "/Brand New.bin";
    EXPECT_EQ(library::add_remote_games(again, server, &roms, true), 1);
    EXPECT_EQ(roms.back().name, "Brand New");
    EXPECT_EQ(library::add_remote_games(again, server, &roms, false), 2); // all: the other two
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
    touch(drive / "roms/gba/notes.txt");             // no core takes .txt
    touch(drive / "roms/gba/Zipped Zone (USA).zip"); // RetroArch unpacks it for mGBA
    touch(drive / "roms/nds/Paper Moon (Europe).nds");
    touch(drive / "roms/unknown-system/whatever.bin"); // not a system: ignored
    EXPECT_EQ(library::scan_rom_folder(emulator, (drive / "roms").string(), "", &roms), 3);
    ASSERT_EQ(roms.size(), content + 3);
    const library::Title &lunar = roms[content];
    EXPECT_EQ(lunar.name, "Lunar Lift");
    EXPECT_EQ(lunar.system, "Nintendo - Game Boy Advance");
    // RetroArch reads it where it is: the same path, not one in its own folder.
    EXPECT_EQ(lunar.rom, (drive / "roms/gba/Lunar Lift (USA).gba").string());
    EXPECT_EQ(lunar.launcher, title.id);
    for (const library::System &system : emulator.systems)
    {
        if (system.short_name == "GBA")
            EXPECT_EQ(system.games, 4);
    }
    EXPECT_EQ(roms[content + 1].name, "Zipped Zone");

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

void put_big32(std::string &bytes, std::size_t at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<char>((value >> (24 - 8 * i)) & 0xff);
}

void put_little(std::string &bytes, std::size_t at, std::uint32_t value, int size)
{
    for (int i = 0; i < size; ++i)
        bytes[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
}

// The start of an Xbox 360 executable: its XEX2 header with two optional
// headers, the second the execution info (at 0x40) holding the title ID.
std::string xex(std::uint32_t title_id)
{
    std::string bytes(0x60, '\0');
    bytes.replace(0, 4, "XEX2");
    put_big32(bytes, 20, 2);
    put_big32(bytes, 24, 0x000183ffu);
    put_big32(bytes, 28, 0x1234);
    put_big32(bytes, 32, 0x00040006u);
    put_big32(bytes, 36, 0x40);
    put_big32(bytes, 0x40 + 12, title_id);
    return bytes;
}

// An Xbox disc's game partition (XDVDFS): the volume descriptor in sector
// 32, the root folder in sector 33 (an entry "media" whose right subtree is
// default.xex), the executable in sector 34.
std::string xbox_disc(std::uint32_t title_id)
{
    const std::string head = xex(title_id);
    std::string disc(34 * 2048 + head.size(), '\0');
    disc.replace(32 * 2048, 20, "MICROSOFT*XBOX*MEDIA");
    put_little(disc, 32 * 2048 + 20, 33, 4);
    put_little(disc, 32 * 2048 + 24, 2048, 4);
    const std::size_t folder = 33 * 2048;
    put_little(disc, folder + 2, 8, 2); // right: 8 steps of 4 bytes on
    disc[folder + 12] = 0x10;
    disc[folder + 13] = 5;
    disc.replace(folder + 14, 5, "media");
    put_little(disc, folder + 32 + 4, 34, 4);
    put_little(disc, folder + 32 + 8, static_cast<std::uint32_t>(head.size()), 4);
    disc[folder + 32 + 12] = 0x20;
    disc[folder + 32 + 13] = 11;
    disc.replace(folder + 32 + 14, 11, "DEFAULT.XEX");
    disc.replace(34 * 2048, head.size(), head);
    return disc;
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
    EXPECT_TRUE(sx2->starts_games);
    EXPECT_EQ(dolphin->games, 2); // the gba folder is another system's
    EXPECT_EQ(sx2->games, 2);     // the GameCube disc is Porpoise's kind
    ASSERT_FALSE(dolphin->folders.empty());
    EXPECT_EQ(dolphin->folders.front(), (porpoise / "porpoise/games").string());

    std::vector<std::string> systems;
    for (const library::System &system : emulator.systems)
        systems.push_back(system.short_name);
    EXPECT_EQ(systems, (std::vector<std::string>{"NGC", "PS2", "Wii"}));

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

// PS5SX2's and Porpoise's own covers: a PS2 game by the serial PS5SX2 noted
// for its file, a GameCube disc by the ID in its header. A game that has a
// picture keeps it.
TEST(Emulators, UsesTheCoversTheEmulatorsDownloaded)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hui-emulator-covers";
    fs::remove_all(root);
    write_file(root / "PCSX2/covers/SLUS-21678.jpg", "jpeg");
    write_file(root / "porpoise/covers/GQBP01.png", "png");
    const fs::path ps2 = root / "roms/Budokai (USA).iso";
    const fs::path cube = root / "roms/Quiet Bay.iso";
    write_file(ps2, std::string(1024, '\0'));
    write_file(cube, disc_head(0, "GQBP01", "Quiet Bay", false));
    write_file(root / "PCSX2/chd-serials.txt",
               ps2.string() +
                   "\t3020816384\t1792304888\tSLUS-21678\n/elsewhere.iso\t1\t2\tSLES-00000\n");

    std::vector<library::Title> roms(3);
    roms[0].system = "Sony - PlayStation 2";
    roms[0].path = ps2.string();
    roms[1].system = "Nintendo - GameCube";
    roms[1].path = cube.string();
    roms[2].system = "Sony - PlayStation 2";
    roms[2].path = (root / "roms/Unknown.iso").string();
    roms[2].icon = "/kept.png";
    EXPECT_EQ(library::attach_emulator_covers(roms, (root / "PCSX2").string(),
                                              (root / "porpoise").string()),
              2);
    EXPECT_EQ(roms[0].icon, (root / "PCSX2/covers/SLUS-21678.jpg").string());
    EXPECT_EQ(roms[1].icon, (root / "porpoise/covers/GQBP01.png").string());
    EXPECT_EQ(roms[2].icon, "/kept.png");
    fs::remove_all(root);
}

// A full ROM set's many files of one game show as one, the best: a verified
// dump from the USA over bad dumps, hacks, trainers and translations; discs
// stay apart, and another emulator's games are left alone.
TEST(Emulators, KeepsTheBestVersionOfEachGame)
{
    EXPECT_GT(library::version_score("Lantern Night (U) [!].gb"),
              library::version_score("Lantern Night (U) [b1].gb"));
    EXPECT_GT(library::version_score("Lantern Night (U).gb"),
              library::version_score("Lantern Night (U) [h1].gb"));
    EXPECT_GT(library::version_score("Lantern Night (USA).gb"),
              library::version_score("Lantern Night (Japan).gb"));
    EXPECT_GT(library::version_score("Lantern Night (J) [T+Eng].gb"),
              library::version_score("Lantern Night (J) [b2].gb"));
    EXPECT_GT(library::version_score("Lantern Night (Europe) (Rev 1).gb"),
              library::version_score("Lantern Night (Europe) (Beta).gb"));

    const auto rom = [](const std::string &file, const std::string &name, const char *system,
                        const std::string &launcher)
    {
        library::Title title;
        title.path = "/mnt/usb1/gbc/" + file;
        title.rom = title.path;
        title.id = library::rom_id(title.path);
        title.name = name;
        title.system = system;
        title.launcher = launcher;
        title.source = library::Source::image;
        return title;
    };
    constexpr const char *kGb = "Nintendo - Game Boy";
    constexpr const char *kGbc = "Nintendo - Game Boy Color";
    constexpr const char *kPs1 = "Sony - PlayStation";
    std::vector<library::Title> roms = {
        rom("Comet Post (U) [C][b1].gbc", "Comet Post", kGbc, "RA"),
        rom("Comet Post (U) [C][!].gbc", "Comet Post", kGbc, "RA"),
        rom("Comet Post (U) [C][t1].gbc", "Comet Post", kGbc, "RA"),
        rom("Comet Post (J) [T+Eng].gb", "Comet Post", kGb, "RA"), // the same tab
        rom("Quiet Bay (USA) (Disc 1).chd", "Quiet Bay", kPs1, "RA"),
        rom("Quiet Bay (USA) (Disc 2).chd", "Quiet Bay", kPs1, "RA"),
        rom("Paper Moon (U) [b1].gb", "Paper Moon", kGb, "OTHER"),
        rom("Paper Moon (U) [!].gb", "Paper Moon", kGb, "OTHER"),
    };
    EXPECT_EQ(library::keep_best_versions(roms, "RA"), 3);
    ASSERT_EQ(roms.size(), 5u);
    EXPECT_EQ(roms[0].path, "/mnt/usb1/gbc/Comet Post (U) [C][!].gbc");
    EXPECT_EQ(roms[1].name, "Quiet Bay");
    EXPECT_EQ(roms[2].name, "Quiet Bay"); // two discs, both kept
    EXPECT_EQ(roms[3].launcher, "OTHER"); // not RetroArch's: both kept
    EXPECT_EQ(roms[4].launcher, "OTHER");
    EXPECT_EQ(library::keep_best_versions(roms, ""), 0);
}

// PSwanStation's PS1 discs (the first disc of a game on several, a .cue and
// not its .bin) with its covers by file name; ProsperoEden's Switch games and
// its covers by title. Both start games themselves; ProsperoEden is offered
// before PS5-ECAM for a Switch game.
TEST(Emulators, ListsPSwanStationAndProsperoEdenGames)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hui-swan-eden";
    fs::remove_all(root);
    const fs::path swan_root = root / "PPSA90170";
    const fs::path games = swan_root / "games";
    write_file(games / "Quiet Bay (USA) (Disc 1).chd", "chd");
    write_file(games / "Quiet Bay (USA) (Disc 2).chd", "chd");
    write_file(games / "Paper Tower (USA).cue", "cue");
    write_file(games / "Paper Tower (USA).bin", "bin");
    write_file(games / "notes.txt", "not a game");
    write_file(swan_root / "covers/Paper Tower (USA).png", "png");
    const fs::path switch_games = root / "switch";
    write_file(switch_games / "Moon Garden [0100AAAA00000000][v0][US].xci", "xci");
    write_file(switch_games / "readme.txt", "no");
    // Its update and add-ons are installed, not played (by ID, by tag, by folder).
    write_file(switch_games / "Moon Garden [0100AAAA00000800][v65536].nsp", "nsp");
    write_file(switch_games / "Moon Garden Seeds [0100AAAA00001001][v0].nsp", "nsp");
    write_file(switch_games / "Moon Garden [DLC] Night Pack.nsp", "nsp");
    write_file(switch_games / "dlc/Moon Garden Extra [0100AAAA00000000].nsp", "nsp");
    const fs::path eden_data = root / "prosperoeden";
    write_file(eden_data / "covers/0a1b2c3d.name", "Moon Garden");
    write_file(eden_data / "covers/0a1b2c3d.tga", "tga");
    write_file(eden_data / "covers/ffffffff.name", "No Picture Here");

    const std::vector<library::Title> titles = {
        app_title("PPSA90170", "PSSwanStation", swan_root.string()),
        app_title("PPSA90171", "ProsperoEden", (root / "PPSA90171").string()),
        app_title("PPSA90172", "PS5-ECAM", (root / "PPSA90172").string()),
    };
    EXPECT_EQ(library::emulator_key(titles[0]), "psswanstation");
    EXPECT_EQ(library::emulator_key(titles[1]), "eden");

    std::vector<library::Title> roms;
    library::Emulator emulator = library::scan_emulators(titles, {}, &roms);
    const library::EmulatorApp *swan = emulator.app("psswanstation");
    ASSERT_NE(swan, nullptr);
    EXPECT_TRUE(swan->starts_games);
    EXPECT_EQ(swan->games, 2);
    ASSERT_EQ(roms.size(), 2u);
    EXPECT_EQ(roms[0].name, "Paper Tower");
    EXPECT_EQ(roms[0].system, "Sony - PlayStation");
    EXPECT_EQ(roms[0].rom, "/app0/games/Paper Tower (USA).cue"); // its folder is /app0 to it
    EXPECT_EQ(roms[0].icon, (swan_root / "covers/Paper Tower (USA).png").string());
    EXPECT_EQ(roms[1].name, "Quiet Bay");
    EXPECT_EQ(roms[1].launcher, "PPSA90170");

    // ProsperoEden's games, from a folder of the player's, with its covers.
    const auto covers = library::eden_covers(eden_data.string());
    ASSERT_EQ(covers.size(), 1u);
    EXPECT_EQ(covers.begin()->second, (eden_data / "covers/0a1b2c3d.tga").string());
    EXPECT_EQ(library::scan_game_folder(emulator, {switch_games.string(), "", "eden"}, &roms), 1);
    ASSERT_EQ(roms.size(), 3u);
    EXPECT_EQ(roms[2].name, "Moon Garden");
    EXPECT_EQ(roms[2].system, "Nintendo - Switch");
    const std::vector<library::Player> players = library::players_for(emulator, roms[2]);
    ASSERT_EQ(players.size(), 2u);
    EXPECT_EQ(players[0].key, "eden");
    EXPECT_EQ(players[1].key, "ecam");
    // A Switch folder is no PS1 folder.
    EXPECT_EQ(
        library::scan_game_folder(emulator, {switch_games.string(), "", "psswanstation"}, &roms),
        0);
    fs::remove_all(root);
}

// PS5X360's Xbox 360 games: a folder per extracted game (default.xex, named
// for the folder, its cover.jpg) and disc images (a picture of the same name
// beside one). It is handed the game as its first argument.
TEST(Emulators, ListsPS5X360Games)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hui-x360";
    fs::remove_all(root);
    const fs::path x360_root = root / "PPSA50011";
    const fs::path roms = x360_root / "assets/roms";
    write_file(roms / "Harbor Lights/default.xex", xex(0x454107d9u));
    write_file(roms / "Harbor Lights/cover.jpg", "jpg");
    write_file(roms / "Iron Valley/game.iso", "iso");
    // An XGD2 disc: its game partition starts 0xFD90000 bytes in (a sparse
    // file, so the test writes a few kilobytes, not 265 MB).
    {
        const std::string partition = xbox_disc(0x4d5307e6u);
        const fs::path disc = roms / "Night Relay (USA).iso";
        write_file(disc, "");
        fs::resize_file(disc, 0xFD90000u + partition.size());
        std::fstream out(disc, std::ios::binary | std::ios::in | std::ios::out);
        out.seekp(0xFD90000);
        out << partition;
    }
    write_file(roms / "Night Relay (USA).png", "png");
    write_file(roms / "Empty Folder/readme.txt", "no");
    write_file(roms / "README.txt", "no");

    const std::vector<library::Title> titles = {
        app_title("PPSA50011", "PS5X360", x360_root.string()),
    };
    EXPECT_EQ(library::emulator_key(titles[0]), "ps5x360");

    std::vector<library::Title> found;
    library::Emulator emulator = library::scan_emulators(titles, {}, &found);
    const library::EmulatorApp *x360 = emulator.app("ps5x360");
    ASSERT_NE(x360, nullptr);
    EXPECT_TRUE(x360->starts_games);
    EXPECT_EQ(x360->games, 3);
    ASSERT_EQ(found.size(), 3u);
    EXPECT_EQ(found[0].name, "Harbor Lights");
    EXPECT_EQ(found[0].system, "Microsoft - Xbox 360");
    EXPECT_EQ(found[0].rom, "/app0/assets/roms/Harbor Lights/default.xex");
    EXPECT_EQ(found[0].icon, (roms / "Harbor Lights/cover.jpg").string());
    // The title IDs XboxUnity knows their covers by, from default.xex, read
    // only when a cover is wanted.
    EXPECT_EQ(found[0].serial, "");
    EXPECT_EQ(library::cover_id(found[0]), "454107D9");
    // The top folder's games, then those in its folders.
    EXPECT_EQ(found[1].name, "Night Relay");
    EXPECT_EQ(found[1].icon, (roms / "Night Relay (USA).png").string());
    EXPECT_EQ(library::cover_id(found[1]), "4D5307E6");
    EXPECT_EQ(found[2].name, "Iron Valley");
    EXPECT_EQ(library::cover_id(found[2]), ""); // not a disc it can read
    // Box art is kept by the game's folder where every file is called alike.
    EXPECT_EQ(library::art_stem("/app0/assets/roms/Harbor Lights/default.xex"), "Harbor Lights");
    EXPECT_EQ(library::art_stem("/mnt/usb0/xbox360/Iron Valley/game.iso"), "Iron Valley");
    EXPECT_EQ(library::art_stem("/mnt/usb0/wiiu/Quiet Bay/title.tmd"), "Quiet Bay");
    EXPECT_EQ(library::art_stem("/mnt/usb0/xbox360/Night Relay (USA).iso"), "Night Relay (USA)");
    EXPECT_EQ(library::art_stem("/mnt/usb0/roms/snes/game.sfc"),
              "game"); // other systems keep theirs
    EXPECT_EQ(found[2].launcher, "PPSA50011");

    const std::vector<library::Player> players = library::players_for(emulator, found[0]);
    ASSERT_EQ(players.size(), 1u);
    EXPECT_EQ(players[0].key, "ps5x360");
    // The game first: PS5X360 took a title ID there for the game to boot.
    EXPECT_EQ(library::game_arguments(players[0].rom),
              "/app0/assets/roms/Harbor Lights/default.xex\n");
    EXPECT_EQ(library::xex_title_id("XEX2"), "");
    EXPECT_EQ(library::xex_title_id(xex(0)), "");
    fs::remove_all(root);
}

// Game Boy games share the Game Boy Color's tab, each keeping its own system
// (its box art is the Game Boy's).
TEST(Emulators, PutsGameBoyGamesOnTheColorsTab)
{
    std::vector<library::Title> roms;
    library::Emulator emulator = library::scan_retroarch(retroarch(), &roms);
    const std::size_t before = roms.size();
    const std::string playlist = R"json({"items": [
        {"path": "/mnt/usb9/gbc/Lantern Night (U).gb", "label": "Lantern Night",
         "db_name": "Nintendo - Game Boy.lpl"},
        {"path": "/mnt/usb9/gbc/Comet Post (U).gbc", "label": "Comet Post",
         "db_name": "Nintendo - Game Boy Color.lpl"}]})json";
    EXPECT_EQ(library::read_retroarch_playlist(emulator, playlist, "usb9.lpl", &roms), 2);
    ASSERT_EQ(roms.size(), before + 2u);
    // Sorted by name: Comet Post, then Lantern Night.
    EXPECT_EQ(roms[before].system, "Nintendo - Game Boy Color");
    EXPECT_EQ(roms[before + 1].system, "Nintendo - Game Boy");
    int shelves = 0;
    for (const library::System &system : emulator.systems)
    {
        EXPECT_NE(system.name, "Nintendo - Game Boy");
        if (system.name == "Nintendo - Game Boy Color")
        {
            ++shelves;
            EXPECT_EQ(system.short_name, "GBC");
            EXPECT_EQ(system.games, 2);
        }
    }
    EXPECT_EQ(shelves, 1);
    EXPECT_EQ(library::shelf_system("Nintendo - Game Boy"), "Nintendo - Game Boy Color");
    EXPECT_EQ(library::shelf_system("Nintendo - Game Boy Advance"), "Nintendo - Game Boy Advance");
    EXPECT_EQ(library::short_system_name("Nintendo - GameCube"), "NGC");
}

// PS5-ECAM's own list: its games with the covers it downloaded (the box by
// the GameTDB ID, else the icon by the title ID), a deleted game left out, a
// game RetroArch listed first given PS5-ECAM's cover. Then a folder of its
// with a game it has not listed yet.
TEST(Emulators, ListsPs5EcamsGamesWithItsCovers)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hui-ecam";
    fs::remove_all(root);
    const fs::path data = root / "ps5cemu";
    const fs::path drive = root / "drive";
    write_file(data / "covers/boxart/wiiu/AQBE01.tga", "tga");
    write_file(data / "covers/0005000010aaaa00.tga", "icon");
    write_file(data / "covers/0005000010bbbb00.tga", "icon");
    write_file(data / "covers/boxart/ds/AMGE.tga", "tga");
    const fs::path bay = drive / "wiiu/Quiet Bay [0005000010AAAA00]/title.tmd";
    const fs::path tower = drive / "wiiu/Paper Tower [0005000010BBBB00]/title.tmd";
    const fs::path moon = drive / "switch/Moon Garden [0100AAAA00000000][v0][US].xci";
    const fs::path lantern = drive / "switch/Lantern Night (USA).nsp";
    const fs::path comet = drive / "nds/Comet Post (USA).zip";
    write_file(bay, "tmd");
    write_file(tower, "tmd");
    write_file(moon, "xci");
    write_file(lantern, "nsp");
    write_file(comet, "zip");
    write_file(drive / "switch/notes.txt", "not a game");

    library::Emulator emulator;
    library::EmulatorApp ecam;
    ecam.key = "ecam";
    ecam.name = "PS5-ECAM";
    ecam.title_id = "PPSA90140";
    ecam.root = (root / "PPSA90140").string();
    ecam.starts_games = true;
    emulator.apps.push_back(ecam);
    EXPECT_EQ(library::emulator_key(app_title("PPSA90140", "PS5-ECAM", ecam.root)), "ecam");

    // RetroArch's DS game, listed first, with no picture.
    std::vector<library::Title> roms(1);
    roms[0].id = library::rom_id(comet.string());
    roms[0].rom = comet.string();
    roms[0].path = comet.string();
    roms[0].system = "Nintendo - Nintendo DS";
    const auto item = [](const std::string &title_id, const std::string &name, const fs::path &path,
                         const std::string &game_id)
    {
        return R"({"titleId": ")" + title_id + R"(", "name": ")" + name + R"(", "path": ")" +
               path.string() + R"(", "gameId": ")" + game_id + R"("})";
    };
    const std::string text =
        R"({"version": 1, "wiiu": [)" + item("0005000010aaaa00", "Quiet Bay", bay, "AQBE01") + "," +
        item("0005000010bbbb00", "Paper Tower", tower, "") + R"(], "3ds": [], "ds": [)" +
        item("d500aaaa45414141", "Comet Post", comet, "AMGE") + R"(], "switch": [)" +
        item("0100aaaa00000000", "Moon Garden", moon, "") + "," +
        item("0100bbbb00000000", "Gone", drive / "switch/Gone.xci", "") + "," +
        item("0100cccc00000000", "Far Away", "/mnt/usb7/switch/Far Away.xci", "") + "]}";
    EXPECT_EQ(library::read_ecam_library(emulator, text, data.string(), &roms), 4);
    EXPECT_EQ(library::read_ecam_library(emulator, "not json", data.string(), &roms), 0);
    EXPECT_EQ(emulator.apps.front().games, 4);

    const auto find = [&](const std::string &name) -> const library::Title *
    {
        for (const library::Title &rom : roms)
        {
            if (rom.name == name)
                return &rom;
        }
        return nullptr;
    };
    const library::Title *quiet = find("Quiet Bay");
    ASSERT_NE(quiet, nullptr);
    EXPECT_EQ(quiet->system, "Nintendo - Wii U");
    EXPECT_EQ(quiet->rom, bay.string());
    EXPECT_EQ(quiet->launcher, "PPSA90140");
    EXPECT_EQ(quiet->icon, (data / "covers/boxart/wiiu/AQBE01.tga").string());
    EXPECT_EQ(quiet->serial, "AQBE01"); // its box art can be fetched by it

    // The console's titles show the sharper picture ORION saved, never a game.
    {
        const fs::path icons = root / "icons";
        write_file(icons / "PPSA90150.png", "png");
        write_file(icons / "rom-000000000099.png", "png");
        library::Library shelf;
        shelf.titles.resize(3);
        shelf.titles[0].id = "PPSA90150";
        shelf.titles[0].icon = "/data/shadowmount/icon-cache/PPSA90150-128-1-1.png";
        shelf.titles[1].id = "PPSA90151"; // nothing saved
        shelf.titles[1].icon = "/kept.png";
        shelf.titles[2].id = "rom-000000000099";
        shelf.titles[2].system = "Nintendo - Wii U";
        EXPECT_EQ(library::prefer_saved_icons(shelf, icons.string()), 1);
        EXPECT_EQ(shelf.titles[0].icon, (icons / "PPSA90150.png").string());
        EXPECT_EQ(shelf.titles[1].icon, "/kept.png");
        EXPECT_EQ(shelf.titles[2].icon, "");
    }
    ASSERT_NE(find("Paper Tower"), nullptr);
    EXPECT_EQ(find("Paper Tower")->icon, (data / "covers/0005000010bbbb00.tga").string());
    ASSERT_NE(find("Moon Garden"), nullptr);
    EXPECT_EQ(find("Moon Garden")->system, "Nintendo - Switch");
    EXPECT_EQ(find("Moon Garden")->icon, "");
    EXPECT_EQ(find("Gone"), nullptr);     // its folder is there, the file is not
    ASSERT_NE(find("Far Away"), nullptr); // on a drive not plugged in
    EXPECT_EQ(roms[0].icon, (data / "covers/boxart/ds/AMGE.tga").string());
    EXPECT_EQ(library::short_system_name("Nintendo - Wii U"), "Wii U");

    // Its Switch folder: the game it has not listed yet joins, Moon Garden
    // is not listed twice and the text file is no game.
    EXPECT_EQ(library::scan_game_folder(emulator, {(drive / "switch").string(), "", "ecam"}, &roms),
              1);
    ASSERT_NE(find("Lantern Night"), nullptr);
    EXPECT_EQ(find("Lantern Night")->system, "Nintendo - Switch");
    // A Wii U game kept as NUS files is its folder, named for the folder.
    library::Emulator fresh;
    fresh.apps.push_back(ecam);
    std::vector<library::Title> found;
    EXPECT_EQ(library::scan_game_folder(fresh, {(drive / "wiiu").string(), "", "ecam"}, &found), 2);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].name, "Paper Tower");
    EXPECT_EQ(found[0].rom, tower.string());
    EXPECT_EQ(found[0].system, "Nintendo - Wii U");

    // It starts them as Porpoise does, and plays RetroArch's DS game too.
    const std::vector<library::Player> players = library::players_for(emulator, *quiet);
    ASSERT_EQ(players.size(), 1u);
    EXPECT_EQ(players[0].key, "ecam");
    EXPECT_EQ(players[0].rom, bay.string());
    ASSERT_EQ(library::players_for(emulator, roms[0]).size(), 1u);
    EXPECT_EQ(library::players_for(emulator, roms[0])[0].key, "ecam");
    fs::remove_all(root);
}

} // namespace
} // namespace hui
