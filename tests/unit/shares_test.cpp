// ORION Media Center - Tests: network drives (SMB shares) and the games on them.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/shares.hpp"

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

std::string fresh_dir(const std::string &name)
{
    const std::string dir = ::testing::TempDir() + name;
    save::ensure_directory(dir);
    std::remove((dir + "/shares.json").c_str());
    std::remove((dir + "/share-games.tsv").c_str());
    std::remove((dir + "/keys.txt").c_str());
    return dir;
}

TEST(Shares, ReadAddressesAsWindowsAndNasesWriteThem)
{
    orion::Share share;
    ASSERT_TRUE(orion::parse_share_address("\\\\nas\\games\\roms\\", &share));
    EXPECT_EQ(share.server, "nas");
    EXPECT_EQ(share.share, "games");
    EXPECT_EQ(share.folder, "roms");
    EXPECT_EQ(orion::share_address(share), "\\\\nas\\games\\roms");
    ASSERT_TRUE(orion::parse_share_address(" smb://192.168.1.20/Games/Retro/Sets ", &share));
    EXPECT_EQ(share.server, "192.168.1.20");
    EXPECT_EQ(share.share, "Games");
    EXPECT_EQ(share.folder, "Retro/Sets");
    ASSERT_TRUE(orion::parse_share_address("SMB://nas/games", &share));
    EXPECT_EQ(share.folder, "");
    ASSERT_TRUE(orion::parse_share_address("//nas//games", &share));
    EXPECT_EQ(share.share, "games");
    EXPECT_FALSE(orion::parse_share_address("nas", &share));
    EXPECT_FALSE(orion::parse_share_address("", &share));
    EXPECT_FALSE(orion::parse_share_address("\\\\nas\\games\\..\\secret", &share));
}

TEST(Shares, AreKeptWithoutTheirPasswords)
{
    const std::string dir = fresh_dir("orion-shares");
    std::vector<orion::Share> shares;
    orion::Share nas;
    ASSERT_TRUE(orion::parse_share_address("\\\\nas\\games\\roms", &nas));
    nas.user = "player";
    EXPECT_EQ(orion::add_share(shares, nas).id, "s1");
    EXPECT_EQ(orion::add_share(shares, nas).id, "s2");
    ASSERT_TRUE(orion::save_shares(dir, shares));
    ASSERT_TRUE(orion::write_service_key(dir, orion::share_password_key("s1"), "hunter2").empty());
    const std::vector<orion::Share> back = orion::load_shares(dir);
    ASSERT_EQ(back.size(), 2u);
    EXPECT_EQ(back[0].folder, "roms");
    EXPECT_EQ(back[0].user, "player");
    std::string json;
    ASSERT_TRUE(save::read_file(dir + "/shares.json", &json));
    EXPECT_EQ(json.find("hunter2"), std::string::npos);
    const sys::SmbLogin login = orion::share_login(back[0], dir);
    EXPECT_EQ(login.server, "nas");
    EXPECT_EQ(login.share, "games");
    EXPECT_EQ(login.password, "hunter2");
    EXPECT_TRUE(orion::share_login(back[1], dir).password.empty());
}

TEST(Shares, TheirGamesListOnTheSystemsTheirFoldersName)
{
    const std::vector<orion::ShareFile> files = {
        {"s1", "gba/Moon Garden (USA).gba", 4194304},
        {"s1", "snes/Night Drive.sfc", 1048576},
        {"s2", "Comet Run.nds", 8388608}, // at the top of a folder named for its system
        {"s9", "gba/Gone.gba", 1},        // a share no longer set up
    };
    const std::vector<orion::ShareFile> back =
        orion::read_share_files(orion::share_files_text(files));
    ASSERT_EQ(back.size(), files.size());
    EXPECT_EQ(back[0].path, files[0].path);
    EXPECT_EQ(back[0].size, files[0].size);

    std::vector<orion::Share> shares(2);
    shares[0].id = "s1";
    shares[0].folder = "roms";
    shares[1].id = "s2";
    shares[1].folder = "handhelds/nds";
    const std::vector<library::RemoteGame> games =
        orion::share_remote_games(back, shares, "/data/orion");
    ASSERT_EQ(games.size(), 3u);
    EXPECT_EQ(games[0].system_folder, "gba");
    EXPECT_EQ(games[0].name, "Moon Garden");
    EXPECT_EQ(games[0].path, "/data/orion/shares/s1/gba/Moon Garden (USA).gba");
    EXPECT_EQ(games[0].remote, "smb://s1/roms/gba/Moon Garden (USA).gba");
    EXPECT_TRUE(games[0].filtered);
    EXPECT_EQ(games[2].system_folder, "nds");
    EXPECT_EQ(games[2].remote, "smb://s2/handhelds/nds/Comet Run.nds");
}

