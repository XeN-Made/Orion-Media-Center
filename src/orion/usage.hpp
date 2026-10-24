// ORION Media Center - Anonymous usage: check-ins that count the consoles using ORION.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <ctime>
#include <functional>
#include <memory>
#include <string>

namespace hui::orion
{

// Where check-ins go: the project's counter (stats/worker, a Cloudflare
// Worker that keeps the ID, the version and when it was last seen, nothing
// else). Empty sends nothing.
inline constexpr const char *kUsageUrl = "";
// While ORION is open it checks in this often, so the counter can tell how
// many consoles have it open now (seen in the last five minutes).
inline constexpr float kUsageEvery = 300.0f; // seconds

// Everything a check-in holds. The ID is random, made once on this console
// (usage_id); it says nothing about the console, its owner or its games.
struct UsageReport
{
    std::string id;      // 32 lowercase hex characters
    std::string version; // ORION's ("01.104.000")
};

// Sends body (JSON) to url; false with *error set when it could not.
using HttpPost =
    std::function<bool(const std::string &url, const std::string &body, std::string *error)>;

// The console's random ID, from <state_dir>/usage-id.txt, made there the
// first time (16 random bytes). Empty when it can be neither read nor made.
std::string usage_id(const std::string &state_dir);
// The check-in as sent: {"id": "...", "version": "..."}.
std::string usage_body(const UsageReport &report);
// A moment as usage.txt writes it: "2026-10-24 12:00 UTC".
std::string usage_time(std::time_t now);

// Sends check-ins on a thread of their own, one at a time: a request still
// on its way when the next is due makes the next wait for its turn. On
// success <state_dir>/usage.txt says when, where to and the exact text sent,
// for the player to read. A thread is left to finish by itself (one that
// hangs must not hold ORION up), so the sender can go before it does.
//
//   UsageSender usage;
//   ...every kUsageEvery seconds while sharing is on:
//   usage.send(sys::http_post, kUsageUrl, state, {usage_id(state), version}, std::time(nullptr));
class UsageSender
{
  public:
    // False when nothing was started: no address, no valid ID or version, or
    // the last check-in still on its way.
    bool send(const HttpPost &post, const std::string &url, const std::string &state_dir,
              const UsageReport &report, std::time_t now);

  private:
    std::shared_ptr<std::atomic<bool>> busy_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace hui::orion
