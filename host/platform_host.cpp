// ORION Media Center - Host (Linux/WSL) implementation of the system services.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/system.hpp"

#include "platform/ps5/game_copy.hpp"
#include "platform/ps5/smb.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

namespace hui::sys
{

std::int64_t monotonic_us()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}

void log(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}

bool hide_splash_screen()
{
    return true;
}

void sleep_us(std::uint32_t microseconds)
{
    usleep(microseconds);
}

void park()
{
    std::exit(1);
}

void quit()
{
    std::exit(0);
}

int launch_app(const char *title_id)
{
    std::fprintf(stderr, "[HUI] launch %s: not on a console\n", title_id);
    return kLaunchUnsupported;
}

bool on_console()
{
    return false;
}

std::string system_language()
{
    const char *language = std::getenv("HUI_LANG");
    return language != nullptr && *language != '\0' ? language : "en";
}

void art_trace(const char *step, bool reset)
{
    (void)step;
    (void)reset;
}

bool http_get(const std::string &url, std::size_t limit, std::string *body, std::string *error)
{
    (void)url;
    (void)limit;
    body->clear();
    *error = "the PC preview has no network";
    return false;
}

bool http_get(const std::string &url, const std::vector<std::string> &headers, std::size_t limit,
              std::string *body, std::string *error)
{
    (void)headers;
    return http_get(url, limit, body, error);
}

bool http_download(const std::string &url, const std::vector<std::string> &headers,
                   const std::string &path, std::size_t limit, std::string *error)
{
    (void)headers;
    return http_download(url, path, limit, error);
}

bool http_post(const std::string &url, const std::vector<std::string> &headers,
               const std::string &body, std::size_t limit, std::string *answer, std::string *error)
{
    (void)url;
    (void)headers;
    (void)body;
    (void)limit;
    answer->clear();
    *error = "the PC preview has no network";
    return false;
}

bool http_download(const std::string &url, const std::string &path, std::size_t limit,
                   std::string *error)
{
    (void)url;
    (void)path;
    (void)limit;
    *error = "the PC preview has no network";
    return false;
}

bool smb_list(const SmbLogin &login, const std::string &folder, int depth, std::size_t most,
              std::vector<SmbEntry> *entries, std::string *error)
{
    (void)login;
    (void)folder;
    (void)depth;
    (void)most;
    entries->clear();
    *error = "the PC preview has no network";
    return false;
}

bool smb_download(const SmbLogin &login, const std::string &path, const std::string &local,
                  std::atomic<std::uint64_t> *done, const std::atomic<bool> *cancel,
                  std::string *error)
{
    (void)login;
    (void)path;
    (void)local;
    (void)cancel;
    if (done != nullptr)
        *done = 0;
    *error = "the PC preview has no network";
    return false;
}

struct SmbFile::Open
{
};

SmbFile::SmbFile() = default;

SmbFile::~SmbFile() = default;

bool SmbFile::open(const SmbLogin &login, const std::string &path, std::string *error)
{
    (void)login;
    (void)path;
    *error = "the PC preview has no network";
    return false;
}

long long SmbFile::read(std::uint64_t offset, void *buffer, std::size_t count)
{
    (void)offset;
    (void)buffer;
    (void)count;
    return -1;
}

void SmbFile::close()
{
    open_ = nullptr;
    size_ = 0;
}

std::vector<SmbServer> smb_find_servers(const std::string &own, std::string *trace,
                                        const std::function<void(const std::string &)> &step)
{
    (void)own;
    (void)step;
    *trace = "the PC preview has no network";
    return {};
}

bool smb_shares(const SmbLogin &login, std::vector<std::string> *shares, std::string *error)
{
    (void)login;
    shares->clear();
    *error = "the PC preview has no network";
    return false;
}

bool copy_game(const GameSource &source, const std::string &into, CopyProgress *progress,
               std::string *error)
{
    (void)source;
    (void)into;
    (void)progress;
    *error = "the PC preview has no network";
    return false;
}

void notify(const std::string &text)
{
    std::fprintf(stderr, "[HUI] notification: %s\n", text.c_str());
}

int send_payload(const std::string &elf, int port)
{
    std::fprintf(stderr, "[HUI] payload of %zu bytes for port %d: not on a console\n", elf.size(),
                 port);
    return kLaunchUnsupported;
}

} // namespace hui::sys
