// ps5-homebrew-ui - Box art matching: a ROM's file name against the libretro thumbnail sets.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/art_match.hpp"

#include "library/emulators.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <tuple>

namespace hui::library
{

namespace
{

constexpr const char *kPreferred[] = {"usa", "world", "europe", "japan"};

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string stem_of(std::string_view file)
{
    const std::size_t dot = file.rfind('.');
    return std::string(dot == std::string_view::npos || dot == 0 ? file : file.substr(0, dot));
}

// GoodTools codes and No-Intro names alike: "(U)", "(USA, Europe)", "(J)".
std::string region_of(std::string_view word)
{
    struct Code
    {
        const char *word;
        const char *region;
    };
    static constexpr Code kCodes[] = {
        {"u", "usa"},       {"us", "usa"},    {"usa", "usa"},     {"ue", "usa"},
        {"ju", "usa"},      {"e", "europe"},  {"eu", "europe"},   {"europe", "europe"},
        {"j", "japan"},     {"jp", "japan"},  {"japan", "japan"}, {"w", "world"},
        {"world", "world"}, {"jue", "world"}, {"k", "korea"},     {"korea", "korea"},
    };
    for (const Code &code : kCodes)
    {
        if (word == code.word)
            return code.region;
    }
    return "";
}

std::vector<std::string> regions_of(std::string_view name)
{
    std::vector<std::string> found;
    std::size_t open = name.find('(');
    while (open != std::string_view::npos)
    {
        const std::size_t close = name.find(')', open);
        if (close == std::string_view::npos)
            break;
        const std::string tag = lower(name.substr(open + 1, close - open - 1));
        std::size_t start = 0;
        while (start <= tag.size())
        {
            std::size_t end = tag.find_first_of(", ", start);
            if (end == std::string::npos)
                end = tag.size();
            const std::string region = region_of(std::string_view(tag).substr(start, end - start));
            if (!region.empty() && std::find(found.begin(), found.end(), region) == found.end())
                found.push_back(region);
            start = end + 1;
        }
        open = name.find('(', close);
    }
    return found;
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string url_decode(std::string_view text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size() && hex_value(text[i + 1]) >= 0 &&
            hex_value(text[i + 2]) >= 0)
        {
            out += static_cast<char>(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2]));
            i += 2;
        }
        else
            out += text[i];
    }
    return out;
}

void replace_all(std::string &text, std::string_view from, std::string_view to)
{
    std::size_t at = text.find(from);
    while (at != std::string::npos)
    {
        text.replace(at, from.size(), to);
        at = text.find(from, at + to.size());
    }
}

} // namespace

std::vector<std::string> parse_art_index(std::string_view html)
{
    std::vector<std::string> pictures;
    std::size_t at = html.find("href=\"");
    while (at != std::string_view::npos)
    {
        const std::size_t start = at + 6;
        const std::size_t end = html.find('"', start);
        if (end == std::string_view::npos)
            break;
        const std::string name = url_decode(html.substr(start, end - start));
        if (name.size() > 4 && lower(name.substr(name.size() - 4)) == ".png" &&
            name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
            name.front() != '.')
            pictures.push_back(name);
        at = html.find("href=\"", end);
    }
    return pictures;
}

std::string art_key(std::string_view name)
{
    // Tags out, then a scene release number in front.
    std::string text;
    int depth = 0;
    for (char c : name)
    {
        if (c == '(' || c == '[')
            ++depth;
        else if ((c == ')' || c == ']') && depth > 0)
            --depth;
        else if (depth == 0)
            text += c;
    }
    while (!text.empty() && text.front() == ' ')
        text.erase(0, 1);
    if (text.size() > 7 && std::isdigit(static_cast<unsigned char>(text[0])) != 0 &&
        std::isdigit(static_cast<unsigned char>(text[1])) != 0 &&
        std::isdigit(static_cast<unsigned char>(text[2])) != 0 &&
        std::isdigit(static_cast<unsigned char>(text[3])) != 0 && text.compare(4, 3, " - ") == 0)
        text.erase(0, 7);
    text = lower(text);
    // "Legend of Zelda, The - ..." and "The Legend of Zelda - ..." are one game.
    replace_all(text, ", the ", " ");
    if (text.size() >= 5 && text.compare(text.size() - 5, 5, ", the") == 0)
        text.erase(text.size() - 5);
    if (text.rfind("the ", 0) == 0)
        text.erase(0, 4);
    replace_all(text, "&", " and ");
    std::string key;
    for (char c : text)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0)
            key += c;
    }
    return key;
}

std::string match_art(std::string_view rom_file, const std::vector<std::string> &pictures)
{
    const std::string stem = stem_of(rom_file);
    const std::string exact = thumbnail_name(stem) + ".png";
    if (std::find(pictures.begin(), pictures.end(), exact) != pictures.end())
        return exact;
    const std::string want = art_key(stem);
    if (want.empty())
        return "";
    std::vector<std::string> order = regions_of(stem);
    for (const char *region : kPreferred)
    {
        if (std::find(order.begin(), order.end(), region) == order.end())
            order.emplace_back(region);
    }
    const auto rank = [&](const std::string &picture)
    {
        std::size_t best = order.size();
        for (const std::string &region : regions_of(picture))
        {
            const auto at = std::find(order.begin(), order.end(), region);
            best = std::min(best, static_cast<std::size_t>(at - order.begin()));
        }
        // Fewer extra tags first: "(USA)" before "(USA) (Beta)".
        const auto tags = static_cast<std::size_t>(std::count(picture.begin(), picture.end(), '('));
        return std::make_tuple(best, tags, picture.size(), picture);
    };
    const std::string *chosen = nullptr;
    for (const std::string &picture : pictures)
    {
        if (art_key(stem_of(picture)) != want)
            continue;
        if (chosen == nullptr || rank(picture) < rank(*chosen))
            chosen = &picture;
    }
    return chosen == nullptr ? std::string() : *chosen;
}

std::string url_encode(std::string_view text)
{
    std::string out;
    for (char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || c == '-' || c == '_' || c == '.' || c == '~')
            out += c;
        else
        {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", byte);
            out += escaped;
        }
    }
    return out;
}

} // namespace hui::library
