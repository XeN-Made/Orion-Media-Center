// ORION Media Center - Tests: box art matching and ORION's downloader, with a fake server.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "library/art_match.hpp"
#include "orion/art_fetcher.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace hui
{
namespace
{

const std::vector<std::string> kPictures = {
    "Chrono Trigger (USA).png",
    "Chrono Trigger (Japan).png",
    "Legend of Zelda, The - The Minish Cap (USA).png",
    "Ratchet _ Clank (USA).png",
    "Pokemon - Emerald Version (USA, Europe).png",
    "Mario Kart DS (Europe) (En,Fr,De,Es,It).png",
    "Mario Kart DS (USA, Australia).png",
    "Mario Kart DS (USA, Australia) (Beta).png",
};

TEST(ArtMatch, FindsTheReleaseOfTheRomsRegion)
{
    using library::match_art;
    EXPECT_EQ(match_art("3055 - Chrono Trigger (U)(XenoPhobia).nds", kPictures),
              "Chrono Trigger (USA).png");
    EXPECT_EQ(match_art("Chrono Trigger (J).nds", kPictures), "Chrono Trigger (Japan).png");
    EXPECT_EQ(match_art("The Legend of Zelda - The Minish Cap (U).gba", kPictures),
              "Legend of Zelda, The - The Minish Cap (USA).png");
    EXPECT_EQ(match_art("Ratchet & Clank (USA).iso", kPictures), "Ratchet _ Clank (USA).png");
    EXPECT_EQ(match_art("Pokemon - Emerald Version (U) [!].gba", kPictures),
              "Pokemon - Emerald Version (USA, Europe).png");
    EXPECT_EQ(match_art("Mario Kart DS (E).nds", kPictures),
              "Mario Kart DS (Europe) (En,Fr,De,Es,It).png");
    // No region: USA first, and the release before its beta.
    EXPECT_EQ(match_art("Mario Kart DS.nds", kPictures), "Mario Kart DS (USA, Australia).png");
    EXPECT_EQ(match_art("Unknown Game (USA).gba", kPictures), "");
    EXPECT_EQ(match_art("(USA).gba", kPictures), "");
}

TEST(ArtMatch, ReadsAnIndexPageAndEncodesUrls)
{
    const std::string page =
        R"(<a href="../">Parent</a><a href="Chrono%20Trigger%20%28USA%29.png">x</a>)"
        R"(<a href="sub/evil.png">y</a><a href="readme.txt">z</a><a href=".hidden.png">w</a>)";
    const std::vector<std::string> pictures = library::parse_art_index(page);
    ASSERT_EQ(pictures.size(), 1u);
    EXPECT_EQ(pictures[0], "Chrono Trigger (USA).png");
    EXPECT_EQ(library::url_encode("Nintendo - Nintendo DS"), "Nintendo%20-%20Nintendo%20DS");
    EXPECT_EQ(library::url_encode("A&B (USA).png"), "A%26B%20%28USA%29.png");
}

// A server that answers from a map, counting what it was asked.
struct FakeServer
{
    std::map<std::string, std::string> pages;
    std::vector<std::string> asked;

    orion::HttpGet get()
    {
        return
            [this](const std::string &url, std::size_t limit, std::string *body, std::string *error)
        {
            asked.push_back(url);
            const auto page = pages.find(url);
            if (page == pages.end())
            {
                *error = "HTTP 404";
                return false;
            }
            if (page->second.size() > limit)
            {
                *error = "too large";
                return false;
            }
            *body = page->second;
            return true;
        };
    }
};

const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "picture";
const std::string kDs =
    "https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20DS/Named_Boxarts/";

orion::ArtJob job(const std::string &id, const std::string &rom, const std::string &dir)
{
    orion::ArtJob out;
    out.id = id;
    out.system = "Nintendo - Nintendo DS";
    out.rom_file = rom;
    const std::size_t dot = rom.rfind('.');
    out.target = dir + "/art/Nintendo - Nintendo DS/" + rom.substr(0, dot) + ".png";
    return out;
}

TEST(ArtFetcher, DownloadsWhatMatchesAndReportsTheRest)
{
    const std::string dir = ::testing::TempDir() + "hui-art-fetcher";
    std::remove((dir + "/art/Nintendo - Nintendo DS/3055 - Chrono Trigger (U).png").c_str());
    FakeServer server;
    server.pages[kDs] = R"(<a href="Chrono%20Trigger%20%28USA%29.png">)"
                        R"(<a href="Moon%20Garden%20%28Japan%29.png">)";
    server.pages[kDs + "Chrono%20Trigger%20%28USA%29.png"] = kPng;
    server.pages[kDs + "Moon%20Garden%20%28Japan%29.png"] = "<html>not a picture</html>";
    orion::ArtFetcher art(server.get());
    ASSERT_TRUE(art.start({job("rom-000000000001", "3055 - Chrono Trigger (U).nds", dir),
                           job("rom-000000000002", "Starfall (USA).nds", dir),
                           job("rom-000000000003", "Moon Garden (Japan).nds", dir)},
                          dir + "/art.txt"));
    art.wait();
    EXPECT_FALSE(art.running());
    EXPECT_TRUE(art.take_finished());
    EXPECT_FALSE(art.take_finished()); // once

    const orion::ArtFetcher::Progress done = art.progress();
    EXPECT_EQ(done.total, 3);
    EXPECT_EQ(done.done, 3);
    EXPECT_EQ(done.found, 1);
    EXPECT_EQ(done.missing, 1);
    EXPECT_EQ(done.failed, 1); // the page that is not a PNG
    EXPECT_EQ(done.error, "not a PNG");
    // One index for the system, however many games it has.
    EXPECT_EQ(std::count(server.asked.begin(), server.asked.end(), kDs), 1);

    const std::vector<orion::ArtFound> found = art.take_found();
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].id, "rom-000000000001");
    std::string bytes;
    ASSERT_TRUE(save::read_file(found[0].path, &bytes));
    EXPECT_EQ(bytes, kPng);
    EXPECT_TRUE(art.take_found().empty());
    EXPECT_EQ(art.take_missing(), std::vector<std::string>{"rom-000000000002"});
}

