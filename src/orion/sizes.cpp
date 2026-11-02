// ORION Media Center - Titles' sizes as the system keeps them.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/sizes.hpp"

#include "core/save_file.hpp"

namespace hui::orion
{

std::unordered_map<std::string, std::uint64_t> parse_sizes(std::string_view text)
{
    std::unordered_map<std::string, std::uint64_t> sizes;
    for (std::size_t start = 0; start < text.size();)
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        const std::size_t tab = line.find('\t');
        if (tab == 0 || tab == std::string_view::npos || tab + 1 >= line.size())
            continue;
        std::uint64_t bytes = 0;
        bool digits = true;
        for (const char c : line.substr(tab + 1))
        {
            if (c == '\r')
                break;
            if (c < '0' || c > '9' || bytes > (UINT64_MAX - 9) / 10)
            {
                digits = false;
                break;
            }
            bytes = bytes * 10 + static_cast<std::uint64_t>(c - '0');
        }
        if (digits && bytes > 0)
            sizes[std::string(line.substr(0, tab))] = bytes;
    }
    return sizes;
}

std::unordered_map<std::string, std::uint64_t> read_sizes(const std::string &state_dir)
{
    std::string text;
    if (state_dir.empty() || !save::read_file(state_dir + "/sizes.txt", &text, 1u << 20))
        return {};
    return parse_sizes(text);
}

} // namespace hui::orion
