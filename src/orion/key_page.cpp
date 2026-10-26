// ORION Media Center - The key page: ORION's SteamGridDB key typed on a phone or PC.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/key_page.hpp"

#include "orion/steamgriddb.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxRequest = 16u << 10;

std::string response(std::string_view status, std::string_view body)
{
    return "HTTP/1.1 " + std::string(status) +
           "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
           "Connection: close\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

// The form; `note` says what just happened (empty: nothing yet).
std::string page(bool has_key, std::string_view note, bool good)
{
    std::string html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ORION key</title><style>"
        "body{font-family:system-ui,sans-serif;background:#0b1020;color:#e8ecf6;margin:0;"
        "display:flex;min-height:100vh;align-items:center;justify-content:center}"
        "main{width:min(30rem,calc(100% - 2rem))}h1{font-size:1.4rem;margin:0 0 .4rem}"
        "p{color:#aab3c8;line-height:1.45}input{width:100%;box-sizing:border-box;padding:.8rem;"
        "border-radius:.6rem;border:1px solid #2c3550;background:#141b31;color:#fff;"
        "font-size:1rem}button{margin-top:.8rem;padding:.75rem 1.4rem;border:0;"
        "border-radius:.6rem;background:#5b7cfa;color:#fff;font-size:1rem}"
        ".note{padding:.7rem .9rem;border-radius:.6rem;margin:.8rem 0}"
        ".good{background:#14351f;color:#a6f0bd}.bad{background:#3a1820;color:#f6b0bd}"
        "</style></head><body><main><h1>ORION Media Center</h1>"
        "<p>Your SteamGridDB key (free at steamgriddb.com: your profile, Preferences, API). "
        "ORION uses it for covers no other source has, and for Find other art.</p>";
    if (!note.empty())
        html += std::string("<div class=\"note ") + (good ? "good" : "bad") + "\">" +
                std::string(note) + "</div>";
    html += std::string("<p>Now: ") + (has_key ? "a key is set." : "no key yet.") + "</p>";
    html += "<form method=\"post\" action=\"/key\"><input name=\"steamgriddb\" "
            "autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\" "
            "placeholder=\"Paste the key here; empty removes it\"><button>Save</button>"
            "</form></main></body></html>";
    return html;
}

int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

} // namespace

bool form_value(std::string_view body, std::string_view name, std::string *value)
{
    std::size_t start = 0;
    while (start <= body.size())
    {
        std::size_t end = body.find('&', start);
        if (end == std::string_view::npos)
            end = body.size();
        const std::string_view pair = body.substr(start, end - start);
        start = end + 1;
        const std::size_t equals = pair.find('=');
        if (pair.substr(0, equals) != name)
            continue;
        value->clear();
        const std::string_view raw =
            equals == std::string_view::npos ? std::string_view() : pair.substr(equals + 1);
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == '+')
                *value += ' ';
            else if (raw[i] == '%' && i + 2 < raw.size() && hex_digit(raw[i + 1]) >= 0 &&
                     hex_digit(raw[i + 2]) >= 0)
            {
                *value += static_cast<char>(hex_digit(raw[i + 1]) * 16 + hex_digit(raw[i + 2]));
                i += 2;
            }
            else
                *value += raw[i];
        }
        return true;
    }
    return false;
}

