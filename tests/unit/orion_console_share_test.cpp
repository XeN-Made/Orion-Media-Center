// ORION Media Center - Tests: the file ORION writes for the share payload, and what it says back.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "orion/console_share.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{

using hui::orion::ConsoleShare;

// Off with the user "ps5" and no password until written; what is written
// comes back, a line each, with no line breaks or tabs smuggled in.
TEST(OrionConsoleShare, KeepsItsSettingsOneToALine)
{
    const std::string dir = ::testing::TempDir() + "orion-console-share";
    hui::save::remove_tree(dir);
    ASSERT_TRUE(hui::save::ensure_directory(dir));

    const ConsoleShare none = hui::orion::read_console_share(dir);
    EXPECT_FALSE(none.on);
    EXPECT_EQ(none.user, "ps5");
    EXPECT_TRUE(none.password.empty());

    ConsoleShare share;
    share.on = true;
    share.user = "orion";
    share.password = "pa\tss\nword";
    ASSERT_TRUE(hui::orion::write_console_share(dir, share));
    std::string text;
    ASSERT_TRUE(hui::save::read_file(dir + "/smb-share.txt", &text, 4096));
    EXPECT_EQ(text, "on\t1\nuser\torion\npassword\tpassword\n");
    const ConsoleShare back = hui::orion::read_console_share(dir);
    EXPECT_TRUE(back.on);
    EXPECT_EQ(back.user, "orion");
    EXPECT_EQ(back.password, "password");
}

// What went wrong, as Settings shows it; nothing while it shares.
TEST(OrionConsoleShare, SaysWhatWentWrong)
{
    const std::string dir = ::testing::TempDir() + "orion-console-share-status";
    hui::save::remove_tree(dir);
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    EXPECT_EQ(hui::orion::console_share_problem(dir), "");
    const auto status = [&](const std::string &line)
    {
        hui::save::write_atomic(dir + "/smb-share-status.txt", line);
        return hui::orion::console_share_problem(dir);
    };
    EXPECT_EQ(status("on\tdata, usb0\n"), "");
    EXPECT_EQ(status("busy\t\n"),
              "Another app already shares this console's drives (port 445 is taken)");
    EXPECT_EQ(status("error\terrno 13\n"), "Could not share: errno 13");
    EXPECT_EQ(status("off\t\n"), "");
}

} // namespace
