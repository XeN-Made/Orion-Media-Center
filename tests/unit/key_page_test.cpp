// ORION Media Center - Tests: the key page, without a socket.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/key_page.hpp"

#include "core/save_file.hpp"
#include "orion/steamgriddb.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

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

} // namespace
} // namespace hui
