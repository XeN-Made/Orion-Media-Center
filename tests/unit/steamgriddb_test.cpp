// ORION Media Center - Tests: SteamGridDB covers and the keys file.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "orion/steamgriddb.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

namespace hui
{
namespace
{

std::string fresh_folder(const char *name)
{
    const std::string folder = ::testing::TempDir() + name;
    std::remove((folder + "/keys.txt").c_str());
    save::ensure_directory(folder);
    return folder;
}

TEST(SteamGridDb, KeepsKeysBesideTheOthers)
{
    const std::string state = fresh_folder("orion-keys");
    EXPECT_EQ(orion::read_service_key(state, orion::kSteamGridDb), "");
    ASSERT_EQ(orion::write_service_key(state, orion::kSteamGridDb, "  abc123  "), "");
    EXPECT_EQ(orion::read_service_key(state, orion::kSteamGridDb), "abc123");

    // Another service's line stays, and a new key replaces the old one.
    std::string text;
    ASSERT_TRUE(save::read_file(state + "/keys.txt", &text));
    ASSERT_TRUE(save::write_atomic(state + "/keys.txt", text + "screenscraper = me\n").empty());
    ASSERT_EQ(orion::write_service_key(state, orion::kSteamGridDb, "def456"), "");
    EXPECT_EQ(orion::read_service_key(state, orion::kSteamGridDb), "def456");
    EXPECT_EQ(orion::read_service_key(state, "screenscraper"), "me");

    // Empty removes it; a key is one line.
    ASSERT_EQ(orion::write_service_key(state, orion::kSteamGridDb, ""), "");
    EXPECT_EQ(orion::read_service_key(state, orion::kSteamGridDb), "");
    EXPECT_EQ(orion::read_service_key(state, "screenscraper"), "me");
    EXPECT_NE(orion::write_service_key(state, orion::kSteamGridDb, "a\nb"), "");
}

TEST(SteamGridDb, SearchesByTheNameWithoutItsTags)
{
    EXPECT_EQ(orion::grid_query("Chrono Trigger (USA) [!]"), "Chrono Trigger");
    EXPECT_EQ(orion::grid_query("  Moon   Garden_Deluxe (Rev 1) "), "Moon Garden Deluxe");
    EXPECT_EQ(orion::grid_query("(Beta)"), "");
    EXPECT_EQ(orion::grid_search_url("Chrono Trigger (USA)"),
              "https://www.steamgriddb.com/api/v2/search/autocomplete/Chrono%20Trigger");
    EXPECT_NE(orion::grid_covers_url(42).find("/grids/game/42?"), std::string::npos);
    EXPECT_NE(orion::grid_covers_url(42).find("mimes=image/png,image/jpeg"), std::string::npos);
}

TEST(SteamGridDb, ReadsItsAnswers)
{
    std::vector<long long> games;
    ASSERT_TRUE(orion::parse_grid_search(
        R"({"success": true, "data": [{"id": 5241, "name": "A"}, {"id": 77, "name": "B"}]})",
        &games));
    EXPECT_EQ(games, (std::vector<long long>{5241, 77}));
    EXPECT_FALSE(orion::parse_grid_search(R"({"success": false, "errors": ["bad key"]})", &games));

    std::vector<orion::GridCover> covers;
    ASSERT_TRUE(orion::parse_grid_covers(
        R"({"success": true, "data": [
            {"mime": "image/png", "url": "https://cdn2.steamgriddb.com/grid/a.png",
             "thumb": "https://cdn2.steamgriddb.com/thumb/a.jpg"},
            {"mime": "image/webp", "url": "https://cdn2.steamgriddb.com/grid/b.webp"},
            {"mime": "image/jpeg", "url": "https://cdn2.steamgriddb.com/grid/c.jpg"},
            {"mime": "image/png", "url": "http://insecure/d.png"}]})",
        &covers));
    ASSERT_EQ(covers.size(), 2u); // no WebP, nothing but https
    EXPECT_EQ(covers[0].thumb, "https://cdn2.steamgriddb.com/thumb/a.jpg");
    EXPECT_EQ(covers[1].thumb, covers[1].url); // no small copy: the picture itself
}

