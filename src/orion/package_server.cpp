// ORION Media Center - Serves one package to the console's installer, streamed from its drive.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/package_server.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hui::orion
{

namespace
{

long long now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool send_all(int fd, const char *data, std::size_t size)
{
    while (size > 0)
    {
        const ssize_t put = ::send(fd, data, size, MSG_NOSIGNAL);
        if (put < 0 && errno == EINTR)
            continue;
        if (put <= 0)
            return false;
        data += put;
        size -= static_cast<std::size_t>(put);
    }
    return true;
}

// One request's head, or empty when the connection ends or stays quiet.
std::string read_head(int fd, std::string *buffer, const std::atomic<bool> &stopping)
{
    for (;;)
    {
        const std::size_t end = buffer->find("\r\n\r\n");
        if (end != std::string::npos)
        {
            std::string head = buffer->substr(0, end);
            buffer->erase(0, end + 4);
            return head;
        }
        if (buffer->size() > 16 * 1024 || stopping.load())
            return {};
        // The installer keeps a connection open between ranges a while; it
        // is looked at twice a second, so stopping never waits on it.
        int quiet = 0;
        pollfd wait = {fd, POLLIN, 0};
        while (::poll(&wait, 1, 500) == 0)
        {
            if (stopping.load() || ++quiet > 120)
                return {};
        }
        if ((wait.revents & POLLIN) == 0)
            return {};
        char chunk[4096];
        const ssize_t got = ::recv(fd, chunk, sizeof(chunk), 0);
        if (got <= 0)
            return {};
        buffer->append(chunk, static_cast<std::size_t>(got));
    }
}

std::string header_value(const std::string &head, const char *name)
{
    std::string lower = head;
    for (char &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string key = std::string("\r\n") + name + ":";
    const std::size_t at = lower.find(key);
    if (at == std::string::npos)
        return {};
    std::size_t start = at + key.size();
    while (start < head.size() && head[start] == ' ')
        ++start;
    const std::size_t end = head.find("\r\n", start);
    return head.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

} // namespace

bool parse_range(const std::string &header, std::uint64_t size, std::uint64_t *first,
                 std::uint64_t *last)
{
    if (size == 0 || header.rfind("bytes=", 0) != 0 || header.find(',') != std::string::npos)
        return false;
    const std::string spec = header.substr(6);
    const std::size_t dash = spec.find('-');
    if (dash == std::string::npos)
        return false;
    const std::string from = spec.substr(0, dash);
    const std::string to = spec.substr(dash + 1);
    const auto number = [](const std::string &text, std::uint64_t *value)
    {
        if (text.empty() || text.size() > 19)
            return false;
        std::uint64_t n = 0;
        for (const char c : text)
        {
            if (c < '0' || c > '9')
                return false;
            n = n * 10 + static_cast<std::uint64_t>(c - '0');
        }
        *value = n;
        return true;
    };
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    if (from.empty())
    {
        // The last bytes.
        if (!number(to, &b) || b == 0)
            return false;
        *first = b >= size ? 0 : size - b;
        *last = size - 1;
        return true;
    }
    if (!number(from, &a) || a >= size)
        return false;
    *first = a;
    *last = to.empty() || !number(to, &b) || b >= size ? size - 1 : b;
    return *last >= *first;
}

PackageServer::~PackageServer()
{
    stop();
}

bool PackageServer::start(std::string token, std::uint64_t size, PackageOpener open, int port)
{
    stop();
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return false;
    int on = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 8) != 0)
    {
        ::close(listener);
        return false;
    }
    token_ = std::move(token);
    size_ = size;
    port_ = port;
    open_ = std::move(open);
    sent_ = 0;
    last_ms_ = now_ms();
    stopping_ = false;
    running_ = true;
    listener_ = listener;
    listener_thread_ = std::thread([this, listener]() { listen_loop(listener); });
    return true;
}

void PackageServer::stop()
{
    if (!running_.load() && !listener_thread_.joinable())
        return;
    stopping_ = true;
    if (listener_thread_.joinable())
        listener_thread_.join();
    std::vector<std::thread> clients;
    {
        const std::lock_guard<std::mutex> lock(clients_mutex_);
        clients.swap(clients_);
    }
    for (std::thread &client : clients)
    {
        if (client.joinable())
            client.join();
    }
    if (listener_ >= 0)
        ::close(listener_);
    listener_ = -1;
    running_ = false;
}

std::string PackageServer::url() const
{
    return "http://127.0.0.1:" + std::to_string(port_) + "/" + token_ + "/package.pkg";
}

int PackageServer::quiet_seconds() const
{
    return static_cast<int>((now_ms() - last_ms_.load()) / 1000);
}

void PackageServer::listen_loop(int listener)
{
    while (!stopping_.load())
    {
        pollfd wait = {listener, POLLIN, 0};
        if (::poll(&wait, 1, 250) <= 0)
            continue;
        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0)
            continue;
        const std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.emplace_back([this, client]() { serve(client); });
    }
}

void PackageServer::serve(int client)
{
    ++connections_;
    std::string error;
    std::unique_ptr<PackageSource> source;
    std::string buffer;
    std::vector<char> chunk(1u << 20);
    for (;;)
    {
        const std::string head = read_head(client, &buffer, stopping_);
        if (head.empty())
            break;
        const std::size_t line_end = head.find("\r\n");
        const std::string line = head.substr(0, line_end);
        const bool get = line.rfind("GET ", 0) == 0;
        const bool only_head = line.rfind("HEAD ", 0) == 0;
        const std::string path = "/" + token_ + "/package.pkg";
        if ((!get && !only_head) || line.find(" " + path + " ") == std::string::npos)
        {
            const std::string answer =
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all(client, answer.data(), answer.size());
            break;
        }
        std::uint64_t first = 0;
        std::uint64_t last = size_ == 0 ? 0 : size_ - 1;
        const std::string range = header_value(head, "range");
        const bool ranged = !range.empty() && parse_range(range, size_, &first, &last);
        if (!range.empty() && !ranged)
        {
            const std::string answer =
                "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" +
                std::to_string(size_) + "\r\nContent-Length: 0\r\n\r\n";
            if (!send_all(client, answer.data(), answer.size()))
                break;
            continue;
        }
        const std::uint64_t length = size_ == 0 ? 0 : last - first + 1;
        std::string answer =
            std::string(ranged ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n") +
            "Content-Type: application/octet-stream\r\nAccept-Ranges: bytes\r\n"
            "Content-Length: " +
            std::to_string(length) + "\r\n";
        if (ranged)
            answer += "Content-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) +
                      "/" + std::to_string(size_) + "\r\n";
        answer += "\r\n";
        if (!send_all(client, answer.data(), answer.size()))
            break;
        if (only_head || length == 0)
            continue;
        if (!source)
        {
            source = open_(&error);
            if (!source)
                break; // the length is promised: all that can be done is to close
        }
        std::uint64_t at = first;
        bool ok = true;
        while (at <= last && !stopping_.load())
        {
            const std::size_t want =
                static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), last - at + 1));
            const long long got = source->read(at, chunk.data(), want);
            if (got <= 0 || !send_all(client, chunk.data(), static_cast<std::size_t>(got)))
            {
                ok = false;
                break;
            }
            at += static_cast<std::uint64_t>(got);
            sent_ += static_cast<std::uint64_t>(got);
            last_ms_ = now_ms();
        }
        if (!ok || stopping_.load())
            break;
    }
    ::close(client);
    --connections_;
}

} // namespace hui::orion
