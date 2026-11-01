// ORION Media Center - Tests: the checks a console run makes on itself.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "orion/console_share.hpp"
#include "orion/self_test.hpp"
#include "orion/shares.hpp"

#include <gtest/gtest.h>

#include <string>
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
        seen = login;
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