TEST(SteamGridDb, FindsTheBestGamesCovers)
{
    std::vector<std::string> asked;
    const orion::HttpGetWith get = [&](const std::string &url,
                                       const std::vector<std::string> &headers, std::size_t,
                                       std::string *body, std::string *)
    {
        asked.push_back(url);
        EXPECT_EQ(headers, (std::vector<std::string>{"Authorization: Bearer k3y"}));
        if (url.find("/search/autocomplete/") != std::string::npos)
            *body = R"({"success": true, "data": [{"id": 9}]})";
        else
            *body = R"({"success": true, "data": [
                {"mime": "image/png", "url": "https://x/1.png"},
                {"mime": "image/png", "url": "https://x/2.png"},
                {"mime": "image/png", "url": "https://x/3.png"}]})";
        return true;
    };
    std::vector<orion::GridCover> covers;
    std::string error;
    ASSERT_TRUE(orion::find_grid_covers(get, "k3y", "Moon Garden (USA)", 2, &covers, &error))
        << error;
    ASSERT_EQ(covers.size(), 2u);
    EXPECT_EQ(covers[1].url, "https://x/2.png");
    ASSERT_EQ(asked.size(), 2u);
    EXPECT_NE(asked[1].find("/grids/game/9?"), std::string::npos);

    // No key: not asked at all.
    asked.clear();
    EXPECT_FALSE(orion::find_grid_covers(get, "", "Moon Garden", 2, &covers, &error));
    EXPECT_TRUE(asked.empty());
}

TEST(SteamGridDb, PicksACoverToKeep)
{
    const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + "picture";
    const std::string jpg = std::string("\xFF\xD8\xFF", 3) + "jpeg";
    const orion::HttpGetWith get = [&](const std::string &url, const std::vector<std::string> &,
                                       std::size_t, std::string *body, std::string *error)
    {
        if (url.find("/search/") != std::string::npos)
            *body = R"({"success": true, "data": [{"id": 1}]})";
        else if (url.find("/grids/") != std::string::npos)
            *body = R"({"success": true, "data": [
                {"mime": "image/png", "url": "https://x/a.png", "thumb": "https://x/a-small.png"},
                {"mime": "image/jpeg", "url": "https://x/b.jpg", "thumb": "https://x/gone.jpg"},
                {"mime": "image/jpeg", "url": "https://x/c.jpg", "thumb": "https://x/c-small.jpg"}]})";
        else if (url == "https://x/gone.jpg")
        {
            *error = "HTTP 404";
            return false;
        }
        else
            *body = url.find(".jpg") != std::string::npos ? jpg : png;
        return true;
    };
    const std::string folder = ::testing::TempDir() + "orion-picks";
    orion::GridPicker picker(get);
    ASSERT_TRUE(picker.search("k", "Moon Garden", folder, "rom-0000000000aa"));
    picker.wait();
    EXPECT_TRUE(picker.take_finished());
    EXPECT_EQ(picker.count(), 3u);
    const auto ready = picker.take_ready(); // the one whose small copy failed is left out
    ASSERT_EQ(ready.size(), 2u);
    EXPECT_EQ(ready[0].first, 0u);
    EXPECT_EQ(ready[0].second, folder + "/rom-0000000000aa-0.png");
    EXPECT_EQ(ready[1].first, 2u);
    EXPECT_EQ(ready[1].second, folder + "/rom-0000000000aa-2.jpg");

    // Keeping the third: fetched in full, named by its kind.
    ASSERT_TRUE(picker.keep(2, folder + "/rom-0000000000aa"));
    picker.wait();
    EXPECT_TRUE(picker.take_finished());
    EXPECT_EQ(picker.kept(), folder + "/rom-0000000000aa.jpg");
    std::string bytes;
    ASSERT_TRUE(save::read_file(picker.kept(), &bytes));
    EXPECT_EQ(bytes, jpg);
    EXPECT_FALSE(picker.keep(7, folder + "/x")); // no such cover
}

} // namespace
} // namespace hui