TEST(ArtFetcher, AsksSteamGridDbForWhatTheOthersLack)
{
    const std::string dir = ::testing::TempDir() + "hui-art-grid";
    std::remove((dir + "/art/Nintendo - Nintendo DS/Starfall (USA).png").c_str());
    FakeServer server;
    server.pages[kDs] = R"(<a href="Chrono%20Trigger%20%28USA%29.png">)";
    server.pages["https://cdn.example/starfall.png"] = kPng;
    std::vector<std::string> searched;
    const orion::HttpGetWith grid = [&](const std::string &url, const std::vector<std::string> &,
                                        std::size_t, std::string *body, std::string *)
    {
        searched.push_back(url);
        *body = url.find("/search/") != std::string::npos
                    ? R"({"success": true, "data": [{"id": 3}]})"
                    : R"({"success": true, "data": [{"mime": "image/png",
                          "url": "https://cdn.example/starfall.png"}]})";
        return true;
    };
    orion::ArtFetcher art(server.get());
    art.set_grid_source("k", grid);
    ASSERT_TRUE(art.start({job("rom-000000000002", "Starfall (USA).nds", dir)}, dir + "/art.txt"));
    art.wait();
    EXPECT_EQ(art.progress().found, 1);
    EXPECT_EQ(art.progress().missing, 0);
    ASSERT_EQ(searched.size(), 2u);
    EXPECT_NE(searched[0].find("/autocomplete/Starfall"), std::string::npos);
    std::string bytes;
    ASSERT_TRUE(save::read_file(dir + "/art/Nintendo - Nintendo DS/Starfall (USA).png", &bytes));
    EXPECT_EQ(bytes, kPng);

    // No key: SteamGridDB is not asked, and the game stays missing.
    searched.clear();
    art.set_grid_source("", grid);
    ASSERT_TRUE(
        art.start({job("rom-000000000004", "Comet Post (USA).nds", dir)}, dir + "/art.txt"));
    art.wait();
    EXPECT_EQ(art.progress().missing, 1);
    EXPECT_TRUE(searched.empty());
}

TEST(ArtFetcher, StopsAskingWhenTheNetworkIsGone)
{
    const std::string dir = ::testing::TempDir() + "hui-art-offline";
    FakeServer server; // knows nothing: every request fails
    orion::ArtFetcher art(server.get());
    std::vector<orion::ArtJob> jobs;
    const char *systems[] = {"A - One", "B - Two", "C - Three", "D - Four", "E - Five"};
    for (const char *system : systems)
    {
        orion::ArtJob next = job("rom-00000000000a", "Game (USA).bin", dir);
        next.system = system;
        jobs.push_back(next);
    }
    ASSERT_TRUE(art.start(std::move(jobs), ""));
    art.wait();
    const orion::ArtFetcher::Progress done = art.progress();
    EXPECT_EQ(done.failed, 5);
    EXPECT_EQ(done.error, "HTTP 404");
    EXPECT_EQ(server.asked.size(), 3u);      // then it gives up
    EXPECT_TRUE(art.take_missing().empty()); // failures are worth another try
}

