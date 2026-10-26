// ORION Media Center - ORION's play records: how long and how lately each game was played.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/playtime.hpp"

#include "core/i18n.hpp"
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

std::int64_t parse_date_time(std::string_view text)
{
    if (text.size() < 19 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != ' ') || text[13] != ':' || text[16] != ':')
        return 0;
    std::int64_t year = 0;
    std::int64_t month = 0;
    std::int64_t day = 0;
    std::int64_t hour = 0;
    std::int64_t minute = 0;
    std::int64_t second = 0;
    if (!number(text.substr(0, 4), &year) || !number(text.substr(5, 2), &month) ||
        !number(text.substr(8, 2), &day) || !number(text.substr(11, 2), &hour) ||
        !number(text.substr(14, 2), &minute) || !number(text.substr(17, 2), &second) ||
        year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
        second > 60)
        return 0;
    // Days from the civil date (Howard Hinnant's days_from_civil).
    const std::int64_t y = month <= 2 ? year - 1 : year;
    const std::int64_t era = y / 400;
    const std::int64_t of_era = y - era * 400;
    const std::int64_t of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const std::int64_t of_cycle = of_era * 365 + of_era / 4 - of_era / 100 + of_year;
    const std::int64_t days = era * 146097 + of_cycle - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

void merge_system_play_time(std::string_view text, PlayLog *log)
{
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        const std::size_t first = line.find('\t');
        const std::size_t second =
            first == std::string_view::npos ? first : line.find('\t', first + 1);
        std::int64_t seconds = 0;
        const std::string id(line.substr(0, first));
        if (second == std::string_view::npos ||
            !number(line.substr(first + 1, second - first - 1), &seconds) || seconds <= 0 ||
            !library::is_title_id(id))
            continue;
        PlayRecord &record = (*log)[id];
        record.seconds = std::max(record.seconds, seconds);
        record.last = std::max(record.last, parse_date_time(line.substr(second + 1)));
    }
}

std::string play_time_text(std::int64_t seconds)
{
    if (seconds < 60)
        return i18n::tr("Under a minute");
    const std::int64_t minutes = seconds / 60;
    if (minutes < 60)
        return i18n::trf("{0} m", {std::to_string(minutes)});
    const std::int64_t hours = minutes / 60;
    if (hours >= 100)
        return i18n::trf("{0} h", {std::to_string(hours)});
    const std::int64_t rest = minutes % 60;
    return rest > 0 ? i18n::trf("{0} h {1} m", {std::to_string(hours), std::to_string(rest)})
                    : i18n::trf("{0} h", {std::to_string(hours)});
}

std::string ago_text(std::int64_t then, std::int64_t now)
{
    const std::int64_t days = std::max<std::int64_t>(0, now - then) / 86400;
    if (days == 0)
        return i18n::tr("today");
    if (days == 1)
        return i18n::tr("yesterday");
    if (days < 14)
        return i18n::trf("{0} days ago", {std::to_string(days)});
    if (days < 60)
        return i18n::trf("{0} weeks ago", {std::to_string(days / 7)});
    if (days < 365)
        return i18n::trf("{0} months ago", {std::to_string(days / 30)});
    return days < 730 ? i18n::tr("a year ago")
                      : i18n::trf("{0} years ago", {std::to_string(days / 365)});
}

} // namespace hui::orion
