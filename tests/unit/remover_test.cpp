// ps5-homebrew-ui - Tests: removing games through ShadowMountPlus, and the local HTTP client.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/local_http.hpp"
#include "orion/remover.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace
{

using hui::orion::HttpReply;
using hui::orion::Remover;

TEST(LocalHttp, ReadsPlainAndChunkedReplies)
{
    HttpReply reply;
    ASSERT_TRUE(hui::orion::parse_http_reply(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 13\r\n\r\n"
        "{\"status\": 0}trailing",
        &reply));
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.body, "{\"status\": 0}");
    ASSERT_TRUE(hui::orion::parse_http_reply(
        "HTTP/1.1 400 Bad Request\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\n{\"sta\r\n8\r\ntus\": 1}\r\n0\r\n\r\n",
        &reply));
    EXPECT_EQ(reply.status, 400);
    EXPECT_EQ(reply.body, "{\"status\": 1}");
    EXPECT_FALSE(hui::orion::parse_http_reply("SSH-2.0-nope\r\n\r\n", &reply));
    EXPECT_FALSE(hui::orion::parse_http_reply("HTTP/1.1 200 OK\r\n", &reply));
}

TEST(LocalHttp, SaysSoWhenNothingAnswers)
{
    HttpReply reply;
    std::string error;
    // Port 1 on the loopback: nothing listens there.
    EXPECT_FALSE(hui::orion::post_local_json(1, "/api/v1/version", "{}", &reply, &error));
    EXPECT_FALSE(error.empty());
}

// A pretend ShadowMountPlus: answers in order, remembers what it was asked.
struct FakeApi
{
    std::vector<std::pair<std::string, std::string>> asked;
    std::vector<std::string> answers;
    std::size_t next = 0;

    Remover::Post post()
    {
        return [this](const std::string &path, const std::string &json, std::string *reply,
                      std::string *)
        {
            asked.emplace_back(path, json);
            *reply = next < answers.size() ? answers[next++] : std::string("{\"status\": 0}");
            return true;
        };
    }
};

TEST(Remover, TakesAGameOffTheHomeScreen)
{
    FakeApi api;
    Remover remover(api.post());
    ASSERT_TRUE(remover.start(Remover::Kind::home_screen, "PPSA01234", "Some Game"));
    remover.wait();
    EXPECT_TRUE(remover.take_finished());
    EXPECT_FALSE(remover.take_finished()); // once
    const Remover::Progress done = remover.progress();
    EXPECT_TRUE(done.ok) << done.error;
    ASSERT_EQ(api.asked.size(), 1u);
    EXPECT_EQ(api.asked[0].first, "/api/v1/games/uninstall");
    EXPECT_EQ(api.asked[0].second, "{\"title_id\": \"PPSA01234\"}");
}

TEST(Remover, DeletesWatchesTheJobThenUninstalls)
{
    FakeApi api;
    api.answers = {
        "{\"status\": 0, \"job_id\": 7, \"active\": true}",
        "{\"status\": 0, \"active\": true, \"processed_bytes\": 10, \"total_bytes\": 40}",
        "{\"status\": 0, \"active\": false, \"processed_bytes\": 40, \"total_bytes\": 40, "
        "\"result_status\": 0}",
        "{\"status\": 0}",
    };
    Remover remover(api.post());
    remover.poll_ms = 0;
    ASSERT_TRUE(remover.start(Remover::Kind::erase, "PPSA01234", "Some Game"));
    remover.wait();
    const Remover::Progress done = remover.progress();
    EXPECT_TRUE(done.ok) << done.error;
    EXPECT_EQ(done.done_bytes, 40u);
    EXPECT_EQ(done.total_bytes, 40u);
    ASSERT_EQ(api.asked.size(), 4u);
    EXPECT_EQ(api.asked[0].first, "/api/v1/games/delete");
    EXPECT_EQ(api.asked[0].second, "{\"title_id\": \"PPSA01234\", \"confirm\": true}");
    EXPECT_EQ(api.asked[1].first, "/api/v1/games/storage/status");
    EXPECT_EQ(api.asked[3].first, "/api/v1/games/uninstall");
}

TEST(Remover, SaysWhyWhenShadowMountPlusRefuses)
{
    FakeApi api;
    api.answers = {"{\"status\": 1, \"error\": \"installed PKG sources cannot be deleted\"}"};
    Remover remover(api.post());
    remover.poll_ms = 0;
    ASSERT_TRUE(remover.start(Remover::Kind::erase, "PPSA01234", "Some Game"));
    remover.wait();
    const Remover::Progress done = remover.progress();
    EXPECT_FALSE(done.ok);
    EXPECT_EQ(done.error, "installed PKG sources cannot be deleted");
    EXPECT_EQ(api.asked.size(), 1u); // nothing after a refusal

    // A job that ends badly is reported, and the game stays on the home screen.
    FakeApi failing;
    failing.answers = {"{\"status\": 0, \"active\": true}",
                       "{\"status\": 0, \"active\": false, \"result_status\": 5, "
                       "\"result_error\": \"read-only drive\"}"};
    Remover again(failing.post());
    again.poll_ms = 0;
    ASSERT_TRUE(again.start(Remover::Kind::erase, "PPSA01234", "Some Game"));
    again.wait();
    EXPECT_FALSE(again.progress().ok);
    EXPECT_EQ(again.progress().error, "read-only drive");
    EXPECT_EQ(failing.asked.size(), 2u);
}

TEST(Remover, AGameAlreadyGoneOnlyLeavesTheHomeScreen)
{
    FakeApi api;
    api.answers = {"{\"status\": 0, \"source_missing\": true}", "{\"status\": 0}"};
    Remover remover(api.post());
    remover.poll_ms = 0;
    ASSERT_TRUE(remover.start(Remover::Kind::erase, "PPSA01234", "Some Game"));
    remover.wait();
    EXPECT_TRUE(remover.progress().ok);
    ASSERT_EQ(api.asked.size(), 2u);
    EXPECT_EQ(api.asked[1].first, "/api/v1/games/uninstall");
}

} // namespace
