// ORION Media Center - Tests: looking for the system's installed titles, and saying how it went.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/registered_titles.hpp"

#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace hui
{
namespace
{

TEST(RegisteredTitles, RemembersTheFtpPortThatAnswered)
{
    const std::string dir = ::testing::TempDir() + "orion-ftp-port";
    ASSERT_TRUE(save::ensure_directory(dir));
    std::remove((dir + "/ftp-port.txt").c_str());
    EXPECT_EQ(orion::ftp_port_of(dir, 2121), 2121); // nothing found yet
    ASSERT_TRUE(save::write_atomic(dir + "/ftp-port.txt", "1337\n").empty());
    EXPECT_EQ(orion::ftp_port_of(dir, 2121), 1337);
    ASSERT_TRUE(save::write_atomic(dir + "/ftp-port.txt", "nonsense").empty());
    EXPECT_EQ(orion::ftp_port_of(dir, 2121), 2121);
    EXPECT_EQ(orion::ftp_port_of("", 21), 21);
}

// With no FTP server and no ShadowMountPlus (as on a PC), the run still
// says which ports it tried, so a player's file shows what was missing.
TEST(RegisteredTitles, SaysWhatItTried)
{
    const std::string dir = ::testing::TempDir() + "orion-registered-log";
    ASSERT_TRUE(save::ensure_directory(dir));
    std::remove((dir + "/registered.txt").c_str());
    orion::RegisteredTitles registered;
    ASSERT_TRUE(registered.start(dir, 39999)); // a port nothing listens on
    registered.wait();
    EXPECT_TRUE(registered.take_finished());
    std::string log;
    ASSERT_TRUE(save::read_file(dir + "/registered.txt", &log));
    EXPECT_NE(log.find("FTP ports 39999 no, 2121 "), std::string::npos) << log;
    EXPECT_NE(log.find("1337 "), std::string::npos) << log;
    EXPECT_NE(log.find("ShadowMountPlus: "), std::string::npos) << log;
}

// No FTP server: the titles ORION's payload copied out of /user/app join
// all the same, with their icons, and only the ones still installed.
TEST(RegisteredTitles, ListsWhatThePayloadFoundWithoutFtp)
{
    const std::string dir = ::testing::TempDir() + "orion-registered-payload";
    const std::string apps = dir + "/" + orion::kSystemApps;
    ASSERT_TRUE(save::ensure_directory(dir));
    ASSERT_TRUE(save::ensure_directory(apps));
    ASSERT_TRUE(save::ensure_directory(apps + "/PPSA90108"));
    ASSERT_TRUE(save::ensure_directory(apps + "/PPSA90999"));
    std::remove((dir + "/registered.tsv").c_str());
    std::remove((dir + "/icons/PPSA90108.png").c_str());
    std::string param;
    ASSERT_TRUE(save::read_file(std::string(HUI_SOURCE_DIR) +
                                    "/tests/fixtures/library/homebrew/PPSA90108/sce_sys/param.json",
                                &param));
    ASSERT_TRUE(save::write_atomic(apps + "/PPSA90108/param.json", param).empty());
    ASSERT_TRUE(save::write_atomic(apps + "/PPSA90108/icon0.png", "png").empty());
    // Copied before, uninstalled since: not in the list any more.
    ASSERT_TRUE(save::write_atomic(apps + "/PPSA90999/param.json", param).empty());
    ASSERT_TRUE(save::write_atomic(apps + "/index.txt", "PPSA90108\nnot-an-id\n").empty());

    orion::RegisteredTitles registered;
    ASSERT_TRUE(registered.start(dir, 39999));
    registered.wait();
    EXPECT_TRUE(registered.take_finished());
    const orion::RegisteredTitles::Result result = registered.result();
    EXPECT_TRUE(result.server);
    EXPECT_EQ(result.titles, 1);
    std::string cache;
    ASSERT_TRUE(save::read_file(dir + "/registered.tsv", &cache));
    EXPECT_EQ(cache.rfind("PPSA90108\t", 0), 0u) << cache;
    EXPECT_EQ(cache.find("PPSA90999"), std::string::npos);
    std::string icon;
    ASSERT_TRUE(save::read_file(dir + "/icons/PPSA90108.png", &icon));
    EXPECT_EQ(icon, "png");
    std::string log;
    ASSERT_TRUE(save::read_file(dir + "/registered.txt", &log));
    EXPECT_NE(log.find("ORION's payload: 1 more"), std::string::npos) << log;
}

} // namespace
} // namespace hui
