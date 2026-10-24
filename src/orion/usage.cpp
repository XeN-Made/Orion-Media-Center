// ORION Media Center - Anonymous usage: check-ins that count the consoles using ORION.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/usage.hpp"

#include "core/save_file.hpp"
#include "library/json.hpp"

#include <cstdio>
#include <random>
#include <thread>

namespace hui::orion
{

namespace
{

bool is_id(const std::string &id)
{
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}

// 16 bytes from the system's generator; random_device where /dev/urandom
// cannot be opened (a sandbox without it).
std::string random_id()
{
    unsigned char bytes[16] = {};
    bool read = false;
    if (std::FILE *in = std::fopen("/dev/urandom", "rb"))
    {
        read = std::fread(bytes, 1, sizeof(bytes), in) == sizeof(bytes);
        std::fclose(in);
    }
    if (!read)
    {
        std::random_device device;
        for (unsigned char &byte : bytes)
            byte = static_cast<unsigned char>(device() & 0xffu);
    }
    std::string id;
    for (const unsigned char byte : bytes)
    {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned>(byte));
        id += hex;
    }
    return id;
}

} // namespace

std::string usage_id(const std::string &state_dir)
{
    if (state_dir.empty())
        return "";
    const std::string file = state_dir + "/usage-id.txt";
    std::string id;
    if (save::read_file(file, &id, 256))
    {
        id = id.substr(0, id.find_first_of("\r\n"));
        if (is_id(id))
            return id;
    }
    id = random_id();
    return save::write_atomic(file, id + "\n").empty() ? id : std::string();
}

std::string usage_body(const UsageReport &report)
{
    return "{\"id\": " + json::quote(report.id) + ", \"version\": " + json::quote(report.version) +
           "}";
}

std::string usage_time(std::time_t now)
{
    std::tm when = {};
    if (gmtime_r(&now, &when) == nullptr)
        return "";
    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d UTC", when.tm_year + 1900,
                  when.tm_mon + 1, when.tm_mday, when.tm_hour, when.tm_min);
    return text;
}

bool UsageSender::send(const HttpPost &post, const std::string &url, const std::string &state_dir,
                       const UsageReport &report, std::time_t now)
{
    if (!post || url.empty() || state_dir.empty() || !is_id(report.id) || report.version.empty() ||
        busy_->exchange(true))
        return false;
    // Copies only: the thread may outlive the sender.
    std::thread(
        [post, url, state_dir, busy = busy_, body = usage_body(report), when = usage_time(now)]()
        {
            std::string error;
            if (post(url, body, &error))
                save::write_atomic(state_dir + "/usage.txt",
                                   "sent " + when + " to " + url + "\n" + body + "\n");
            busy->store(false);
        })
        .detach();
    return true;
}

} // namespace hui::orion
