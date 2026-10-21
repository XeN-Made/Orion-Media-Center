// ORION Media Center - Tests: ORION's folders, counted and copied.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "orion/storage.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace hui
{
namespace
{

TEST(Storage, CopiesAFolderWithoutOverwriting)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "hui-storage";
    std::filesystem::remove_all(root);
    const auto write = [](const std::filesystem::path &file, const char *text)
    {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file) << text;
    };
    write(root / "art/Nintendo - Nintendo DS/Chrono Trigger.png", "ds");
    write(root / "art/Sony - PlayStation 2/Dragon Ball.png", "ps2");
    write(root / "art/tried.txt", "rom-1\n");
    write(root / "usb/art/Sony - PlayStation 2/Dragon Ball.png", "mine"); // already there

    EXPECT_EQ(orion::copy_tree((root / "art").string(), (root / "usb/art").string()), 2);
    std::string text;
    ASSERT_TRUE(save::read_file(
        (root / "usb/art/Nintendo - Nintendo DS/Chrono Trigger.png").string(), &text));
    EXPECT_EQ(text, "ds");
    ASSERT_TRUE(
        save::read_file((root / "usb/art/Sony - PlayStation 2/Dragon Ball.png").string(), &text));
    EXPECT_EQ(text, "mine"); // the copy there wins
    EXPECT_EQ(orion::count_files((root / "usb/art").string()), 3);
    EXPECT_EQ(orion::count_files((root / "usb/art").string(), ".png"), 2);
    EXPECT_EQ(orion::copy_tree((root / "art").string(), (root / "art").string()), 0);

    write(root / "backdrops/PPSA08560.png", "x");
    write(root / "backdrops/PPSA99000.none", "");
    EXPECT_EQ(orion::remove_files((root / "backdrops").string(), ".none"), 1);
    EXPECT_EQ(orion::count_files((root / "backdrops").string()), 1);
    std::filesystem::remove_all(root);
}

} // namespace
} // namespace hui
