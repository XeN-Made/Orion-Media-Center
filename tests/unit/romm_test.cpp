// ORION Media Center - Tests: the RomM client, against a fake server.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/romm.hpp"

#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace hui
{
namespace
{

TEST(Romm, MakesABaseAddressOfWhatWasTyped)
{
    EXPECT_EQ(orion::romm_base("192.168.4.20:3000"), "http://192.168.4.20:3000");
    EXPECT_EQ(orion::romm_base(" http://nas:3000/ "), "http://nas:3000");
    EXPECT_EQ(orion::romm_base("https://romm.example.com/api"), "https://romm.example.com");
    EXPECT_EQ(orion::romm_base("romm.example.com"), "https://romm.example.com");
    EXPECT_EQ(orion::romm_base(""), "");
    EXPECT_EQ(orion::romm_base("not an address"), "");
}

TEST(Romm, BuildsItsAddresses)
{
    const std::string base = "http://nas:3000";
    EXPECT_EQ(orion::romm_platforms_url(base), "http://nas:3000/api/platforms");
    const std::string page = orion::romm_games_url(base, 7, 200, 200);
    EXPECT_EQ(
        page.rfind("http://nas:3000/api/roms?platform_ids=7&platform_id=7&limit=200&offset=200", 0),
        0u);
    orion::RommGame game;
    game.id = 12;
    game.fs_name = "Moon Garden (USA).gba";
    EXPECT_EQ(orion::romm_content_url(base, game),
              "http://nas:3000/api/roms/12/content/Moon%20Garden%20%28USA%29.gba");
    game.multi = true;
    game.fs_name = "Quiet Bay";
    EXPECT_EQ(orion::romm_content_url(base, game),
              "http://nas:3000/api/roms/12/content/Quiet%20Bay.zip");
    EXPECT_EQ(orion::romm_game_path("/data/orion/romm/games", game),
              "/data/orion/romm/games//Quiet Bay.zip"); // no system folder known here
    game.cover = "/assets/romm/resources/roms/1/12/cover/big.png?ts=1";
    EXPECT_EQ(orion::romm_cover_url(base, game),
              "http://nas:3000/assets/romm/resources/roms/1/12/cover/big.png?ts=1");
    game.cover = "https://images.example/cover.jpg";
    EXPECT_EQ(orion::romm_cover_url(base, game), "https://images.example/cover.jpg");
    EXPECT_EQ(orion::romm_headers("abc"), (std::vector<std::string>{"Authorization: Bearer abc"}));
    EXPECT_TRUE(orion::romm_headers("").empty());
}

const char *kPlatforms = R"([
    {"id": 1, "slug": "gba", "fs_slug": "gba", "name": "Game Boy Advance",
     "display_name": "Game Boy Advance", "rom_count": 3},
    {"id": 2, "slug": "psx", "fs_slug": "psx", "name": "PlayStation", "rom_count": 0},
    {"id": 0, "slug": "broken"}
])";

TEST(Romm, ReadsSystemsGamesAndPairing)
{
    std::vector<orion::RommPlatform> platforms;
    ASSERT_TRUE(orion::parse_romm_platforms(kPlatforms, &platforms));
    ASSERT_EQ(platforms.size(), 2u);
    EXPECT_EQ(platforms[0].fs_slug, "gba");
    EXPECT_EQ(platforms[0].rom_count, 3);
    EXPECT_FALSE(orion::parse_romm_platforms("not json", &platforms));

    std::vector<orion::RommGame> games;
    long long total = 0;
    ASSERT_TRUE(orion::parse_romm_games(
        R"({"items": [
            {"id": 10, "platform_id": 1, "name": "Moon Garden", "fs_name": "Moon Garden (USA).gba",
             "fs_size_bytes": 4194304, "path_cover_large": "/assets/romm/resources/a.png",
             "has_multiple_files": false},
            {"id": 11, "platform_id": 1, "name": null, "fs_name": "Comet Post.gba",
             "fs_name_no_ext": "Comet Post"},
            {"id": 12, "platform_id": 1, "name": "Escape", "fs_name": "../../etc"},
            {"id": 13, "platform_id": 9, "name": "No System", "fs_name": "x.bin"}
        ], "total": 3})",
        platforms, &games, &total));
    EXPECT_EQ(total, 3);
    ASSERT_EQ(games.size(), 2u); // a file name with a slash, and a game of no known system, go
    EXPECT_EQ(games[0].fs_slug, "gba");
    EXPECT_EQ(games[0].size, 4194304u);
    EXPECT_EQ(games[0].cover, "/assets/romm/resources/a.png");
    EXPECT_EQ(games[1].name, "Comet Post"); // unidentified: its file's name

    EXPECT_EQ(orion::parse_romm_pairing(R"({"id": 3, "raw_token": "rmm_abc123"})"), "rmm_abc123");
    EXPECT_EQ(orion::parse_romm_pairing(R"({"detail": "Invalid code"})"), "");

    // The catalogue keeps them as they were.
    const std::vector<orion::RommGame> back =
        orion::read_romm_catalogue(orion::romm_catalogue_text(games) + "junk line\n");
    ASSERT_EQ(back.size(), 2u);
    EXPECT_EQ(back[0].id, 10);
    EXPECT_EQ(back[0].name, "Moon Garden");
    EXPECT_EQ(back[0].fs_name, "Moon Garden (USA).gba");
    EXPECT_EQ(back[0].fs_slug, "gba");
    EXPECT_EQ(back[0].size, 4194304u);
    EXPECT_EQ(back[0].cover, "/assets/romm/resources/a.png");
    EXPECT_EQ(back[1].cover, "");
}

