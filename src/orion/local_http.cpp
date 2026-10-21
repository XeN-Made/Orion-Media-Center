// ps5-homebrew-ui - A small HTTP client for services on the console itself (127.0.0.1).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/local_http.hpp"

#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace hui::orion
{

namespace
{

constexpr int kTimeoutSeconds = 10;
constexpr std::size_t kMaxReply = 1u << 20; // the API answers in a few kilobytes

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// A chunked body put together: hex size, CRLF, data, CRLF, ..., 0.
bool unchunk(std::string_view body, std::string *out)
{
    out->clear();
    std::size_t at = 0;
    for (;;)
    {
        const std::size_t line = body.find("\r\n", at);
        if (line == std::string_view::npos)
            return false;
        const std::string size_text(body.substr(at, line - at));
        char *end = nullptr;
        const unsigned long size = std::strtoul(size_text.c_str(), &end, 16);
        if (end == size_text.c_str())
            return false;
        at = line + 2;
        if (size == 0)
            return true;
        if (at + size > body.size())
            return false;
        out->append(body.substr(at, size));
        at += size + 2; // the CRLF after the data
    }
}

} // namespace

bool parse_http_reply(std::string_view raw, HttpReply *reply)
{
    *reply = HttpReply();
    if (raw.substr(0, 5) != "HTTP/")
        return false;
    const std::size_t space = raw.find(' ');
    const std::size_t head_end = raw.find("\r\n\r\n");
    if (space == std::string_view::npos || head_end == std::string_view::npos || space > head_end)
        return false;
    reply->status = std::atoi(std::string(raw.substr(space + 1, 3)).c_str());
    if (reply->status < 100)
        return false;
    const std::string head = lower(raw.substr(0, head_end));
    std::string_view body = raw.substr(head_end + 4);
    if (head.find("transfer-encoding: chunked") != std::string::npos)
        return unchunk(body, &reply->body);
    const std::size_t length = head.find("content-length:");
    if (length != std::string::npos)
    {
        const std::size_t n = static_cast<std::size_t>(std::atoll(head.c_str() + length + 15));
        if (n < body.size())
            body = body.substr(0, n);
    }
    reply->body = std::string(body);
    return true;
}

bool post_local_json(int port, const std::string &path, const std::string &json, HttpReply *reply,
                     std::string *error)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        *error = "no socket";
        return false;
    }
    timeval timeout = {};
    timeout.tv_sec = kTimeoutSeconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        *error = "nothing answers on port " + std::to_string(port);
        return false;
    }
    const std::string request = "POST " + path +
                                " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                                "Content-Length: " +
                                std::to_string(json.size()) + "\r\nConnection: close\r\n\r\n" +
                                json;
    std::string_view rest = request;
    while (!rest.empty())
    {
        const ssize_t sent = ::send(fd, rest.data(), rest.size(), 0);
        if (sent <= 0)
        {
            ::close(fd);
            *error = "the request could not be sent";
            return false;
        }
        rest.remove_prefix(static_cast<std::size_t>(sent));
    }
    // On the heap: this runs on a worker thread, whose stack is small on the
    // console (orion/local_ftp.cpp found that out).
    std::vector<char> buffer(16 * 1024);
    std::string raw;
    for (;;)
    {
        const ssize_t got = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (got <= 0 || raw.size() > kMaxReply)
            break;
        raw.append(buffer.data(), static_cast<std::size_t>(got));
    }
    ::close(fd);
    if (!parse_http_reply(raw, reply))
    {
        *error = raw.empty() ? "no answer" : "the answer was not HTTP";
        return false;
    }
    return true;
}

} // namespace hui::orion
