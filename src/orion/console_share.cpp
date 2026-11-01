// ORION Media Center - Sharing the console's drives: what ORION asks the share payload for.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/console_share.hpp"

#include "core/i18n.hpp"
#include "core/save_file.hpp"

namespace hui::orion
{

namespace
{

constexpr const char *kFile = "/smb-share.txt";
constexpr const char *kStatusFile = "/smb-share-status.txt";

// A value on one line: no line breaks or tabs get into the file.
std::string one_line(const std::string &text)
{
    std::string out;
    for (const char c : text)
    {
        if (c != '\n' && c != '\r' && c != '\t')
            out += c;
    }
    return out;
}

} // namespace

ConsoleShare read_console_share(const std::string &state_dir)
{
    ConsoleShare share;
    std::string text;
    if (!save::read_file(state_dir + kFile, &text, 16u << 10))
        return share;
    for (std::size_t start = 0; start < text.size();)
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos)
            continue;
        const std::string key = line.substr(0, tab);
        const std::string value = line.substr(tab + 1);
        if (key == "on")
            share.on = value == "1";
        else if (key == "user")
            share.user = value;
        else if (key == "password")
            share.password = value;
    }
    return share;
}

bool write_console_share(const std::string &state_dir, const ConsoleShare &share)
{
    const std::string text = std::string("on\t") + (share.on ? "1" : "0") + "\nuser\t" +
                             one_line(share.user) + "\npassword\t" + one_line(share.password) +
                             "\n";
    return save::write_atomic(state_dir + kFile, text).empty();
}

std::string console_share_problem(const std::string &state_dir)
{
    std::string text;
    if (!save::read_file(state_dir + kStatusFile, &text, 4096))
        return {};
    const std::size_t end = text.find('\n');
    const std::string line = text.substr(0, end);
    const std::size_t tab = line.find('\t');
    const std::string state = line.substr(0, tab);
    const std::string detail = tab == std::string::npos ? std::string() : line.substr(tab + 1);
    if (state == "busy")
        return i18n::tr("Another app already shares this console's drives (port 445 is taken)");
    if (state == "error")
        return i18n::trf("Could not share: {0}", {detail});
    if (state == "nopassword")
        return i18n::tr("Set a password first");
    return {};
}

} // namespace hui::orion
