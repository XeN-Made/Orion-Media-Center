// ORION Media Center - Tests: the checks a console run makes on itself.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "core/sha256.hpp"
#include "orion/console_share.hpp"
#include "orion/self_test.hpp"
#include "orion/shares.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace
{

using hui::orion::CheckResult;
using Outcome = CheckResult::Outcome;

// save::ensure_directory makes one level; these make a path.
bool make_path(const std::string &path)
{
    bool made = true;
    for (std::size_t at = path.find('/', 1); made; at = path.find('/', at + 1))
    {
        made = hui::save::ensure_directory(path.substr(0, at));
        if (at == std::string::npos)
            break;
    }
    return made;
}

const CheckResult *find(const std::vector<CheckResult> &results, const std::string &name)
{
    for (const CheckResult &result : results)
    {
        if (result.name == name)
            return &result;
    }
    return nullptr;
}

struct SelfTestFixture : ::testing::Test
{
    std::string dir;
    hui::orion::SelfTestInput in;

    void SetUp() override
    {
        dir = ::testing::TempDir() + "orion-self-test";
        hui::save::remove_tree(dir);
        ASSERT_TRUE(make_path(dir + "/state"));
        ASSERT_TRUE(make_path(dir + "/app/assets/payloads"));
        ASSERT_TRUE(hui::save::write_atomic(dir + "/app/assets/payloads/a.elf", "elf").empty());
        in.state_dir = dir + "/state";
        in.app_dir = dir + "/app";
        in.payloads = {"a.elf"};
        in.port_open = [](int port) { return port == 9021; };
    }
};

// A console where nothing is set up: /data writes, the payloads and the
// loader are there, the library is empty, drives and sharing are skipped.
TEST_F(SelfTestFixture, ReportsAPlainConsole)
{
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);
    ASSERT_NE(find(results, "data-writable"), nullptr);
    EXPECT_EQ(find(results, "data-writable")->outcome, Outcome::pass);
    EXPECT_EQ(find(results, "payloads")->outcome, Outcome::pass);
    EXPECT_EQ(find(results, "elf-loader")->outcome, Outcome::pass);
    EXPECT_EQ(find(results, "library")->outcome, Outcome::fail);
    EXPECT_EQ(find(results, "drives")->outcome, Outcome::skip);
    EXPECT_EQ(find(results, "network-drives")->outcome, Outcome::skip);
    EXPECT_EQ(find(results, "console-share")->outcome, Outcome::skip);
}

// What is missing is named; a closed loader port fails.
TEST_F(SelfTestFixture, NamesWhatIsMissing)
{
    in.payloads.push_back("b.elf");
    in.port_open = [](int) { return false; };
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);
    EXPECT_EQ(find(results, "payloads")->outcome, Outcome::fail);
    EXPECT_EQ(find(results, "payloads")->detail, "missing b.elf");
    EXPECT_EQ(find(results, "elf-loader")->outcome, Outcome::fail);
}

// Titles are counted by kind; a drive with installed games the library
// did not pick up fails.
TEST_F(SelfTestFixture, CountsTheLibraryAndItsDrives)
{
    hui::library::Title game;
    game.path = dir + "/ext0/user/app/PPSA00001";
    hui::library::Title brew;
    brew.homebrew = true;
    brew.path = "/data/homebrew/BREW00001";
    hui::library::Title rom;
    rom.system = "Nintendo - Game Boy Advance";
    in.titles = {game, brew, rom};
    ASSERT_TRUE(make_path(dir + "/ext0/user/app/PPSA00001"));
    ASSERT_TRUE(make_path(dir + "/usb0/user/app/PPSA00002"));
    in.drive_roots = {dir + "/ext0", dir + "/usb0", dir + "/usb1"};
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);
    EXPECT_EQ(find(results, "library")->outcome, Outcome::pass);
    EXPECT_EQ(find(results, "library")->detail,
              "3 titles: 1 games, 1 homebrew, 1 emulated, 0 payloads");
    EXPECT_EQ(find(results, "drives")->outcome, Outcome::fail);
    EXPECT_NE(find(results, "drives")->detail.find("usb0 has 1 in user/app"), std::string::npos);

    hui::save::remove_tree(dir + "/usb0");
    const std::vector<CheckResult> again = hui::orion::run_self_test(in);
    EXPECT_EQ(find(again, "drives")->outcome, Outcome::pass);
    EXPECT_EQ(find(again, "drives")->detail, dir + "/ext0 1 titles");
}

