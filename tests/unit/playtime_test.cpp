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

// The system's time counts every session, ORION's own too: the larger time
// and the later date win.
TEST(Playtime, TakesTheSystemsOwnPlayTime)
{
    EXPECT_EQ(orion::parse_date_time("1970-01-01T00:00:00"), 0);
    EXPECT_EQ(orion::parse_date_time("2026-10-12T13:44:10.000Z"), 1791812650);
    EXPECT_EQ(orion::parse_date_time("2000-02-29 12:00:00"), 951825600);
    EXPECT_EQ(orion::parse_date_time("yesterday"), 0);
    EXPECT_EQ(orion::parse_date_time("2026-13-01T00:00:00"), 0);

    orion::PlayLog log = orion::read_play_log("1000 4600 PPSA02343\n");
    orion::merge_system_play_time("PPSA02343\t9000\t2026-10-12T13:44:10\n"
                                  "CUSA00001\t60\t\n"                   // no date
                                  "PPSA02344\t0\t2026-10-12T13:44:10\n" // never played
                                  "rom-0123456789ab\t100\t\n"           // not the system's
                                  "garbage\n"
                                  "PPSA99505\t120\t2026-10-01T00:00:00",
                                  &log);
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log.at("PPSA02343").seconds, 9000);
    EXPECT_EQ(log.at("PPSA02343").last, 1791812650);
    EXPECT_EQ(log.at("PPSA02343").sessions, 1);
    EXPECT_EQ(log.at("CUSA00001").seconds, 60);
    EXPECT_EQ(log.at("CUSA00001").last, 0);
    EXPECT_EQ(log.at("PPSA99505").seconds, 120);

    // ORION's own record stays when it is the larger.
    orion::merge_system_play_time("PPSA02343\t10\t1970-01-01T00:00:00\n", &log);
    EXPECT_EQ(log.at("PPSA02343").seconds, 9000);
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
