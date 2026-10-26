// ORION Media Center - Whether a host is on the local network, where plain http is allowed.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace hui::net
{

// A server at home (a RomM server, a NAS) is often plain http on a private
// address; anything on the internet is asked over https only. Private:
// 10/8, 172.16/12, 192.168/16, 169.254/16, 127/8, and names with no dot or
// ending .local, .lan, .home or .internal.
inline bool is_local_host(std::string_view host)
{
    if (host.empty())
        return false;
    std::string name(host);
    for (char &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    int parts[4] = {-1, -1, -1, -1};
    int count = 0;
    int value = -1;
    bool numeric = true;
    for (const char c : name)
    {
        if (c >= '0' && c <= '9')
        {
            value = (value < 0 ? 0 : value) * 10 + (c - '0');
            if (value > 255)
                numeric = false;
        }
        else if (c == '.' && value >= 0 && count < 3)
        {
            parts[count++] = value;
            value = -1;
        }
        else
            numeric = false;
    }
    if (numeric && count == 3 && value >= 0)
    {
        parts[3] = value;
        return parts[0] == 10 || parts[0] == 127 || (parts[0] == 192 && parts[1] == 168) ||
               (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31) ||
               (parts[0] == 169 && parts[1] == 254);
    }
    if (name.find('.') == std::string::npos)
        return true; // "nas", "localhost": a name only the local network knows
    for (const char *ending : {".local", ".lan", ".home", ".internal"})
    {
        const std::string_view tail(ending);
        if (name.size() > tail.size() &&
            name.compare(name.size() - tail.size(), tail.size(), tail) == 0)
            return true;
    }
    return false;
}

// The host of an http:// or https:// URL ("192.168.4.20" of
// "http://192.168.4.20:8080/api"), lowercased; empty for anything else.
inline std::string url_host(std::string_view url)
{
    std::size_t start = 0;
    if (url.rfind("http://", 0) == 0)
        start = 7;
    else if (url.rfind("https://", 0) == 0)
        start = 8;
    else
        return {};
    std::size_t end = url.find_first_of(":/?#", start);
    if (end == std::string_view::npos)
        end = url.size();
    std::string host(url.substr(start, end - start));
    for (char &c : host)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return host;
}

} // namespace hui::net
