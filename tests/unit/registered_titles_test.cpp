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

} // namespace
} // namespace hui