TEST(Shares, AScanListsEveryShareAndKeepsOneAsleep)
{
    const std::string dir = fresh_dir("orion-shares-scan");
    std::vector<orion::Share> shares(2);
    shares[0].id = "s1";
    shares[0].server = "nas";
    shares[0].share = "games";
    shares[1].id = "s2";
    shares[1].server = "asleep";
    shares[1].share = "games";
    // What s2 had last time.
    ASSERT_TRUE(save::write_atomic(dir + "/share-games.tsv",
                                   orion::share_files_text({{"s2", "gba/Old.gba", 7}}))
                    .empty());
    int asked = 0;
    orion::ShareScan scan(
        [&](const sys::SmbLogin &login, const std::string &folder, int depth, std::size_t,
            std::vector<sys::SmbEntry> *entries, std::string *error)
        {
            ++asked;
            EXPECT_EQ(folder, "");
            EXPECT_EQ(depth, 1);
            if (login.server == "asleep")
            {
                *error = "no answer";
                return false;
            }
            entries->push_back({"gba", true, 0});
            entries->push_back({"gba/Moon Garden.gba", false, 42});
            return true;
        });
    ASSERT_TRUE(scan.start(shares, dir));
    scan.wait();
    orion::ShareScan::Result result;
    ASSERT_TRUE(scan.take_finished(&result));
    EXPECT_EQ(asked, 2);
    EXPECT_EQ(result.shares, 1);
    EXPECT_EQ(result.files, 1);
    EXPECT_EQ(result.error, "\\\\asleep\\games: no answer");
    std::string text;
    ASSERT_TRUE(save::read_file(dir + "/share-games.tsv", &text));
    const std::vector<orion::ShareFile> files = orion::read_share_files(text);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].path, "gba/Moon Garden.gba");
    EXPECT_EQ(files[1].share_id, "s2"); // kept from before
    EXPECT_FALSE(scan.take_finished(&result));
}

// Machines found, then one's folders, each on its own thread; a refused
// sign-in comes back as what the server said, with a hint in plain words.
TEST(Shares, AreFoundOnTheNetworkThenTheirFoldersListed)
{
    orion::DriveFinder finder(
        [](const std::string &own, std::string *trace)
        {
            EXPECT_EQ(own, "192.168.1.30");
            *trace = "one open";
            return std::vector<hui::sys::SmbServer>{{"192.168.1.20", "MEDIA-PC"}};
        },
        [](const hui::sys::SmbLogin &login, std::vector<std::string> *shares, std::string *error)
        {
            if (login.password != "right")
            {
                *error = "Session setup failed (STATUS_LOGON_FAILURE)";
                return false;
            }
            *shares = {"Games", "Music"};
            return true;
        });
    orion::DriveFinder::Result result;
    ASSERT_TRUE(finder.find("192.168.1.30"));
    finder.wait();
    ASSERT_TRUE(finder.take_finished(&result));
    EXPECT_TRUE(result.servers_asked);
    ASSERT_EQ(result.servers.size(), 1u);
    EXPECT_EQ(result.servers[0].name, "MEDIA-PC");
    EXPECT_EQ(result.trace, "one open");

    hui::sys::SmbLogin login;
    login.server = "192.168.1.20";
    login.password = "wrong";
    ASSERT_TRUE(finder.list(login));
    finder.wait();
    ASSERT_TRUE(finder.take_finished(&result));
    EXPECT_FALSE(result.servers_asked);
    EXPECT_NE(std::string(orion::share_hint(result.error)).find("password is wrong"),
              std::string::npos);
    login.password = "right";
    ASSERT_TRUE(finder.list(login));
    finder.wait();
    ASSERT_TRUE(finder.take_finished(&result));
    EXPECT_EQ(result.shares, (std::vector<std::string>{"Games", "Music"}));
    EXPECT_TRUE(std::string(orion::share_hint("no answer")).empty());
}

// The log keeps its newest lines and never grows past a few dozen KB.
TEST(Shares, LogWhatHappensAndStaySmall)
{
    const std::string dir = ::testing::TempDir() + "orion-shares-log";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/shares.txt").c_str());
    for (int i = 0; i < 2000; ++i)
        orion::log_share(dir, "try " + std::to_string(i) + ": STATUS_LOGON_FAILURE");
    std::string text;
    ASSERT_TRUE(hui::save::read_file(dir + "/shares.txt", &text, 1u << 20));
    EXPECT_LE(text.size(), 66u * 1024u);
    EXPECT_NE(text.find("try 1999: STATUS_LOGON_FAILURE\n"), std::string::npos);
    EXPECT_EQ(text.find("try 0:"), std::string::npos);
}

// "PC\\user" signs in to that machine's account.
TEST(Shares, TakeAUserWithItsMachine)
{
    orion::Share share;
    orion::set_share_user(&share, " MEDIA-PC\\orion ");
    EXPECT_EQ(share.domain, "MEDIA-PC");
    EXPECT_EQ(share.user, "orion");
    EXPECT_EQ(orion::share_login_with(share, "pw").domain, "MEDIA-PC");
    orion::set_share_user(&share, "orion");
    EXPECT_TRUE(share.domain.empty());
    EXPECT_EQ(share.user, "orion");
}

TEST(Shares, ADownloadNamesWhatIsWrong)
{
    const std::string dir = fresh_dir("orion-shares-download");
    std::string error;
    EXPECT_FALSE(orion::download_share_file(dir, "http://x/y", dir + "/a", &error));
    EXPECT_EQ(error, "not a network drive's file");
    EXPECT_FALSE(orion::download_share_file(dir, "smb://s1/gba/a.gba", dir + "/a", &error));
    EXPECT_EQ(error, "that network drive is no longer set up");
}

} // namespace
} // namespace hui
