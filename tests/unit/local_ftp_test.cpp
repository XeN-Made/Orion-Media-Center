// ORION Media Center - Tests: the small FTP client, with a fake server.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/local_ftp.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace hui
{
namespace
{

TEST(LocalFtp, ReadsReplies)
{
    std::size_t end = 0;
    EXPECT_EQ(orion::reply_code("220 ready\r\n", &end), 220);
    EXPECT_EQ(end, 11u);
    EXPECT_EQ(orion::reply_code("230-welcome\r\nmore\r\n", &end), 0); // not finished yet
    EXPECT_EQ(orion::reply_code("230-welcome\r\nmore\r\n230 in\r\n", &end), 230);
    EXPECT_EQ(orion::reply_code("150 sending", &end), 0); // no line end yet
    EXPECT_EQ(orion::passive_port("227 Entering Passive Mode (127,0,0,1,4,1)"), 1025);
    EXPECT_EQ(orion::passive_port("227 Passive (192,168,4,41,200,13)."), 200 * 256 + 13);
    EXPECT_EQ(orion::passive_port("500 no"), 0);
    EXPECT_EQ(orion::passive_port("227 (1,2,3)"), 0);
}

// A listening socket on the loopback, and its port.
int listen_on(int *port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    ::listen(fd, 1);
    socklen_t length = sizeof(address);
    ::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
    *port = ntohs(address.sin_port);
    return fd;
}

std::string read_line(int fd)
{
    std::string line;
    char c = 0;
    while (::recv(fd, &c, 1, 0) == 1 && c != '\n')
        line += c;
    return line;
}

void say(int fd, const std::string &text)
{
    ::send(fd, text.data(), text.size(), 0);
}

TEST(LocalFtp, FetchesAFileAndHearsAMissingOne)
{
    int port = 0;
    const int server = listen_on(&port);
    std::thread fake(
        [server]
        {
            const int control = ::accept(server, nullptr, nullptr);
            say(control, "220 ftpsrv\r\n");
            read_line(control); // USER
            say(control, "331 password\r\n");
            read_line(control); // PASS
            say(control, "230-welcome\r\n230 in\r\n");
            read_line(control); // TYPE I
            say(control, "200 binary\r\n");
            for (int file = 0; file < 2; ++file)
            {
                read_line(control); // PASV
                int data_port = 0;
                const int data_server = listen_on(&data_port);
                say(control, "227 Entering Passive Mode (127,0,0,1," +
                                 std::to_string(data_port / 256) + "," +
                                 std::to_string(data_port % 256) + ")\r\n");
                const int data = ::accept(data_server, nullptr, nullptr);
                const std::string retr = read_line(control);
                if (retr.find("pic0.png") != std::string::npos)
                {
                    say(control, "150 sending\r\n");
                    say(data, "PNGDATA");
                    ::close(data);
                    say(control, "226 done\r\n");
                }
                else
                {
                    ::close(data);
                    say(control, "550 no such file\r\n");
                }
                ::close(data_server);
            }
            read_line(control); // QUIT
            ::close(control);
        });

    orion::LocalFtp ftp;
    ASSERT_TRUE(ftp.open(port));
    std::string bytes;
    EXPECT_TRUE(ftp.get("/user/appmeta/PPSA08560/pic0.png", 1024, &bytes));
    EXPECT_EQ(bytes, "PNGDATA");
    EXPECT_FALSE(ftp.get("/user/appmeta/PPSA08560/pic1.png", 1024, &bytes));
    EXPECT_TRUE(bytes.empty());
    ftp.close();
    fake.join();
    ::close(server);
}

TEST(LocalFtp, SaysSoWhenNothingListens)
{
    int port = 0;
    ::close(listen_on(&port)); // a port nobody listens on any more
    orion::LocalFtp ftp;
    EXPECT_FALSE(ftp.open(port));
}

} // namespace
} // namespace hui
