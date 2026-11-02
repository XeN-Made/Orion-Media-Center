// ORION Media Center - Tests: titles' sizes as the play-time helper writes them.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/sizes.hpp"

#include <gtest/gtest.h>

namespace
{

TEST(OrionSizes, ReadsALineATitle)
{
    const auto sizes = hui::orion::parse_sizes("PPSA01325\t12345678901\r\n"
                                               "CUSA00495\t52000000000\n"
                                               "PPSA99062\t0\n"      // no size: left out
                                               "PPSA00001\tbroken\n" // not a number
                                               "\t42\n"              // no ID
                                               "PPSA00002 99\n"      // no tab
                                               "PPSA00003\t99999999999999999999999\n" // too big
                                               "CUSA00001\t7");
    EXPECT_EQ(sizes.size(), 3u);
    EXPECT_EQ(sizes.at("PPSA01325"), 12345678901u);
    EXPECT_EQ(sizes.at("CUSA00495"), 52000000000u);
    EXPECT_EQ(sizes.at("CUSA00001"), 7u);
    EXPECT_TRUE(hui::orion::read_sizes("").empty());
    EXPECT_TRUE(hui::orion::read_sizes(::testing::TempDir() + "no-such-folder").empty());
}

} // namespace
