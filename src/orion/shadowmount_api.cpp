// ORION Media Center - ShadowMountPlus's HTTP API: the games it knows, asked directly.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/shadowmount_api.hpp"

#include "library/json.hpp"

#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace hui::orion
{

namespace
{

// Lower case, for header names.
std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return out;
}

} // namespace

bool parse_http_answer(std::string_view raw, int *status, std::string *body)
{
    const std::size_t end = raw.find("\r\n\r\n");
    if (raw.compare(0, 5, "HTTP/") != 0 || end == std::string_view::npos)
        return false;
    const std::size_t space = raw.find(' ');
    if (space == std::string_view::npos || space > end)
        return false;
    *status = std::atoi(std::string(raw.substr(space + 1, 3)).c_str());
    const std::string headers = lower(raw.substr(0, end));
    std::string_view rest = raw.substr(end + 4);
    body->clear();
    if (headers.find("transfer-encoding: chunked") == std::string::npos)
    {
        body->assign(rest);
        return true;
    }
    // Chunks: a hex size line, the bytes, CRLF; a size of 0 ends them.
    while (!rest.empty())
    {
        const std::size_t line = rest.find("\r\n");
        if (line == std::string_view::npos)
            return false;
        const std::size_t size = static_cast<std::size_t>(
            std::strtoul(std::string(rest.substr(0, line)).c_str(), nullptr, 16));
        rest.remove_prefix(line + 2);
        if (size == 0)
            return true;
        if (rest.size() < size)
            return false;
        body->append(rest.substr(0, size));
        rest.remove_prefix(std::min(rest.size(), size + 2));
    }
    return true;
}

bool shadowmount_request(int port, std::string_view path, std::string_view json, std::string *body,
                         std::string *error, std::size_t limit)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        *error = "no socket";
        return false;
    }
    timeval timeout{10, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        *error = "ShadowMountPlus did not answer";
        return false;
    }
    std::string request = std::string(json.empty() ? "GET " : "POST ") + std::string(path) +
                          " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
    if (!json.empty())
        request +=
            "Content-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) +
            "\r\n";
    request += "\r\n";
    request += json;
    std::size_t sent = 0;
    while (sent < request.size())
    {
        const ssize_t wrote = ::send(fd, request.data() + sent, request.size() - sent, 0);
        if (wrote <= 0)
        {
            ::close(fd);
            *error = "ShadowMountPlus closed the connection";
            return false;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    std::string raw;
    char chunk[16384];
    for (;;)
    {
        const ssize_t got = ::recv(fd, chunk, sizeof(chunk), 0);
        if (got <= 0)
            break;
        raw.append(chunk, static_cast<std::size_t>(got));
        if (raw.size() > limit)
        {
            ::close(fd);
            *error = "too large";
            return false;
        }
    }
    ::close(fd);
    int status = 0;
    if (!parse_http_answer(raw, &status, body))
    {
        *error = "ShadowMountPlus's answer is not HTTP";
        return false;
    }
    if (status != 200)
    {
        *error = "ShadowMountPlus answered " + std::to_string(status);
        return false;
    }
    return true;
}

bool parse_shadowmount_games(std::string_view json, std::vector<library::Title> *titles)
{
    json::Value root;
    if (!json::parse(json, &root) || root["status"].number_or(-1.0) != 0.0)
        return false;
    const json::Value &games = root["games"];
    for (std::size_t i = 0; i < games.size(); ++i)
    {
        const json::Value &game = games[i];
        library::Title title;
        title.id = game["title_id"].string_or("");
        title.name = game["title_name"].string_or("");
        title.version = game["version"].string_or("");
        const std::string platform = game["platform"].string_or("");
        if (!library::is_title_id(title.id) || title.name.empty() ||
            !game["installed"].bool_or(false) || (platform != "ps4" && platform != "ps5"))
            continue;
        title.platform = platform == "ps4" ? library::Platform::ps4 : library::Platform::ps5;
        title.path = game["path"].string_or("");
        title.source = library::Source::installed;
        titles->push_back(std::move(title));
    }
    return true;
}

} // namespace hui::orion
