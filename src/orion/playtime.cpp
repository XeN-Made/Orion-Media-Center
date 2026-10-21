// ORION Media Center - ORION's play records: how long and how lately each game was played.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/playtime.hpp"

#include "library/library.hpp"

#include <algorithm>
#include <charconv>

namespace hui::orion
{

namespace
{

bool number(std::string_view text, std::int64_t *value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), *value);
    return error == std::errc() && end == text.data() + text.size();
}

} // namespace

PlayLog read_play_log(std::string_view text)
{
    PlayLog log;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        const std::size_t first = line.find(' ');
        const std::size_t second =
            first == std::string_view::npos ? first : line.find(' ', first + 1);
        if (second == std::string_view::npos)
            continue;
        std::int64_t from = 0;
        std::int64_t to = 0;
        const std::string_view id = line.substr(second + 1);
        if (!number(line.substr(0, first), &from) ||
            !number(line.substr(first + 1, second - first - 1), &to) || to < from ||
            to - from > 7 * 24 * 3600 || !library::is_item_id(std::string(id)))
            continue;
        PlayRecord &record = log[std::string(id)];
        record.seconds += to - from;
        record.last = std::max(record.last, to);
        ++record.sessions;
    }
    return log;
}

std::string play_time_text(std::int64_t seconds)
{
    if (seconds < 60)
        return "Under a minute";
    const std::int64_t minutes = seconds / 60;
    if (minutes < 60)
        return std::to_string(minutes) + " m";
    const std::int64_t hours = minutes / 60;
    if (hours >= 100)
        return std::to_string(hours) + " h";
    const std::int64_t rest = minutes % 60;
    return std::to_string(hours) + " h" + (rest > 0 ? " " + std::to_string(rest) + " m" : "");
}

std::string ago_text(std::int64_t then, std::int64_t now)
{
    const std::int64_t days = std::max<std::int64_t>(0, now - then) / 86400;
    if (days == 0)
        return "today";
    if (days == 1)
        return "yesterday";
    if (days < 14)
        return std::to_string(days) + " days ago";
    if (days < 60)
        return std::to_string(days / 7) + " weeks ago";
    if (days < 365)
        return std::to_string(days / 30) + " months ago";
    return days < 730 ? "a year ago" : std::to_string(days / 365) + " years ago";
}

} // namespace hui::orion