TEST(Romm, PairsWithACode)
{
    std::string asked;
    std::string sent;
    const orion::HttpPost post = [&](const std::string &url, const std::vector<std::string> &,
                                     const std::string &body, std::size_t, std::string *answer,
                                     std::string *)
    {
        asked = url;
        sent = body;
        *answer = R"({"raw_token": "rmm_token"})";
        return true;
    };
    std::string token;
    EXPECT_EQ(orion::romm_pair(post, "http://nas:3000", " AB12CD ", &token), "");
    EXPECT_EQ(token, "rmm_token");
    EXPECT_EQ(asked, "http://nas:3000/api/client-tokens/exchange");
    EXPECT_EQ(sent, R"({"code": "AB12CD"})");
    EXPECT_EQ(orion::romm_pair(post, "http://nas:3000", "bad\"code", &token), "no pairing code");
    EXPECT_EQ(orion::romm_pair(post, "", "AB12CD", &token), "no RomM address");
}

// The whole catalogue, a page at a time, then the covers it lacks.
TEST(Romm, SyncsTheCatalogueAndItsCovers)
{
    const std::string dir = ::testing::TempDir() + "orion-romm-sync";
    ASSERT_TRUE(save::ensure_directory(dir));
    std::remove((dir + "/romm-games.tsv").c_str());
    std::remove((dir + "/romm/covers/10.png").c_str());
    std::map<std::string, std::string> pages;
    pages["http://nas:3000/api/platforms"] = kPlatforms;
    pages[orion::romm_games_url("http://nas:3000", 1, 0, 200)] =
        R"({"items": [{"id": 10, "platform_id": 1, "name": "Moon Garden", "fs_name": "a.gba",
                       "path_cover_large": "/c/10.png"},
                      {"id": 11, "platform_id": 1, "name": "Comet Post", "fs_name": "b.gba"}],
            "total": 2})";
    std::vector<std::string> asked;
    std::vector<std::string> keys;
    const orion::HttpGetWith get = [&](const std::string &url,
                                       const std::vector<std::string> &headers, std::size_t,
                                       std::string *body, std::string *error)
    {
        asked.push_back(url);
        keys = headers;
        const auto page = pages.find(url);
        if (page == pages.end())
        {
            *error = "HTTP 404";
            return false;
        }
        *body = page->second;
        return true;
    };
    std::vector<std::string> fetched;
    const orion::HttpDownloadWith download =
        [&](const std::string &url, const std::vector<std::string> &, const std::string &path,
            std::size_t, std::string *)
    {
        fetched.push_back(url);
        return save::write_atomic(path, "png").empty();
    };
    orion::RommSync sync(get, download);
    ASSERT_TRUE(sync.start("http://nas:3000", "rmm_token", dir));
    sync.wait();
    EXPECT_TRUE(sync.take_finished());
    const orion::RommSync::Progress done = sync.progress();
    EXPECT_EQ(done.error, "");
    EXPECT_EQ(done.platforms, 2);
    EXPECT_EQ(done.games, 2);
    EXPECT_EQ(done.covers, 1);
    EXPECT_EQ(asked.size(), 2u); // the systems, then one page: the empty system is not asked
    EXPECT_EQ(keys, (std::vector<std::string>{"Authorization: Bearer rmm_token"}));
    EXPECT_EQ(fetched, (std::vector<std::string>{"http://nas:3000/c/10.png"}));
    std::string text;
    ASSERT_TRUE(save::read_file(dir + "/romm-games.tsv", &text));
    EXPECT_EQ(orion::read_romm_catalogue(text).size(), 2u);

    // Again: the cover is there already.
    fetched.clear();
    ASSERT_TRUE(sync.start("http://nas:3000", "rmm_token", dir));
    sync.wait();
    EXPECT_TRUE(fetched.empty());

    // No token, no start; a server that does not answer is reported.
    EXPECT_FALSE(sync.start("http://nas:3000", "", dir));
    pages.clear();
    ASSERT_TRUE(sync.start("http://nas:3000", "rmm_token", dir));
    sync.wait();
    EXPECT_EQ(sync.progress().error, "HTTP 404");
}

} // namespace
} // namespace hui
