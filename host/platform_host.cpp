// ORION Media Center - Host (Linux/WSL) implementation of the system services.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/system.hpp"

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

bool http_download(const std::string &url, const std::string &path, std::size_t limit,
                   std::string *error)
{
    (void)url;
    (void)path;
    (void)limit;
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
