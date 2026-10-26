// ORION Media Center - Tests: the key page, without a socket.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/key_page.hpp"

#include "core/save_file.hpp"
#include "orion/steamgriddb.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cstdio>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace hui
{
namespace
{

TEST(KeyPage, ReadsFormFields)
{
    std::string value;
    ASSERT_TRUE(orion::form_value("a=1&steamgriddb=ab%2Bc+d%3D&b=2", "steamgriddb", &value));
    EXPECT_EQ(value, "ab+c d=");
    ASSERT_TRUE(orion::form_value("steamgriddb=", "steamgriddb", &value));
    EXPECT_EQ(value, "");
    EXPECT_FALSE(orion::form_value("other=1", "steamgriddb", &value));

    EXPECT_FALSE(orion::request_complete("GET / HTTP/1.1\r\nHost: x\r\n"));
    EXPECT_TRUE(orion::request_complete("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_FALSE(orion::request_complete("POST /key HTTP/1.1\r\nContent-Length: 5\r\n\r\nab"));
    EXPECT_TRUE(orion::request_complete("POST /key HTTP/1.1\r\ncontent-length: 2\r\n\r\nab"));
    ASSERT_TRUE(orion::form_value("steamgriddb=%zz%4", "steamgriddb", &value));
    EXPECT_EQ(value, "%zz%4"); // a broken escape is kept as it is
}

// The form, a key saved through it (never shown back), and anything else
// turned away.
TEST(KeyPage, SavesTheKeyFromTheForm)
{
    const std::string dir = ::testing::TempDir() + "orion-key-page";
    ASSERT_TRUE(save::ensure_directory(dir));
    std::remove((dir + "/keys.txt").c_str());
    bool saved = true;

    // A browser's spare connection, closed with nothing sent: no answer.
    EXPECT_TRUE(orion::answer_key_request("", dir, &saved).empty());

    std::string page = orion::answer_key_request("GET / HTTP/1.1\r\nHost: x\r\n\r\n", dir, &saved);
    EXPECT_FALSE(saved);
    EXPECT_EQ(page.rfind("HTTP/1.1 200 OK", 0), 0u);
    EXPECT_NE(page.find("no key yet"), std::string::npos);
    EXPECT_NE(page.find("<form method=\"post\" action=\"/key\">"), std::string::npos);

    const std::string post = "POST /key HTTP/1.1\r\nContent-Type: "
                             "application/x-www-form-urlencoded\r\nContent-Length: 28\r\n\r\n"
                             "steamgriddb=0123456789abcdef";
    page = orion::answer_key_request(post, dir, &saved);
    EXPECT_TRUE(saved);
    EXPECT_EQ(orion::read_service_key(dir, orion::kSteamGridDb), "0123456789abcdef");
    EXPECT_NE(page.find("Saved"), std::string::npos);
    EXPECT_EQ(page.find("0123456789abcdef"), std::string::npos); // the key is not echoed
    EXPECT_NE(orion::answer_key_request("GET / HTTP/1.1\r\n\r\n", dir, &saved).find("a key is set"),
              std::string::npos);

    // An empty key removes it.
    orion::answer_key_request("POST /key HTTP/1.1\r\n\r\nsteamgriddb=", dir, &saved);
    EXPECT_TRUE(saved);
    EXPECT_TRUE(orion::read_service_key(dir, orion::kSteamGridDb).empty());

    EXPECT_EQ(orion::answer_key_request("GET /secret HTTP/1.1\r\n\r\n", dir, &saved)
                  .rfind("HTTP/1.1 404", 0),
              0u);
    EXPECT_EQ(orion::answer_key_request("DELETE /key HTTP/1.1\r\n\r\n", dir, &saved)
                  .rfind("HTTP/1.1 405", 0),
              0u);
    EXPECT_EQ(orion::answer_key_request("POST /key HTTP/1.1\r\n\r\nother=1", dir, &saved)
                  .rfind("HTTP/1.1 400", 0),
              0u);
    EXPECT_FALSE(saved);
}

int connect_to(int port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    timeval limit = {3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

std::string read_all(int fd)
{
    std::string text;
    char buffer[4096];
    for (;;)
    {
        const ssize_t got = ::recv(fd, buffer, sizeof(buffer), 0);
        if (got <= 0)
            break;
        text.append(buffer, static_cast<std::size_t>(got));
    }
    return text;
}

// As a browser does it: a spare connection that stays quiet, then the page
// on another. The page comes at once, and the quiet one is never answered
// (a browser takes an answer there for its next request: "Only the form is
// taken" on the first visit).
TEST(KeyPage, ServesThePagePastAQuietConnection)
{
    const std::string dir = ::testing::TempDir() + "orion-key-page-server";
    ASSERT_TRUE(save::ensure_directory(dir));
    orion::KeyPage page;
    int port = 0;
    for (int candidate = 39062; candidate < 39100 && port == 0; ++candidate)
    {
        if (page.start(dir, candidate))
            port = candidate;
    }
    ASSERT_NE(port, 0);
    const int quiet = connect_to(port);
    ASSERT_GE(quiet, 0);
    const int asking = connect_to(port);
    ASSERT_GE(asking, 0);
    const std::string get = "GET / HTTP/1.1\r\nHost: console\r\n\r\n";
    ASSERT_EQ(::send(asking, get.data(), get.size(), 0), static_cast<ssize_t>(get.size()));
    const std::string answer = read_all(asking);
    EXPECT_EQ(answer.rfind("HTTP/1.1 200 OK", 0), 0u);
    ::close(asking);
    // The quiet one: closed when the page stops, with nothing said.
    page.stop();
    EXPECT_TRUE(read_all(quiet).empty());
    ::close(quiet);
}

} // namespace
} // namespace hui