// GameTDB: covers by a game's ID, its region first; Switch games by name,
// through an index of GameTDB's list.
TEST(ArtMatch, FindsGameTdbCoversByIdAndSwitchGamesByName)
{
    EXPECT_EQ(library::gametdb_platform("Nintendo - Wii U"), "wiiu");
    EXPECT_EQ(library::gametdb_platform("Nintendo - Switch"), "switch");
    EXPECT_EQ(library::gametdb_platform("Nintendo - Game Boy"), "");
    const std::vector<std::string> wiiu = library::gametdb_covers("wiiu", "AQBP01");
    ASSERT_EQ(wiiu.size(), 6u);
    EXPECT_EQ(wiiu[0], "https://art.gametdb.com/wiiu/coverHQ/EN/AQBP01.jpg"); // P: Europe
    EXPECT_EQ(wiiu[1], "https://art.gametdb.com/wiiu/coverHQ/US/AQBP01.jpg");
    EXPECT_EQ(wiiu[3], "https://art.gametdb.com/wiiu/coverM/EN/AQBP01.jpg");
    EXPECT_EQ(library::gametdb_covers("switch", "AQB7A")[0],
              "https://art.gametdb.com/switch/coverHQ/US/AQB7A.jpg");

    const char *xml = R"xml(<datafile>
        <game name="Moon Garden: Second Bloom (EN,FR)"><id>AMG2A</id><region>ALL</region></game>
        <game name="Moon Garden: Second Bloom (JA)"><id>AMG2B</id></game>
        <game name="Quiet Bay &amp; Friends (EN)"><id>AQBFA</id></game>
        <game name="No Id Here (EN)"></game>
    </datafile>)xml";
    const std::string index = library::gametdb_title_index(xml);
    EXPECT_EQ(library::gametdb_id(index, "Moon Garden Second Bloom"), "AMG2A"); // the first
    EXPECT_EQ(library::gametdb_id(index, "Quiet Bay & Friends"), "AQBFA");
    EXPECT_EQ(library::gametdb_id(index, "No Id Here"), "");
    EXPECT_EQ(library::gametdb_id(index, "Elsewhere"), "");
}

TEST(ArtFetcher, DownloadsGameTdbCoversByIdAndName)
{
    const std::string dir = ::testing::TempDir() + "hui-art-gametdb";
    const std::string wiiu = dir + "/art/Nintendo - Wii U";
    const std::string nx = dir + "/art/Nintendo - Switch";
    for (const std::string &file : {wiiu + "/Quiet Bay.png", nx + "/Moon Garden.png"})
        std::remove(file.c_str());
    ASSERT_TRUE(orion::make_directories(nx));
    // The Switch index, as a run before left it.
    ASSERT_TRUE(save::write_atomic(nx + "/gametdb-switch.tsv", "moongarden\tAMG7A\n").empty());
    const std::string jpeg = std::string("\xff\xd8\xff\xe0", 4) + "cover";
    FakeServer server;
    server.pages["https://art.gametdb.com/wiiu/coverHQ/US/AQBE01.jpg"] = jpeg;
    server.pages["https://art.gametdb.com/switch/coverM/US/AMG7A.jpg"] = jpeg; // no HQ one
    orion::ArtFetcher art(server.get());
    orion::ArtJob bay;
    bay.id = "rom-00000000000b";
    bay.system = "Nintendo - Wii U";
    bay.rom_file = "title.tmd";
    bay.serial = "AQBE01";
    bay.target = wiiu + "/Quiet Bay.png";
    orion::ArtJob moon;
    moon.id = "rom-00000000000c";
    moon.system = "Nintendo - Switch";
    moon.rom_file = "Moon Garden [0100AAAA00000000][v0][US].xci";
    moon.name = "Moon Garden";
    moon.target = nx + "/Moon Garden.png";
    orion::ArtJob unknown = moon;
    unknown.id = "rom-00000000000d";
    unknown.name = "Not In The List";
    ASSERT_TRUE(art.start({bay, moon, unknown}, ""));
    art.wait();
    const orion::ArtFetcher::Progress done = art.progress();
    EXPECT_EQ(done.found, 2);
    EXPECT_EQ(done.missing, 1);
    std::string bytes;
    ASSERT_TRUE(save::read_file(wiiu + "/Quiet Bay.png", &bytes));
    EXPECT_EQ(bytes, jpeg);
    // No libretro index was asked for: GameTDB has these.
    EXPECT_TRUE(std::none_of(server.asked.begin(), server.asked.end(), [](const std::string &url)
                             { return url.find("libretro") != std::string::npos; }));
}

} // namespace
} // namespace hui
