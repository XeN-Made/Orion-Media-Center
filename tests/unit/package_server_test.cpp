// ORION Media Center - Tests: the package server, as the console's installer would ask it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/package_server.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

using hui::orion::PackageServer;
using hui::orion::PackageSource;

constexpr int kPort = 19063;

// Byte i of the file is i % 251: any slice can be checked.
class Pattern : public PackageSource
{
  public:
    explicit Pattern(std::uint64_t size) : size_(size)
    {
    }
    long long read(std::uint64_t offset, void *buffer, std::size_t count) override
    {
        if (offset >= size_)
            return 0;
        const std::size_t n =
            static_cast<std::size_t>(std::min<std::uint64_t>(count, size_ - offset));
        auto *bytes = static_cast<unsigned char *>(buffer);
        for (std::size_t i = 0; i < n; ++i)
            bytes[i] = static_cast<unsigned char>((offset + i) % 251);
        return static_cast<long long>(n);
    }

  private:
    std::uint64_t size_;
};

// One request on a fresh connection; the whole answer back.
std::string ask(const std::string &request)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(kPort);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(::connect(fd, reinterpret_cast<const sockaddr *>(&to), sizeof(to)), 0);
    const std::string sent = request + "Connection: close\r\n\r\n";
    ::send(fd, sent.data(), sent.size(), 0);
    ::shutdown(fd, SHUT_WR);
    std::string answer;
    char chunk[65536];
    for (;;)
    {
        const ssize_t got = ::recv(fd, chunk, sizeof(chunk), 0);
        if (got <= 0)
            break;
        answer.append(chunk, static_cast<std::size_t>(got));
    }
    ::close(fd);
    return answer;
}

std::string body_of(const std::string &answer)
{
    const std::size_t end = answer.find("\r\n\r\n");
    return end == std::string::npos ? std::string() : answer.substr(end + 4);
}

TEST(PackageServer, ReadsRangesAsTheyAreWritten)
{
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    EXPECT_TRUE(hui::orion::parse_range("bytes=100-199", 1000, &first, &last));
    EXPECT_EQ(first, 100u);
    EXPECT_EQ(last, 199u);
    EXPECT_TRUE(hui::orion::parse_range("bytes=900-", 1000, &first, &last));
    EXPECT_EQ(last, 999u);
    EXPECT_TRUE(hui::orion::parse_range("bytes=-100", 1000, &first, &last));
    EXPECT_EQ(first, 900u);
    EXPECT_TRUE(hui::orion::parse_range("bytes=990-5000", 1000, &first, &last));
    EXPECT_EQ(last, 999u); // cut at the end
    EXPECT_FALSE(hui::orion::parse_range("bytes=1000-", 1000, &first, &last));
    EXPECT_FALSE(hui::orion::parse_range("bytes=0-1,5-6", 1000, &first, &last));
    EXPECT_FALSE(hui::orion::parse_range("items=0-1", 1000, &first, &last));
}

// HEAD for the size, ranges as the installer asks them, the whole file,
// and nothing but the one path.
TEST(PackageServer, ServesTheOnePackageInRanges)
{
    constexpr std::uint64_t kSize = 3u * 1024u * 1024u + 17u;
    PackageServer server;
    int opened = 0;
    ASSERT_TRUE(server.start(
        "t42", kSize,
        [&](std::string *)
        {
            ++opened;
            return std::make_unique<Pattern>(kSize);
        },
        kPort));
    EXPECT_EQ(server.url(), "http://127.0.0.1:19063/t42/package.pkg");

    const std::string head = ask("HEAD /t42/package.pkg HTTP/1.1\r\n");
    EXPECT_EQ(head.rfind("HTTP/1.1 200 OK", 0), 0u);
    EXPECT_NE(head.find("Content-Length: " + std::to_string(kSize)), std::string::npos);
    EXPECT_NE(head.find("Accept-Ranges: bytes"), std::string::npos);
    EXPECT_TRUE(body_of(head).empty());

    const std::string part =
        ask("GET /t42/package.pkg HTTP/1.1\r\nRange: bytes=1048570-1048585\r\n");
    EXPECT_EQ(part.rfind("HTTP/1.1 206 Partial Content", 0), 0u);
    EXPECT_NE(part.find("Content-Range: bytes 1048570-1048585/" + std::to_string(kSize)),
              std::string::npos);
    const std::string bytes = body_of(part);
    ASSERT_EQ(bytes.size(), 16u);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        EXPECT_EQ(static_cast<unsigned char>(bytes[i]), (1048570u + i) % 251u);

    const std::string whole = body_of(ask("GET /t42/package.pkg HTTP/1.1\r\n"));
    ASSERT_EQ(whole.size(), kSize);
    EXPECT_EQ(static_cast<unsigned char>(whole[kSize - 1]), (kSize - 1) % 251u);
    EXPECT_EQ(server.sent(), kSize + 16u);
    EXPECT_GE(opened, 2);

    EXPECT_EQ(ask("GET /other/package.pkg HTTP/1.1\r\n").rfind("HTTP/1.1 404", 0), 0u);
    EXPECT_EQ(
        ask("GET /t42/package.pkg HTTP/1.1\r\nRange: bytes=99999999-\r\n").rfind("HTTP/1.1 416", 0),
        0u);
    server.stop();
    EXPECT_FALSE(server.running());
}

TEST(PackageServer, TakesAnAbsoluteAddressAndSaysWhatItWasAsked)
{
    using hui::orion::request_path;
    EXPECT_EQ(request_path("GET /t1/package.pkg HTTP/1.1"), "/t1/package.pkg");
    EXPECT_EQ(request_path("GET /t1/package.pkg?x=1 HTTP/1.1"), "/t1/package.pkg");
    EXPECT_EQ(request_path("GET http://127.0.0.1:9063/t1/package.pkg HTTP/1.1"), "/t1/package.pkg");
    EXPECT_EQ(request_path("GET http://127.0.0.1:9063 HTTP/1.1"), "/");
    EXPECT_EQ(request_path("nonsense"), "");

    PackageServer server;
    ASSERT_TRUE(server.start(
        "t7", 100, [](std::string *) { return std::make_unique<Pattern>(100); }, kPort));
    const std::string head = ask("HEAD http://127.0.0.1:19063/t7/package.pkg HTTP/1.1\r\n");
    EXPECT_EQ(head.rfind("HTTP/1.1 200 OK", 0), 0u);
    EXPECT_NE(head.find("Last-Modified: Wed, 01 Jan 2025 00:00:00 GMT"), std::string::npos);
    EXPECT_NE(head.find("Date: "), std::string::npos);
    ask("GET /t7/package.pkg.crc HTTP/1.1\r\n");
    ask("GET /t7/package.pkg HTTP/1.1\r\nRange: bytes=10-19\r\n");
    const std::vector<std::string> requests = server.requests();
    ASSERT_EQ(requests.size(), 3u);
    EXPECT_EQ(requests[0], "HEAD http://127.0.0.1:19063/t7/package.pkg HTTP/1.1 -> 200");
    EXPECT_EQ(requests[1], "GET /t7/package.pkg.crc HTTP/1.1 -> 404");
    EXPECT_EQ(requests[2], "GET /t7/package.pkg HTTP/1.1 -> 206 10-19");
    server.stop();
}

} // namespace