// Each saved drive is tried with its own sign-in; a refusal says why.
TEST_F(SelfTestFixture, TriesEveryNetworkDrive)
{
    std::vector<hui::orion::Share> shares;
    hui::orion::Share open;
    ASSERT_TRUE(hui::orion::parse_share_address("\\\\nas\\games", &open));
    hui::orion::add_share(shares, open);
    hui::orion::Share locked;
    ASSERT_TRUE(hui::orion::parse_share_address("\\\\pc\\private", &locked));
    hui::orion::add_share(shares, locked);
    ASSERT_TRUE(hui::orion::save_shares(in.state_dir, shares));
    in.list = [](const hui::sys::SmbLogin &login, const std::string &, int, std::size_t,
                 std::vector<hui::sys::SmbEntry> *entries, std::string *error)
    {
        if (login.server == "pc")
        {
            *error = "STATUS_LOGON_FAILURE";
            return false;
        }
        entries->resize(3);
        return true;
    };
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);
    ASSERT_NE(find(results, "network-drive \\\\nas\\games"), nullptr);
    EXPECT_EQ(find(results, "network-drive \\\\nas\\games")->outcome, Outcome::pass);
    EXPECT_EQ(find(results, "network-drive \\\\nas\\games")->detail, "signed in, 3 entries");
    const CheckResult *refused = find(results, "network-drive \\\\pc\\private");
    ASSERT_NE(refused, nullptr);
    EXPECT_EQ(refused->outcome, Outcome::fail);
    EXPECT_NE(refused->detail.find("password is wrong"), std::string::npos);
}

// The console's own share is signed in to over the loopback, with the
// user and password Settings saved; the payload's complaint fails it.
TEST_F(SelfTestFixture, SignsInToTheConsolesOwnShare)
{
    hui::orion::ConsoleShare share;
    share.on = true;
    share.password = "secret";
    ASSERT_TRUE(hui::orion::write_console_share(in.state_dir, share));
    hui::save::write_atomic(in.state_dir + "/smb-share-status.txt", "busy\t\n");
    EXPECT_EQ(find(hui::orion::run_self_test(in), "console-share")->outcome, Outcome::fail);

    hui::save::write_atomic(in.state_dir + "/smb-share-status.txt", "on\tdata, usb0\n");
    hui::sys::SmbLogin seen;
    in.list = [&seen](const hui::sys::SmbLogin &login, const std::string &, int, std::size_t,
                      std::vector<hui::sys::SmbEntry> *entries, std::string *)
    {
        if (seen.server.empty())
            seen = login; // the first sign-in: console-share's own
        entries->resize(5);
        return true;
    };
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);
    const CheckResult *result = find(results, "console-share");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->outcome, Outcome::pass);
    EXPECT_EQ(seen.server, "127.0.0.1");
    EXPECT_EQ(seen.share, "data");
    EXPECT_EQ(seen.user, "ps5");
    EXPECT_EQ(seen.password, "secret");
    EXPECT_NE(result->detail.find("shared: data, usb0"), std::string::npos);
}

