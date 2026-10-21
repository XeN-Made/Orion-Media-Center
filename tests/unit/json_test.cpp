// ORION Media Center - Tests: the JSON reader.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/json.hpp"

#include <gtest/gtest.h>

#include <string>

namespace hui
{
namespace
{

TEST(Json, ReadsNestedObjectsAndArrays)
{
    json::Value root;
    ASSERT_TRUE(json::parse(R"({"a": {"b": [1, 2.5, -3e2]}, "t": true, "n": null})", &root));
    EXPECT_TRUE(root.is_object());
    EXPECT_EQ(root.size(), 3u);
    EXPECT_EQ(root["a"]["b"].size(), 3u);
    EXPECT_DOUBLE_EQ(root["a"]["b"][1].number_or(0.0), 2.5);
    EXPECT_DOUBLE_EQ(root["a"]["b"][2].number_or(0.0), -300.0);
    EXPECT_TRUE(root["t"].bool_or(false));
    EXPECT_EQ(root["n"].type(), json::Type::null);
}

TEST(Json, MissingKeysChainToNull)
{
    json::Value root;
    ASSERT_TRUE(json::parse(R"({"a": 1})", &root));
    EXPECT_EQ(root["x"]["y"][3].type(), json::Type::null);
    EXPECT_EQ(root["x"].string_or("fallback"), "fallback");
    EXPECT_DOUBLE_EQ(root["a"].number_or(0.0), 1.0);
    EXPECT_EQ(root["a"].string_or("not a string"), "not a string");
}

TEST(Json, DecodesEscapesAndSurrogatePairs)
{
    json::Value root;
    ASSERT_TRUE(json::parse(R"(["tab\there", "é", "🎮", "a\/b"])", &root));
    EXPECT_EQ(root[0].string_or(""), "tab\there");
    EXPECT_EQ(root[1].string_or(""), "\xc3\xa9");
    EXPECT_EQ(root[2].string_or(""), "\xf0\x9f\x8e\xae");
    EXPECT_EQ(root[3].string_or(""), "a/b");
}

TEST(Json, SkipsAByteOrderMark)
{
    json::Value root;
    ASSERT_TRUE(json::parse("\xef\xbb\xbf{\"k\": \"v\"}", &root));
    EXPECT_EQ(root["k"].string_or(""), "v");
}

TEST(Json, RefusesBrokenDocumentsWithAPlace)
{
    const char *broken[] = {
        "",    "{",   R"({"a": })", R"({"a" 1})", "[1, 2,]", R"("open)", R"("\ud800")",
        "tru", "01x", "{} {}",      "\"\x01\"",
    };
    for (const char *text : broken)
    {
        json::Value root;
        std::string error;
        EXPECT_FALSE(json::parse(text, &root, &error)) << text;
        EXPECT_FALSE(error.empty()) << text;
        EXPECT_EQ(root.type(), json::Type::null) << text;
    }
}

TEST(Json, RefusesDeepNesting)
{
    std::string deep(100, '[');
    deep += std::string(100, ']');
    json::Value root;
    EXPECT_FALSE(json::parse(deep, &root));
    std::string fine(60, '[');
    fine += std::string(60, ']');
    EXPECT_TRUE(json::parse(fine, &root));
}

} // namespace
} // namespace hui
