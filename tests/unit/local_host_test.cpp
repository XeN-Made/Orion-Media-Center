// ORION Media Center - Tests: which hosts may be asked over plain http.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/local_host.hpp"

#include <gtest/gtest.h>

namespace hui
{
namespace
{

TEST(LocalHost, AllowsOnlyTheLocalNetwork)
{
    for (const char *host :
         {"192.168.1.20", "10.0.0.5", "172.16.0.1", "172.31.255.255", "127.0.0.1", "169.254.10.1",
          "nas", "localhost", "romm.local", "Server.LAN", "box.home", "romm.internal"})
        EXPECT_TRUE(net::is_local_host(host)) << host;
    for (const char *host :
         {"", "8.8.8.8", "172.32.0.1", "172.15.0.1", "192.169.0.1", "romm.example.com",
          "github.com", "192.168.1", "192.168.1.256", "10.0.0.1.evil.com", "notlocal.com"})
        EXPECT_FALSE(net::is_local_host(host)) << host;
}

TEST(LocalHost, ReadsAUrlsHost)
{
    EXPECT_EQ(net::url_host("http://192.168.1.20:8080/api/roms"), "192.168.1.20");
    EXPECT_EQ(net::url_host("https://RomM.Local/"), "romm.local");
    EXPECT_EQ(net::url_host("http://nas?x=1"), "nas");
    EXPECT_EQ(net::url_host("ftp://192.168.1.20"), "");
    EXPECT_EQ(net::url_host("192.168.1.20"), "");
}

} // namespace
} // namespace hui