// The test games are installed off the console's own share with the
// installer's code; here a copier stands in for SMB and the archive reader.
TEST_F(SelfTestFixture, InstallsEachTestGameAndComparesIt)
{
    hui::orion::ConsoleShare share;
    share.on = true;
    share.password = "secret";
    ASSERT_TRUE(hui::orion::write_console_share(in.state_dir, share));
    const std::string fixtures = in.state_dir + "/selftest/fixtures";
    ASSERT_TRUE(make_path(fixtures));
    hui::save::write_atomic(fixtures + "/cases.txt", "ok\tgame.zip\tgame.sha256\n"
                                                     "twice\tsplit.part1.rar\tgame.sha256\n"
                                                     "ok\tbroken.7z\tgame.sha256\n"
                                                     "stop\tbig.7z\tgame.sha256\n"
                                                     "ok\tcheck.ffpkg\timage.sha256\n");
    hui::save::write_atomic(fixtures + "/game.sha256",
                            hui::sha256_hex("{}") + "  sce_sys/param.json\n" +
                                hui::sha256_hex("data") + "  data/a.bin\n");
    hui::save::write_atomic(fixtures + "/image.sha256",
                            hui::sha256_hex("image") + "  check.ffpkg\n");
    in.list = [](const hui::sys::SmbLogin &, const std::string &folder, int, std::size_t,
                 std::vector<hui::sys::SmbEntry> *entries, std::string *error)
    {
        if (folder != "orion/selftest/fixtures")
        {
            *error = "STATUS_OBJECT_NAME_NOT_FOUND"; // the other checks' asks
            return false;
        }
        for (const char *name : {"game.zip", "split.part1.rar", "split.part2.rar", "broken.7z",
                                 "big.7z", "check.ffpkg"})
        {
            hui::sys::SmbEntry entry;
            entry.path = name;
            entries->push_back(entry);
        }
        return true;
    };
    std::vector<std::vector<std::string>> asked;
    in.copy = [&asked](const hui::sys::GameSource &source, const std::string &into,
                       hui::sys::CopyProgress *progress, std::string *)
    {
        asked.push_back(source.paths);
        const std::string &first = source.paths.front();
        progress->done = 1000000;
        if (first.find("check.ffpkg") != std::string::npos)
            return hui::save::write_atomic(into + "/check.ffpkg", "image").empty();
        if (first.find("big.7z") != std::string::npos)
        {
            while (!progress->cancel.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return false;
        }
        const std::string game = into + "/Check Game";
        make_path(game + "/sce_sys");
        make_path(game + "/data");
        hui::save::write_atomic(game + "/sce_sys/param.json", "{}");
        hui::save::write_atomic(game + "/data/a.bin",
                                first.find("broken") != std::string::npos ? "dat" : "data");
        return true;
    };
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);

    const CheckResult *zip = find(results, "install ok game.zip");
    ASSERT_NE(zip, nullptr);
    EXPECT_EQ(zip->outcome, Outcome::pass) << zip->detail;
    EXPECT_EQ(zip->detail.rfind("2 files match, ", 0), 0u) << zip->detail;
    const CheckResult *split = find(results, "install twice split.part1.rar");
    ASSERT_NE(split, nullptr);
    EXPECT_EQ(split->outcome, Outcome::pass) << split->detail;
    EXPECT_NE(split->detail.find("(2 volumes)"), std::string::npos);
    EXPECT_NE(split->detail.find("again: already there"), std::string::npos);
    const CheckResult *broken = find(results, "install ok broken.7z");
    ASSERT_NE(broken, nullptr);
    EXPECT_EQ(broken->outcome, Outcome::fail);
    EXPECT_EQ(broken->detail, "data/a.bin differs");
    const CheckResult *stop = find(results, "install stop big.7z");
    ASSERT_NE(stop, nullptr);
    EXPECT_EQ(stop->outcome, Outcome::pass) << stop->detail;
    const CheckResult *image = find(results, "install ok check.ffpkg");
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->outcome, Outcome::pass) << image->detail;

    ASSERT_EQ(asked.size(), 6u); // the split archive twice
    EXPECT_EQ(asked[1], (std::vector<std::string>{"orion/selftest/fixtures/split.part1.rar",
                                                  "orion/selftest/fixtures/split.part2.rar"}));
    // Every install is removed again.
    EXPECT_TRUE(hui::save::list_directory(in.state_dir + "/selftest/installed").empty());
}

// Without sharing there is no share to install from: skipped, and said why.
TEST_F(SelfTestFixture, SkipsInstallsWithoutSharing)
{
    ASSERT_TRUE(make_path(in.state_dir + "/selftest/fixtures"));
    hui::save::write_atomic(in.state_dir + "/selftest/fixtures/cases.txt", "ok\tgame.zip\tx\n");
    in.copy = [](const hui::sys::GameSource &, const std::string &, hui::sys::CopyProgress *,
                 std::string *) { return false; };
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);
    ASSERT_NE(find(results, "installs"), nullptr);
    EXPECT_EQ(find(results, "installs")->outcome, Outcome::skip);
    // No fixtures: no install line at all.
    hui::save::remove_tree(in.state_dir + "/selftest/fixtures");
    EXPECT_EQ(find(hui::orion::run_self_test(in), "installs"), nullptr);
}

// The search must find the machines the saved drives are on.
TEST_F(SelfTestFixture, SearchesTheNetwork)
{
    std::vector<hui::orion::Share> shares;
    hui::orion::Share saved;
    ASSERT_TRUE(hui::orion::parse_share_address("\\\\192.168.1.20\\games", &saved));
    hui::orion::add_share(shares, saved);
    ASSERT_TRUE(hui::orion::save_shares(in.state_dir, shares));
    in.list = [](const hui::sys::SmbLogin &, const std::string &, int, std::size_t,
                 std::vector<hui::sys::SmbEntry> *, std::string *) { return true; };
    in.own_address = "192.168.1.30";
    std::string asked;
    in.find_servers = [&asked](const std::string &own)
    {
        asked = own;
        hui::sys::SmbServer server;
        server.address = "192.168.1.20";
        server.name = "MEDIA-PC";
        return std::vector<hui::sys::SmbServer>{server};
    };
    const std::vector<CheckResult> found = hui::orion::run_self_test(in);
    EXPECT_EQ(asked, "192.168.1.30");
    EXPECT_EQ(find(found, "network-search")->outcome, Outcome::pass);
    EXPECT_EQ(find(found, "network-search")->detail, "1 found: 192.168.1.20 MEDIA-PC");

    in.find_servers = [](const std::string &) { return std::vector<hui::sys::SmbServer>{}; };
    EXPECT_EQ(find(hui::orion::run_self_test(in), "network-search")->outcome, Outcome::fail);
}

