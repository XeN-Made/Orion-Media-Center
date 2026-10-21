// ORION Media Center - Tests: ORION's play records.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/playtime.hpp"

#include <gtest/gtest.h>

namespace hui
{
namespace
{

TEST(Playtime, AddsUpSessionsPerGame)
{
    const orion::PlayLog log = orion::read_play_log("1000 4600 PPSA02343\n"
                                                    "5000 5900 PPSA02343\n"
                                                    "7000 7300 rom-0123456789ab\n"
                                                    "garbage line\n"
                                                    "9000 8000 PPSA02343\n" // ends before it starts
                                                    "1 2 not-an-id\n"
                                                    "100 200 PPSA99505"); // no newline at the end
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log.at("PPSA02343").seconds, 4500);
    EXPECT_EQ(log.at("PPSA02343").last, 5900);
    EXPECT_EQ(log.at("PPSA02343").sessions, 2);
    EXPECT_EQ(log.at("rom-0123456789ab").seconds, 300);
    EXPECT_EQ(log.at("PPSA99505").sessions, 1);
}

TEST(Playtime, SaysHowLongAndHowLately)
{
    EXPECT_EQ(orion::play_time_text(30), "Under a minute");
    EXPECT_EQ(orion::play_time_text(45 * 60), "45 m");
    EXPECT_EQ(orion::play_time_text(3 * 3600 + 20 * 60), "3 h 20 m");
    EXPECT_EQ(orion::play_time_text(2 * 3600), "2 h");
    EXPECT_EQ(orion::play_time_text(150 * 3600 + 59), "150 h");
    const std::int64_t now = 100 * 86400;
    EXPECT_EQ(orion::ago_text(now - 3600, now), "today");
    EXPECT_EQ(orion::ago_text(now - 86400, now), "yesterday");
    EXPECT_EQ(orion::ago_text(now - 3 * 86400, now), "3 days ago");
    EXPECT_EQ(orion::ago_text(now - 21 * 86400, now), "3 weeks ago");
    EXPECT_EQ(orion::ago_text(now - 90 * 86400, now), "3 months ago");
    EXPECT_EQ(orion::ago_text(0, 800 * 86400), "2 years ago");
}

} // namespace
} // namespace hui
