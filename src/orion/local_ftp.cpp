// ORION Media Center - A small FTP client for the console's own FTP server.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/local_ftp.hpp"

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

constexpr int kTimeoutSeconds = 5;

int connect_loopback(int port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
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
        return -1;
    }
    return fd;
}

bool send_all(int fd, std::string_view text)
{
    while (!text.empty())
    {
        const ssize_t sent = ::send(fd, text.data(), text.size(), 0);
        if (sent <= 0)
            return false;
        text.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

} // namespace

int reply_code(std::string_view text, std::size_t *end)
{
    std::size_t start = 0;
    while (start < text.size())
    {
        const std::size_t line_end = text.find('\n', start);
        if (line_end == std::string_view::npos)
            return 0;
        const std::string_view line = text.substr(start, line_end - start);
        start = line_end + 1;
        // The last line of a reply: three digits and a space.
        if (line.size() >= 4 && std::isdigit(static_cast<unsigned char>(line[0])) != 0 &&
            std::isdigit(static_cast<unsigned char>(line[1])) != 0 &&
            std::isdigit(static_cast<unsigned char>(line[2])) != 0 && line[3] == ' ')
        {
            *end = start;
            return (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
        }
    }
    return 0;
}

int passive_port(std::string_view reply)
{
    if (reply.rfind("227", 0) != 0)
        return 0;
    const std::size_t open = reply.find('(');
    const std::size_t close = reply.find(')', open);
    if (open == std::string_view::npos || close == std::string_view::npos)
        return 0;
    int numbers[6] = {};
    int count = 0;
    std::size_t at = open + 1;
    while (count < 6 && at < close)
    {
        int value = 0;
        bool digits = false;
        while (at < close && std::isdigit(static_cast<unsigned char>(reply[at])) != 0)
        {
            value = value * 10 + (reply[at] - '0');
            digits = true;
            ++at;
        }
        if (!digits || value > 255)
            return 0;
        numbers[count++] = value;
        if (at < close && reply[at] == ',')
            ++at;
    }
    return count == 6 ? numbers[4] * 256 + numbers[5] : 0;
}

LocalFtp::~LocalFtp()
{
    close();
}

int LocalFtp::reply()
{
    char buffer[1024];
    for (;;)
    {
        std::size_t end = 0;
        const int code = reply_code(pending_, &end);
        if (code != 0)
        {
            reply_ = pending_.substr(0, end);
            pending_.erase(0, end);
            return code;
        }
        const ssize_t got = ::recv(control_, buffer, sizeof(buffer), 0);
        if (got <= 0 || pending_.size() > 64 * 1024)
            return 0;
        pending_.append(buffer, static_cast<std::size_t>(got));
    }
}

bool LocalFtp::command(const std::string &line, int expect_class)
{
    if (control_ < 0 || !send_all(control_, line + "\r\n"))
        return false;
    return reply() / 100 == expect_class;
}

bool LocalFtp::open(int port)
{
    close();
    control_ = connect_loopback(port);
    if (control_ < 0 || reply() / 100 != 2)
    {
        close();
        return false;
    }
    if (!send_all(control_, "USER anonymous\r\n"))
    {
        close();
        return false;
    }
    int code = reply();
    if (code / 100 == 3)
    {
        if (!send_all(control_, "PASS orion@\r\n"))
        {
            close();
            return false;
        }
        code = reply();
    }
    if (code / 100 != 2 || !command("TYPE I", 2))
    {
        close();
        return false;
    }
    return true;
}

bool LocalFtp::get(const std::string &path, std::size_t limit, std::string *bytes)
{
    return transfer("RETR " + path, limit, bytes);
}

bool LocalFtp::list(const std::string &folder, std::vector<FtpEntry> *entries)
{
    entries->clear();
    std::string text;
    if (control_ < 0 || !send_all(control_, "CWD " + folder + "\r\n") || reply() / 100 != 2 ||
        !transfer("MLSD", 4u << 20, &text))
        return false;
    *entries = parse_mlsd(text);
    return true;
}

std::vector<FtpEntry> parse_mlsd(std::string_view text)
{
    std::vector<FtpEntry> entries;
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        // The facts end at the first space; the name is everything after it.
        const std::size_t space = line.find(' ');
        if (space == std::string_view::npos || space == 0 || line[space - 1] != ';')
            continue;
        const std::string_view facts = line.substr(0, space);
        FtpEntry entry;
        entry.name = std::string(line.substr(space + 1));
        if (entry.name.empty() || entry.name == "." || entry.name == "..")
            continue;
        std::string lowered(facts);
        for (char &c : lowered)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        entry.folder = lowered.find("type=dir;") != std::string::npos;
        if (lowered.find("type=cdir;") != std::string::npos ||
            lowered.find("type=pdir;") != std::string::npos)
            continue;
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool LocalFtp::transfer(const std::string &line, std::size_t limit, std::string *bytes)
{
    bytes->clear();
    if (control_ < 0 || !send_all(control_, "PASV\r\n"))
        return false;
    const int code = reply();
    const int port = code == 227 ? passive_port(reply_) : 0;
    if (port == 0)
        return false;
    const int data = connect_loopback(port);
    if (data < 0)
        return false;
    if (!send_all(control_, line + "\r\n") || reply() / 100 != 1)
    {
        ::close(data);
        return false; // 550: no such file
    }
    // On the heap: this runs on a worker thread, and a console thread's
    // stack is far smaller than a PC's (64 KB here overflowed it).
    std::vector<char> buffer(64 * 1024);
    bool whole = true;
    for (;;)
    {
        const ssize_t got = ::recv(data, buffer.data(), buffer.size(), 0);
        if (got == 0)
            break;
        if (got < 0 || bytes->size() + static_cast<std::size_t>(got) > limit)
        {
            whole = false;
            break;
        }
        bytes->append(buffer.data(), static_cast<std::size_t>(got));
    }
    ::close(data);
    const bool done = reply() / 100 == 2; // 226: transfer complete
    if (!whole || !done)
        bytes->clear();
    // An empty folder lists nothing and is still a listing.
    return whole && done && (!bytes->empty() || line == "MLSD");
}

void LocalFtp::close()
{
    if (control_ >= 0)
    {
        send_all(control_, "QUIT\r\n");
        ::close(control_);
    }
    control_ = -1;
    pending_.clear();
}

} // namespace hui::orion
