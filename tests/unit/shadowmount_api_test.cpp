// ORION Media Center - Tests: reading ShadowMountPlus's answers.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/shadowmount_api.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace hui
{
namespace
{

TEST(ShadowMountApi, ReadsAnHttpAnswer)
{
    int status = 0;
    std::string body;
    ASSERT_TRUE(orion::parse_http_answer(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\": 0}", &status,
        &body));
    EXPECT_EQ(status, 200);
    EXPECT_EQ(body, "{\"status\": 0}");

    // Chunked, put back together.
    ASSERT_TRUE(orion::parse_http_answer("HTTP/1.1 404 Not Found\r\nTransfer-Encoding: Chunked"
                                         "\r\n\r\n4\r\n{\"st\r\n6\r\natus\"}\r\n0\r\n\r\n",
                                         &status, &body));
    EXPECT_EQ(status, 404);
    EXPECT_EQ(body, "{\"status\"}");

    EXPECT_FALSE(orion::parse_http_answer("not http", &status, &body));
    EXPECT_FALSE(orion::parse_http_answer("HTTP/1.1 200 OK\r\nno end of headers", &status, &body));
}

TEST(ShadowMountApi, ReadsItsGames)
{
    std::vector<library::Title> titles;
    ASSERT_TRUE(orion::parse_shadowmount_games(R"({"status": 0, "count": 5, "games": [
        {"title_id": "PPSA90180", "title_name": "Quiet Bay", "platform": "ps5", "version": "01.000.002",
         "installed": true, "path": "/mnt/usb0/Quiet Bay.exfat", "source_type": "image"},
        {"title_id": "CUSA90181", "title_name": "Paper Kites", "platform": "ps4",
         "installed": true, "path": "/mnt/ext0/CUSA90181/app.pkg", "source_type": "pkg"},
        {"title_id": "PPSA90182", "title_name": "Not Yet", "platform": "ps5", "installed": false},
        {"title_id": "PPSA90183", "title_name": "", "platform": "ps5", "installed": true},
        {"title_id": "nonsense", "title_name": "Odd", "platform": "ps5", "installed": true}]})",
                                               &titles));
    ASSERT_EQ(titles.size(), 2u); // not installed, nameless, and no title ID: left out
    EXPECT_EQ(titles[0].id, "PPSA90180");
    EXPECT_EQ(titles[0].name, "Quiet Bay");
    EXPECT_EQ(titles[0].platform, library::Platform::ps5);
    EXPECT_EQ(titles[0].version, "01.000.002");
    EXPECT_EQ(titles[0].source, library::Source::installed);
    EXPECT_EQ(titles[1].platform, library::Platform::ps4);

    EXPECT_FALSE(orion::parse_shadowmount_games(R"({"status": 22, "error": "bad"})", &titles));
    EXPECT_FALSE(orion::parse_shadowmount_games("not json", &titles));
}

} // namespace
} // namespace hui
