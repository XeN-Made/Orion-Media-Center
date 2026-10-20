// ps5-homebrew-ui - ORION's play records: how long and how lately each game was played.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace hui::orion
{

// What the launch helper saw of a game started from ORION.
struct PlayRecord
{
    std::int64_t seconds = 0; // all sessions together
    std::int64_t last = 0;    // when the latest session ended (seconds since 1970)
    int sessions = 0;
};

// By item id: a title ID, or "rom-..." for a game for an emulator.
using PlayLog = std::map<std::string, PlayRecord>;

// play-log.txt, which the launch helper appends one line to per session:
// "<start> <end> <id>". Lines that do not read as one are skipped.
PlayLog read_play_log(std::string_view text);

// "Under a minute", "45 m", "3 h 20 m", "120 h".
std::string play_time_text(std::int64_t seconds);
// "today", "yesterday", "3 days ago", "2 weeks ago", "5 months ago", "a year ago".
std::string ago_text(std::int64_t then, std::int64_t now);

} // namespace hui::orion
