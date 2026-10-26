// ORION Media Center - The key page: ORION's SteamGridDB key typed on a phone or PC.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/key_page.hpp"

#include "orion/romm.hpp"
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
#include <time.h>
#include <unistd.h>
#include <vector>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxRequest = 16u << 10;
constexpr std::size_t kMaxClients = 8;
constexpr std::int64_t kQuietMs = 30000; // a connection that long without a request is dropped

std::string response(std::string_view status, std::string_view body)
{
    return "HTTP/1.1 " + std::string(status) +
           "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
           "Connection: close\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

// The form; `note` says what just happened (empty: nothing yet).
// What the page shows of ORION's settings: never a key itself.
struct Shown
{
    bool grid_key = false;
    std::string romm; // the RomM address, or empty
    bool paired = false;
};

Shown shown(const std::string &state_dir)
{
    Shown out;
    out.grid_key = !read_service_key(state_dir, kSteamGridDb).empty();
    out.romm = romm_base(read_service_key(state_dir, kRommUrl));
    out.paired = !read_service_key(state_dir, kRommToken).empty();
    return out;
}

std::string escaped(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '&')
            out += "&amp;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}

std::string page(const Shown &now, std::string_view note, bool good)
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
    html += std::string("<p>Now: ") + (now.grid_key ? "a key is set." : "no key yet.") + "</p>";
    html += "<form method=\"post\" action=\"/key\"><input name=\"steamgriddb\" "
            "autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\" "
            "placeholder=\"Paste the key here; empty removes it\"><button>Save</button>"
            "</form>";
    html +=
        "<h1 style=\"margin-top:2rem\">RomM</h1><p>Your RomM server, and the code RomM "
        "shows to pair a device (your profile, Client tokens, a new token, Pair). Its games "
        "then show on their systems' tabs.</p><p>Now: " +
        (now.romm.empty() ? std::string("no server.")
                          : escaped(now.romm) + (now.paired ? ", paired." : ", not paired yet.")) +
        "</p><form method=\"post\" action=\"/romm\"><input name=\"romm_url\" "
        "autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\" "
        "placeholder=\"Address, like 192.168.4.115 (empty keeps it)\">"
        "<input name=\"romm_code\" style=\"margin-top:.6rem\" autocomplete=\"off\" "
        "autocapitalize=\"characters\" spellcheck=\"false\" placeholder=\"Pairing code\">"
        "<button>Save and pair</button></form></main></body></html>";
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

bool request_complete(std::string_view request)
{
    const std::size_t head_end = request.find("\r\n\r\n");
    if (head_end == std::string_view::npos)
        return false;
    std::string head(request.substr(0, head_end));
    for (char &c : head)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::size_t at = head.find("content-length:");
    const std::size_t body =
        at == std::string::npos ? 0 : std::strtoul(head.c_str() + at + 15, nullptr, 10);
    return request.size() >= head_end + 4 + std::min(body, kMaxRequest);
}

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
    // Nothing asked (a connection the browser opened and closed): nothing said.
    if (request.empty())
        return {};
    const std::size_t line_end = request.find("\r\n");
    const std::string_view line = request.substr(0, line_end);
    if (line.rfind("GET ", 0) == 0)
        return line.rfind("GET / ", 0) == 0 || line.rfind("GET /key ", 0) == 0 ||
                       line.rfind("GET /romm ", 0) == 0
                   ? response("200 OK", page(shown(state_dir), "", true))
                   : response("404 Not Found", "<p>Not here: try the address alone.</p>");
    const bool grid = line.rfind("POST /key ", 0) == 0;
    const bool romm = line.rfind("POST /romm ", 0) == 0;
    if (!grid && !romm)
        return response("405 Method Not Allowed", "<p>Only the form is taken.</p>");
    const std::size_t head_end = request.find("\r\n\r\n");
    const std::string_view body =
        head_end == std::string_view::npos ? std::string_view() : request.substr(head_end + 4);
    if (romm)
    {
        std::string address;
        std::string code;
        const bool has_address = form_value(body, "romm_url", &address) && !address.empty();
        const bool has_code = form_value(body, "romm_code", &code) && !code.empty();
        if (!has_address && !has_code)
            return response("400 Bad Request", page(shown(state_dir), "Nothing was sent.", false));
        if (has_address)
        {
            const std::string base = romm_base(address);
            if (base.empty())
                return response("200 OK",
                                page(shown(state_dir),
                                     "That is not an address RomM can be reached at.", false));
            // Another server: the old one's token means nothing there.
            if (base != romm_base(read_service_key(state_dir, kRommUrl)))
                write_service_key(state_dir, kRommToken, "");
            const std::string failed = write_service_key(state_dir, kRommUrl, base);
            if (!failed.empty())
                return response("200 OK",
                                page(shown(state_dir), "Could not save it: " + failed, false));
        }
        // The code is exchanged by ORION itself (it has the network calls):
        // it waits here for it, once.
        if (has_code)
            write_service_key(state_dir, kRommPairCode, code);
        *saved = true;
        return response(
            "200 OK",
            page(shown(state_dir),
                 has_code ? "Saved. ORION is pairing now: watch the console." : "Saved.", true));
    }
    std::string key;
    if (!form_value(body, "steamgriddb", &key))
        return response("400 Bad Request", page(shown(state_dir), "Nothing was sent.", false));
    const std::string failed = write_service_key(state_dir, kSteamGridDb, key);
    if (!failed.empty())
        return response("200 OK",
                        page(shown(state_dir), "Could not save the key: " + failed, false));
    *saved = true;
    const Shown now = shown(state_dir);
    return response("200 OK",
                    page(now,
                         now.grid_key ? "Saved. ORION uses it from now on." : "The key is removed.",
                         true));
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
    // Browsers open a spare connection or two and send nothing on them for
    // a while (to have one ready). Read from every open connection at once,
    // so a quiet one never holds up the request that matters, and close a
    // quiet one without a word: an answer on it would be read as the answer
    // to whatever the browser sends there later.
    struct Client
    {
        int fd = -1;
        std::string request;
        std::int64_t opened = 0; // monotonic ms
    };
    std::vector<Client> clients;
    const auto now_ms = []()
    {
        timespec now = {};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
    };
    const auto finish = [&](Client &client)
    {
        bool saved = false;
        const std::string answer = answer_key_request(client.request, state_dir, &saved);
        std::size_t sent = 0;
        while (sent < answer.size())
        {
            const ssize_t put = ::send(client.fd, answer.data() + sent, answer.size() - sent,
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
        ::close(client.fd);
        client.fd = -1;
        if (saved)
            saved_ = true;
    };
    while (!stopping_.load())
    {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(listener, &ready);
        int top = listener;
        for (const Client &client : clients)
        {
            FD_SET(client.fd, &ready);
            top = std::max(top, client.fd);
        }
        // A short wait, so stop() is never kept waiting.
        timeval wait = {0, 250 * 1000};
        const int count = ::select(top + 1, &ready, nullptr, nullptr, &wait);
        if (count > 0 && FD_ISSET(listener, &ready))
        {
            const int fd = ::accept(listener, nullptr, nullptr);
            if (fd >= 0 && clients.size() >= kMaxClients)
                ::close(fd);
            else if (fd >= 0)
            {
                timeval limit = {5, 0};
                ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
                clients.push_back({fd, std::string(), now_ms()});
            }
        }
        for (Client &client : clients)
        {
            if (client.fd < 0)
                continue;
            if (count > 0 && FD_ISSET(client.fd, &ready))
            {
                char buffer[2048];
                const ssize_t got = ::recv(client.fd, buffer, sizeof(buffer), 0);
                if (got <= 0)
                {
                    // Closed by the browser: an unfinished request is not
                    // answered either.
                    ::close(client.fd);
                    client.fd = -1;
                    continue;
                }
                client.request.append(buffer, static_cast<std::size_t>(got));
                if (request_complete(client.request) || client.request.size() >= kMaxRequest)
                {
                    finish(client);
                    continue;
                }
            }
            if (now_ms() - client.opened > kQuietMs)
            {
                if (client.request.empty())
                {
                    ::close(client.fd);
                    client.fd = -1;
                }
                else
                    finish(client); // half a request: told so
            }
        }
        std::erase_if(clients, [](const Client &client) { return client.fd < 0; });
    }
    for (const Client &client : clients)
        ::close(client.fd);
    ::close(listener);
}

} // namespace hui::orion