// A wrong password and a missing share must each come back with the hint
// a player is shown; one that lets them in fails.
TEST_F(SelfTestFixture, ChecksWhatABadDriveSays)
{
    hui::orion::ConsoleShare share;
    share.on = true;
    share.password = "secret";
    ASSERT_TRUE(hui::orion::write_console_share(in.state_dir, share));
    hui::save::write_atomic(in.state_dir + "/smb-share-status.txt", "on\tdata\n");
    bool strict = true;
    in.list = [&strict](const hui::sys::SmbLogin &login, const std::string &, int, std::size_t,
                        std::vector<hui::sys::SmbEntry> *, std::string *error)
    {
        if (strict && login.password != "secret")
        {
            *error = "Session setup failed with (0xc000006d) STATUS_LOGON_FAILURE";
            return false;
        }
        if (login.share != "data")
        {
            *error = "Tree Connect failed with (0xc00000cc) STATUS_BAD_NETWORK_NAME";
            return false;
        }
        return true;
    };
    in.list_shares = [](const hui::sys::SmbLogin &, std::vector<std::string> *names, std::string *)
    {
        *names = {"data", "usb0"};
        return true;
    };
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);
    ASSERT_NE(find(results, "drive-errors"), nullptr);
    EXPECT_EQ(find(results, "drive-errors")->outcome, Outcome::pass)
        << find(results, "drive-errors")->detail;
    EXPECT_EQ(find(results, "share-list")->outcome, Outcome::pass);
    EXPECT_EQ(find(results, "share-list")->detail, "the console offers data, usb0");

    strict = false;
    const std::vector<CheckResult> lax = hui::orion::run_self_test(in);
    EXPECT_EQ(find(lax, "drive-errors")->outcome, Outcome::fail);
    EXPECT_EQ(find(lax, "drive-errors")->detail, "a wrong password was let in");
}

// Pictures decode as a background would, and a broken one is refused.
TEST_F(SelfTestFixture, DecodesTestPictures)
{
    const std::string dir = in.state_dir + "/selftest/pictures";
    ASSERT_TRUE(make_path(dir));
    std::string cover;
    ASSERT_TRUE(hui::save::read_file(
        std::string(HUI_SOURCE_DIR) + "/tests/fixtures/images/cover.jpg", &cover));
    hui::save::write_atomic(dir + "/cover.jpg", cover);
    hui::save::write_atomic(dir + "/broken.png", "not a picture");
    hui::save::write_atomic(dir + "/cases.txt",
                            "ok\tcover.jpg\nrefuse\tbroken.png\nok\tbroken.png\n");
    const std::vector<CheckResult> results = hui::orion::run_self_test(in);
    std::vector<const CheckResult *> pictures;
    for (const CheckResult &result : results)
    {
        if (result.name.rfind("picture ", 0) == 0)
            pictures.push_back(&result);
    }
    ASSERT_EQ(pictures.size(), 3u);
    EXPECT_EQ(pictures[0]->outcome, Outcome::pass) << pictures[0]->detail;
    EXPECT_EQ(pictures[1]->outcome, Outcome::pass) << pictures[1]->detail;
    EXPECT_EQ(pictures[2]->outcome, Outcome::fail);
    EXPECT_EQ(pictures[2]->detail.rfind("does not decode", 0), 0u) << pictures[2]->detail;
}

TEST(OrionSelfTest, WritesALineACheck)
{
    std::vector<CheckResult> results(3);
    results[0].name = "a";
    results[0].detail = "fine";
    results[1].name = "b";
    results[1].outcome = Outcome::fail;
    results[1].detail = "broken";
    results[2].name = "c";
    results[2].outcome = Outcome::skip;
    results[2].detail = "none";
    EXPECT_EQ(hui::orion::self_test_report("42", results),
              "token 42\nPASS a: fine\nFAIL b: broken\nSKIP c: none\n"
              "passed 1 failed 1 skipped 1\n");
}

// The thread version hands over the same results once.
TEST_F(SelfTestFixture, RunsOnAThread)
{
    hui::orion::SelfTest test;
    ASSERT_TRUE(test.start(in));
    test.wait();
    std::vector<CheckResult> results;
    ASSERT_TRUE(test.take_finished(&results));
    EXPECT_FALSE(results.empty());
    EXPECT_FALSE(test.take_finished(&results));
}

} // namespace
