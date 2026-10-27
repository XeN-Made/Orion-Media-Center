// ORION Media Center - Tests: matching RomM's saves to RetroArch's.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/romm_saves.hpp"

#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace hui
{
namespace
{

TEST(RommSaves, NamesAsRetroArchDoes)
{
    EXPECT_EQ(orion::retroarch_stem("/mnt/usb0/gba/Moon Garden (USA).gba"), "Moon Garden (USA)");
    EXPECT_EQ(orion::retroarch_stem("/mnt/usb0/psp/Game [X].zip#inner.iso"), "Game [X]");
    EXPECT_EQ(orion::retroarch_stem("NoExtension"), "NoExtension");
    EXPECT_EQ(orion::retroarch_core_key("mgba_libretro.so"), "mgba");
    EXPECT_EQ(orion::retroarch_core_key("/app0/cores/mupen64plus_next_libretro.so"),
              "mupen64plus_next");
    EXPECT_EQ(orion::romm_id_from_url("http://nas:3000/api/roms/10502/content/a.nds"), 10502);
    EXPECT_EQ(orion::romm_id_from_url("http://nas:3000/other"), 0);
}

TEST(RommSaves, PicksTheNewestSaveOfTheGamesEmulator)
{
    std::vector<orion::RommSave> saves(4);
    saves[0].rom_id = 9;
    saves[0].emulator = "mgba";
    saves[0].updated = "2026-09-26T00:09:14+00:00";
    saves[1].rom_id = 9;
    saves[1].emulator = "mgba";
    saves[1].updated = "2026-09-26T00:51:48+00:00";
    saves[2].rom_id = 9;
    saves[2].emulator = "vba_next";
    saves[2].updated = "2026-10-01T00:00:00+00:00"; // newer, but another core's
    saves[3].rom_id = 9;
    saves[3].state = true;
    saves[3].emulator = "mgba";
    saves[3].updated = "2026-10-02T00:00:00+00:00";
    const orion::RommSave *save = orion::latest_romm_save(saves, 9, "mgba", false);
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save, &saves[1]);
    EXPECT_EQ(orion::latest_romm_save(saves, 9, "snes9x", false), &saves[2]); // none of its own
    EXPECT_EQ(orion::latest_romm_save(saves, 9, "mgba", true), &saves[3]);
    EXPECT_EQ(orion::latest_romm_save(saves, 10, "mgba", false), nullptr);
    saves[1].id = 2;
    EXPECT_EQ(orion::romm_save_content_url("http://nas:3000", saves[1]),
              "http://nas:3000/api/saves/2/content");
    saves[3].id = 7;
    EXPECT_EQ(orion::romm_save_content_url("http://nas:3000", saves[3]),
              "http://nas:3000/api/states/7/content");

    std::vector<orion::RommGame> games(1);
    games[0].id = 9;
    games[0].fs_name = "Pokemon - FireRed Version (USA).gba";
    EXPECT_EQ(orion::romm_ids_by_file(games).at("pokemon - firered version (usa).gba"), 9);
}

TEST(RommSaves, FindsTheNewestStateInAFolder)
{
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "orion-states";
    fs::remove_all(folder);
    fs::create_directories(folder);
    std::ofstream(folder / "Moon Garden (USA).state1") << "one";
    std::ofstream(folder / "Other Game.state") << "other";
    EXPECT_EQ(orion::newest_state(folder.string(), "Moon Garden (USA)"),
              (folder / "Moon Garden (USA).state1").string());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    std::ofstream(folder / "Moon Garden (USA).state") << "newer";
    EXPECT_EQ(orion::newest_state(folder.string(), "Moon Garden (USA)"),
              (folder / "Moon Garden (USA).state").string());
    EXPECT_EQ(orion::newest_state(folder.string(), "Nothing"), "");
    fs::remove_all(folder);
}

} // namespace
} // namespace hui