std::string answer_key_request(std::string_view request, const std::string &state_dir, bool *saved)
{
    *saved = false;
    const bool has_key = !read_service_key(state_dir, kSteamGridDb).empty();
    const std::size_t line_end = request.find("\r\n");
    const std::string_view line = request.substr(0, line_end);
    if (line.rfind("GET ", 0) == 0)
        return line.rfind("GET / ", 0) == 0 || line.rfind("GET /key ", 0) == 0
                   ? response("200 OK", page(has_key, "", true))
                   : response("404 Not Found", "<p>Not here: try the address alone.</p>");
    if (line.rfind("POST /key ", 0) != 0)
        return response("405 Method Not Allowed", "<p>Only the form is taken.</p>");
    const std::size_t head_end = request.find("\r\n\r\n");
    std::string key;
    if (head_end == std::string_view::npos ||
        !form_value(request.substr(head_end + 4), "steamgriddb", &key))
        return response("400 Bad Request", page(has_key, "Nothing was sent.", false));
    const std::string failed = write_service_key(state_dir, kSteamGridDb, key);
    if (!failed.empty())
        return response("200 OK", page(has_key, "Could not save the key: " + failed, false));
    *saved = true;
    const bool now = !read_service_key(state_dir, kSteamGridDb).empty();
    return response(
        "200 OK",
        page(now, now ? "Saved. ORION uses it from now on." : "The key is removed.", true));
}

std::string local_address()
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return {};
    sockaddr_in outward = {};
    outward.sin_family = AF_INET;
    outward.sin_port = htons(53);
    outward.sin_addr.s_addr = htonl(0x08080808u); // only routed: nothing is sent
    sockaddr_in self = {};
    socklen_t length = sizeof(self);
    std::string text;
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&outward), sizeof(outward)) == 0 &&
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&self), &length) == 0 &&
        self.sin_addr.s_addr != 0)
    {
        char buffer[INET_ADDRSTRLEN] = "";
        if (::inet_ntop(AF_INET, &self.sin_addr, buffer, sizeof(buffer)) != nullptr)
            text = buffer;
    }
    ::close(fd);
    return text;
}

KeyPage::~KeyPage()
{
    stop();
}

bool KeyPage::start(std::string state_dir, int port)
{
    if (running_.load())
        return true;
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return false;
    const int on = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 4) != 0)
    {
        ::close(listener);
        return false;
    }
    stopping_ = false;
    running_ = true;
    worker_ = std::thread([this, listener, state_dir = std::move(state_dir)]()
                          { serve(listener, state_dir); });
    return true;
}

void KeyPage::stop()
{
    stopping_ = true;
    if (worker_.joinable())
        worker_.join();
    running_ = false;
}

void KeyPage::serve(int listener, std::string state_dir)
{
    while (!stopping_.load())
    {
        // A short wait, so stop() is never kept waiting.
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(listener, &ready);
        timeval wait = {0, 250 * 1000};
        if (::select(listener + 1, &ready, nullptr, nullptr, &wait) <= 0)
            continue;
        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0)
            continue;
        timeval limit = {5, 0};
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
        // The request: its headers, then as much body as they announce.
        std::string request;
        char buffer[2048];
        std::size_t wanted = 0;
        while (request.size() < kMaxRequest)
        {
            const ssize_t got = ::recv(client, buffer, sizeof(buffer), 0);
            if (got <= 0)
                break;
            request.append(buffer, static_cast<std::size_t>(got));
            const std::size_t head_end = request.find("\r\n\r\n");
            if (head_end == std::string::npos)
                continue;
            if (wanted == 0)
            {
                std::string head = request.substr(0, head_end);
                for (char &c : head)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                const std::size_t at = head.find("content-length:");
                const std::size_t body =
                    at == std::string::npos ? 0 : std::strtoul(head.c_str() + at + 15, nullptr, 10);
                wanted = head_end + 4 + std::min(body, kMaxRequest);
            }
            if (request.size() >= wanted)
                break;
        }
        bool saved = false;
        const std::string answer = answer_key_request(request, state_dir, &saved);
        std::size_t sent = 0;
        while (sent < answer.size())
        {
            const ssize_t put = ::send(client, answer.data() + sent, answer.size() - sent,
#ifdef MSG_NOSIGNAL
                                       MSG_NOSIGNAL
#else
                                       0
#endif
            );
            if (put <= 0)
                break;
            sent += static_cast<std::size_t>(put);
        }
        ::close(client);
        if (saved)
            saved_ = true;
    }
    ::close(listener);
}

} // namespace hui::orion
