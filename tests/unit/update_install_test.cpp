// ORION Media Center - Tests: SHA-256, ZIP reading, and staging an update from a release.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "core/sha256.hpp"
#include "core/zip.hpp"
#include "orion/updates.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace hui
{
namespace
{

const std::string kFixtures = std::string(HUI_SOURCE_DIR) + "/tests/fixtures/update";

std::string fixture(const std::string &name)
{
    std::string bytes;
    EXPECT_TRUE(save::read_file(kFixtures + "/" + name, &bytes, 1u << 20)) << name;
    return bytes;
}

std::string fresh_dir(const char *name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

} // namespace

TEST(Sha256, MatchesTheStandardVectors)
{
    EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256_hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // Two blocks' worth, the padding spilling into a block of its own.
    EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // Fed in pieces, the same digest.
    Sha256 pieces;
    pieces.update("ab");
    pieces.update("c");
    EXPECT_EQ(pieces.hex(), sha256_hex("abc"));
    const std::string million(1000000, 'a');
    EXPECT_EQ(sha256_hex(million),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Zip, ReadsStoredAndDeflatedEntriesAndSkipsFolders)
{
    std::vector<std::string> names;
    std::string eboot;
    std::string error;
    ASSERT_TRUE(unzip(
        fixture("PPSA99061.zip"),
        [&](const std::string &name, std::string_view data)
        {
            names.push_back(name);
            if (name == "PPSA99061/eboot.bin")
                eboot = std::string(data);
            return true;
        },
        &error))
        << error;
    EXPECT_EQ(names, (std::vector<std::string>{"PPSA99061/eboot.bin",
                                               "PPSA99061/sce_sys/param.json", "SHA256SUMS"}));
    EXPECT_EQ(eboot.size(), 6800u); // inflated
    EXPECT_EQ(eboot.substr(0, 17), "ORION test eboot ");

    EXPECT_FALSE(
        unzip("not a zip", [](const std::string &, std::string_view) { return true; }, &error));
    EXPECT_EQ(error, "not a ZIP archive");
    std::string cut = fixture("PPSA99061.zip");
    cut.resize(cut.size() - 30);
    EXPECT_FALSE(unzip(cut, [](const std::string &, std::string_view) { return true; }, &error));
}

TEST(OrionUpdates, ReadsTheSumOfTheZip)
{
    const std::string sums = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789ABCDEF  "
                             "PPSA99061.zip\r\n"
                             "1111111111111111111111111111111111111111111111111111111111111111 "
                             "*other.zip\n";
    EXPECT_EQ(orion::sum_for(sums, "PPSA99061.zip"),
              "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    EXPECT_EQ(orion::sum_for(sums, "other.zip"), std::string(64, '1'));
    EXPECT_EQ(orion::sum_for(sums, "missing.zip"), "");
    // The fixture's own sums match the fixture.
    EXPECT_EQ(orion::sum_for(fixture("SHA256SUMS"), "PPSA99061.zip"),
              sha256_hex(fixture("PPSA99061.zip")));
}

TEST(OrionUpdates, StagesTheTitleFolderAndListsItsFiles)
{
    const std::string stage = fresh_dir("orion-update-stage") + "/stage";
    std::string error;
    ASSERT_TRUE(orion::stage_update(fixture("PPSA99061.zip"), "PPSA99061", stage, &error)) << error;
    std::string list;
    ASSERT_TRUE(save::read_file(stage + "/files.txt", &list));
    EXPECT_EQ(list, "eboot.bin\nsce_sys/param.json\n"); // SHA256SUMS is not the title's
    std::string param;
    ASSERT_TRUE(save::read_file(stage + "/sce_sys/param.json", &param));
    EXPECT_NE(param.find("01.001.001"), std::string::npos);

    // Another title's release, or a path that climbs out, is refused.
    EXPECT_FALSE(orion::stage_update(fixture("PPSA99061.zip"), "PPSA90000", stage, &error));
    EXPECT_FALSE(orion::stage_update(fixture("unsafe.zip"), "PPSA99061", stage, &error));
    EXPECT_NE(error.find("unsafe"), std::string::npos) << error;
}

TEST(OrionUpdates, AsksTheReleaseATestFileNames)
{
    const std::string state = fresh_dir("orion-update-feed");
    EXPECT_EQ(orion::update_source(state), orion::kLatestRelease);
    EXPECT_EQ(orion::update_source(""), orion::kLatestRelease);

    const std::string tag = std::string(orion::kReleasesApi) + "tags/01.102.900";
    ASSERT_TRUE(save::write_atomic(state + "/update-feed.txt", tag + "\r\n").empty());
    EXPECT_EQ(orion::update_source(state), tag);

    // Only this repository's releases.
    for (const char *other : {"https://api.github.com/repos/someone/else/releases/latest",
                              "https://example.com/releases/", "", "not a url"})
    {
        ASSERT_TRUE(save::write_atomic(state + "/update-feed.txt", other).empty());
        EXPECT_EQ(orion::update_source(state), orion::kLatestRelease) << other;
    }
    ASSERT_TRUE(save::write_atomic(state + "/update-feed.txt", tag + "?x=1").empty());
    EXPECT_EQ(orion::update_source(state), orion::kLatestRelease);
}

TEST(OrionUpdates, StagesTheNextTitleIdsRelease)
{
    // ORION under PPSA90000 told its next title ID is PPSA99061: that
    // release is staged; without being told, it is refused (as above).
    const std::string stage = fresh_dir("orion-update-next") + "/stage";
    std::string error;
    ASSERT_TRUE(
        orion::stage_update(fixture("PPSA99061.zip"), "PPSA90000", stage, &error, "PPSA99061"))
        << error;
    std::string list;
    ASSERT_TRUE(save::read_file(stage + "/files.txt", &list));
    EXPECT_EQ(list, "eboot.bin\nsce_sys/param.json\n");
    EXPECT_FALSE(
        orion::stage_update(fixture("PPSA99061.zip"), "PPSA90000", stage, &error, "PPSA90001"));
}

TEST(OrionUpdates, DownloadsChecksAndStagesARelease)
{
    const std::string zip = fixture("PPSA99061.zip");
    const std::string sums = fixture("SHA256SUMS");
    orion::Release release;
    release.version = "01.001.001";
    release.zip = "https://example.invalid/download/PPSA99061.zip";
    release.sums = "https://example.invalid/download/SHA256SUMS";
    const orion::HttpGet get = [&](const std::string &, std::size_t, std::string *body,
                                   std::string *) -> bool
    {
        *body = sums;
        return true;
    };
    const auto serve = [&](std::string served_zip)
    {
        return [&, served_zip](const std::string &url, const std::string &path, std::size_t,
                               std::string *error) -> bool
        {
            EXPECT_EQ(url, release.zip);
            *error = save::write_atomic(path, served_zip);
            return error->empty();
        };
    };
    const auto finish = [](orion::UpdateInstaller &installer)
    {
        for (int i = 0; i < 400 && !installer.take_finished(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    };

    const std::string dir = fresh_dir("orion-update-install");
    orion::UpdateInstaller good(get, serve(zip));
    ASSERT_TRUE(good.start(release, "PPSA99061", dir));
    finish(good);
    EXPECT_EQ(good.state(), orion::UpdateInstaller::State::ready) << good.error();
    std::string version;
    ASSERT_TRUE(save::read_file(dir + "/stage/version.txt", &version));
    EXPECT_EQ(version, "01.001.001\n");

    // Downloaded and unpacked before, never installed: ready again with no
    // download. Another release's stage is not this one.
    orion::UpdateInstaller again(
        get,
        [](const std::string &, const std::string &, std::size_t, std::string *) -> bool
        {
            ADD_FAILURE() << "downloaded again";
            return false;
        });
    ASSERT_TRUE(again.resume(release, dir));
    EXPECT_TRUE(again.take_finished());
    EXPECT_EQ(again.state(), orion::UpdateInstaller::State::ready);
    orion::Release other = release;
    other.version = "01.001.002";
    EXPECT_FALSE(again.resume(other, dir));

    // A download that does not match its published sum installs nothing.
    std::string tampered = zip;
    tampered[tampered.size() / 2] ^= 1;
    orion::UpdateInstaller bad(get, serve(tampered));
    ASSERT_TRUE(bad.start(release, "PPSA99061", fresh_dir("orion-update-tampered")));
    finish(bad);
    EXPECT_EQ(bad.state(), orion::UpdateInstaller::State::failed);
    EXPECT_EQ(bad.error(), "the download does not match SHA256SUMS");
}

} // namespace hui
