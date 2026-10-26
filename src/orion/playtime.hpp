// ORION Media Center - ORION's play records: how long and how lately each game was played.
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

// The system's own play time (system-playtime.txt, which payload/playtime
// writes from the console's database: "<title ID>\t<seconds>\t<last played>"
// a line, the date as "2026-10-12T13:44:10..."), merged into *log. It counts
// every session, those started from ORION too: a title takes the larger
// time and the later date.
void merge_system_play_time(std::string_view text, PlayLog *log);

// "2026-10-12T13:44:10" or "2026-10-12 13:44:10" (UTC, anything after the
// seconds ignored) in seconds since 1970; 0 when it does not read as one.
std::int64_t parse_date_time(std::string_view text);

// "Under a minute", "45 m", "3 h 20 m", "120 h".
std::string play_time_text(std::int64_t seconds);
// "today", "yesterday", "3 days ago", "2 weeks ago", "5 months ago", "a year ago".
std::string ago_text(std::int64_t then, std::int64_t now);

} // namespace hui::orion
