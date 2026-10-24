// ORION Media Center - Tests: the anonymous check-in, what it holds and how it goes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "orion/prefs.hpp"
#include "orion/usage.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace hui
{
namespace
{

std::string fresh_dir(const char *name)
{
    const std::string dir = ::testing::TempDir() + name;
    EXPECT_TRUE(save::ensure_directory(dir));
    for (const char *file : {"/usage-id.txt", "/usage.txt", "/library.json"})
        std::remove((dir + file).c_str());
    return dir;
}

// 2026-10-24 12:00:00 UTC.
constexpr std::time_t kNoon = 1792843200;
constexpr const char *kUrl = "https://example.invalid/ping";

template <typename Done> void wait_for(Done done)
{
    for (int i = 0; i < 300 && !done(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// The ID is random, made once and kept; the check-in holds it and the
// version, nothing else.
TEST(Usage, SendsOnlyARandomIdAndTheVersion)
{
    const std::string dir = fresh_dir("orion-usage-id");
    const std::string id = orion::usage_id(dir);
    ASSERT_EQ(id.size(), 32u);
    EXPECT_EQ(id.find_first_not_of("0123456789abcdef"), std::string::npos);
    EXPECT_EQ(orion::usage_id(dir), id); // kept
    ASSERT_TRUE(save::write_atomic(dir + "/usage-id.txt", "not an id\n").empty());
    EXPECT_NE(orion::usage_id(dir), "not an id"); // a broken file is made again
    EXPECT_EQ(orion::usage_id(""), "");
    EXPECT_EQ(orion::usage_body({"0123456789abcdef0123456789abcdef", "01.104.000"}),
              R"({"id": "0123456789abcdef0123456789abcdef", "version": "01.104.000"})");
    EXPECT_EQ(orion::usage_time(kNoon), "2026-10-24 12:00 UTC");
}

// Sent on a thread of its own, one at a time; usage.txt then shows exactly
// what was sent. Nothing goes without an address or a valid ID.
TEST(Usage, SendsOneAtATimeAndWritesDownWhatItSent)
{
    const std::string dir = fresh_dir("orion-usage-send");
    const orion::UsageReport report{orion::usage_id(dir), "01.104.000"};
    std::atomic<bool> release{false};
    std::atomic<int> calls{0};
    const orion::HttpPost slow = [&](const std::string &, const std::string &, std::string *)
    {
        ++calls;
        while (!release.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return true;
    };
    orion::UsageSender sender;
    EXPECT_FALSE(sender.send(slow, "", dir, report, kNoon));
    EXPECT_FALSE(sender.send(slow, kUrl, dir, {"short", "01.104.000"}, kNoon));
    ASSERT_TRUE(sender.send(slow, kUrl, dir, report, kNoon));
    EXPECT_FALSE(sender.send(slow, kUrl, dir, report, kNoon)); // the first is still out
    release = true;
    std::string written;
    wait_for([&] { return save::read_file(dir + "/usage.txt", &written); });
    EXPECT_EQ(written, "sent 2026-10-24 12:00 UTC to https://example.invalid/ping\n" +
                           orion::usage_body(report) + "\n");
    EXPECT_EQ(calls.load(), 1);

    // A failure writes nothing, and the next one may go.
    const orion::HttpPost fails = [&](const std::string &, const std::string &, std::string *error)
    {
        ++calls;
        *error = "offline";
        return false;
    };
    std::remove((dir + "/usage.txt").c_str());
    bool sent = false;
    wait_for([&] { return (sent = sender.send(fails, kUrl, dir, report, kNoon)); });
    ASSERT_TRUE(sent);
    wait_for([&] { return calls.load() == 2; });
    wait_for([&] { return sender.send(fails, kUrl, dir, report, kNoon); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(save::read_file(dir + "/usage.txt", &written));
}

// On until the player turns it off, and kept.
TEST(Usage, IsOnUntilTurnedOff)
{
    const std::string dir = fresh_dir("orion-usage-prefs");
    EXPECT_TRUE(orion::load_prefs(dir).share_usage);
    orion::LibraryPrefs prefs;
    prefs.share_usage = false;
    ASSERT_TRUE(orion::save_prefs(dir, prefs));
    EXPECT_FALSE(orion::load_prefs(dir).share_usage);
}

} // namespace
} // namespace hui
