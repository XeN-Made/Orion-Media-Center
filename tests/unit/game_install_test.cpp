// ORION Media Center - Tests: PS5 games installed off a network drive.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "orion/game_install.hpp"

#include <gtest/gtest.h>

#include <string>
#include <sys/stat.h>
#include <vector>

namespace
{

using hui::orion::archive_volumes;
using hui::orion::game_root;
using hui::orion::GameInstall;
using hui::orion::install_kind;
using hui::orion::install_name;
using hui::sys::GameSourceKind;

bool exists(const std::string &path)
{
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0;
}

// A place made empty for one test.
std::string fresh_place(const std::string &name)
{
    const std::string place = ::testing::TempDir() + "orion-install-" + name;
    hui::save::remove_tree(place);
    hui::save::ensure_directory(place);
    return place;
}

// Where a place's game is made before it moves in: beside it.
std::string staging_of(const std::string &place)
{
    return place.substr(0, place.find_last_of('/')) + "/.orion-install";
}

// Writes files under `into` as a copier would, each "path" with a little in it.
GameInstall::Copier writes(std::vector<std::string> files, bool ok = true)
{
    return [files = std::move(files), ok](const hui::sys::GameSource &, const std::string &into,
                                          hui::sys::CopyProgress *progress, std::string *error)
    {
        for (const std::string &file : files)
        {
            std::string folder = into;
            std::size_t at = 0;
            while ((at = file.find('/', at)) != std::string::npos)
            {
                folder = into + "/" + file.substr(0, at);
                hui::save::ensure_directory(folder);
                ++at;
            }
            hui::save::write_atomic(into + "/" + file, "data");
            progress->done += 4;
        }
        if (!ok)
            *error = "the share went away";
        return ok;
    };
}

GameInstall::Result install(GameInstall &job, GameSourceKind kind, const std::string &name,
                            const std::string &place)
{
    hui::sys::GameSource source;
    source.kind = kind;
    source.paths = {name};
    EXPECT_TRUE(job.start(source, name, place));
    job.wait();
    GameInstall::Result result;
    EXPECT_TRUE(job.take_finished(&result));
    EXPECT_FALSE(job.running());
    return result;
}

TEST(GameInstall, KnowsWhatItCanInstallByName)
{
    EXPECT_EQ(install_kind("Moon Garden.ffpkg"), GameSourceKind::file);
    EXPECT_EQ(install_kind("games/Moon Garden.EXFAT"), GameSourceKind::file);
    EXPECT_EQ(install_kind("Moon Garden.zip"), GameSourceKind::archive);
    EXPECT_EQ(install_kind("Moon Garden.7z"), GameSourceKind::archive);
    EXPECT_EQ(install_kind("Moon Garden.rar"), GameSourceKind::archive);
    // Only the first volume stands for the archive.
    EXPECT_EQ(install_kind("Moon Garden.part1.rar"), GameSourceKind::archive);
    EXPECT_EQ(install_kind("Moon Garden.part01.rar"), GameSourceKind::archive);
    EXPECT_EQ(install_kind("Moon Garden.part02.rar"), GameSourceKind::none);
    EXPECT_EQ(install_kind("Moon Garden.r00"), GameSourceKind::none);
    EXPECT_EQ(install_kind("Moon Garden.7z.001"), GameSourceKind::archive);
    EXPECT_EQ(install_kind("Moon Garden.7z.002"), GameSourceKind::none);
    EXPECT_EQ(install_kind("Moon Garden.gba"), GameSourceKind::none);
    EXPECT_EQ(install_kind("notes.txt.001"), GameSourceKind::none);
}

TEST(GameInstall, PutsAnArchivesVolumesInOrder)
{
    const std::vector<std::string> folder = {"Moon Garden.part10.rar",
                                             "Moon Garden.part2.rar",
                                             "Moon Garden.part1.rar",
                                             "Other.part1.rar",
                                             "Old.r01",
                                             "Old.rar",
                                             "Old.r00",
                                             "Split.7z.002",
                                             "Split.7z.001",
                                             "Alone.zip"};
    EXPECT_EQ(archive_volumes(folder, "Moon Garden.part1.rar"),
              (std::vector<std::string>{"Moon Garden.part1.rar", "Moon Garden.part2.rar",
                                        "Moon Garden.part10.rar"}));
    EXPECT_EQ(archive_volumes(folder, "Old.rar"),
              (std::vector<std::string>{"Old.rar", "Old.r00", "Old.r01"}));
    EXPECT_EQ(archive_volumes(folder, "Split.7z.001"),
              (std::vector<std::string>{"Split.7z.001", "Split.7z.002"}));
    EXPECT_EQ(archive_volumes(folder, "Alone.zip"), (std::vector<std::string>{"Alone.zip"}));
}

TEST(GameInstall, FindsTheGamesFolderAndNamesIt)
{
    std::string root = "x";
    EXPECT_TRUE(
        game_root({"readme.txt", "PPSA01234/sce_sys/param.json", "PPSA01234/eboot.bin"}, &root));
    EXPECT_EQ(root, "PPSA01234");
    EXPECT_TRUE(game_root({"sce_sys/param.json", "deeper/sce_sys/param.json"}, &root));
    EXPECT_EQ(root, ""); // the archive is the game
    EXPECT_FALSE(game_root({"readme.txt", "sce_sys/icon0.png"}, &root));

    EXPECT_EQ(install_name("games/Moon Garden.part1.rar"), "Moon Garden");
    EXPECT_EQ(install_name("Moon Garden.7z.001"), "Moon Garden");
    EXPECT_EQ(install_name("Moon: Garden?.zip"), "Moon_ Garden_");
    EXPECT_EQ(install_name("..."), "Game");
}

// An archive with the game in a folder: the folder lands in the place, and
// the hidden folder it was unpacked into is gone.
TEST(GameInstall, MovesTheGameIntoItsPlace)
{
    const std::string place = fresh_place("folder");
    GameInstall job(writes({"Moon Garden/sce_sys/param.json", "Moon Garden/eboot.bin"}));
    const GameInstall::Result result =
        install(job, GameSourceKind::archive, "Moon Garden.part1.rar", place);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.installed, place + "/Moon Garden");
    EXPECT_TRUE(exists(place + "/Moon Garden/sce_sys/param.json"));
    EXPECT_FALSE(exists(staging_of(place)));
    EXPECT_EQ(job.done(), 8u);

    // The same again: it is already there, and nothing else is left.
    const GameInstall::Result again =
        install(job, GameSourceKind::archive, "Moon Garden.part1.rar", place);
    EXPECT_FALSE(again.ok);
    EXPECT_EQ(again.problem, GameInstall::Problem::already_there);
    EXPECT_FALSE(exists(staging_of(place)));
}

// The archive's top is the game: it is named after the archive.
TEST(GameInstall, NamesAGameWithNoFolderAfterItsArchive)
{
    const std::string place = fresh_place("top");
    GameInstall job(writes({"sce_sys/param.json", "eboot.bin"}));
    const GameInstall::Result result = install(job, GameSourceKind::archive, "Star Path.7z", place);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(exists(place + "/Star Path/eboot.bin"));
}

TEST(GameInstall, SaysWhenThereIsNoGameOrTheCopyFails)
{
    const std::string place = fresh_place("none");
    GameInstall nothing(writes({"readme.txt"}));
    EXPECT_EQ(install(nothing, GameSourceKind::archive, "Notes.zip", place).problem,
              GameInstall::Problem::no_game);
    GameInstall broken(writes({"half/sce_sys/param.json"}, false));
    const GameInstall::Result failed = install(broken, GameSourceKind::archive, "Half.zip", place);
    EXPECT_EQ(failed.problem, GameInstall::Problem::copy);
    EXPECT_EQ(failed.detail, "the share went away");
    EXPECT_FALSE(exists(place + "/half"));
    EXPECT_FALSE(exists(staging_of(place)));
}

// An image is copied by its name; an archive of images lets them out.
TEST(GameInstall, LetsImagesOutByTheirNames)
{
    const std::string place = fresh_place("images");
    GameInstall copy(writes({"Moon Garden.ffpkg"}));
    EXPECT_TRUE(install(copy, GameSourceKind::file, "Moon Garden.ffpkg", place).ok);
    EXPECT_TRUE(exists(place + "/Moon Garden.ffpkg"));
    GameInstall unpack(writes({"inside/Star Path.exfat", "readme.txt"}));
    EXPECT_TRUE(install(unpack, GameSourceKind::archive, "Star Path.zip", place).ok);
    EXPECT_TRUE(exists(place + "/Star Path.exfat"));
    EXPECT_FALSE(exists(place + "/readme.txt"));
}

// Folders first, then files, each by name whatever their case.
TEST(GameInstall, ListsAFolderOfTheShareFoldersFirst)
{
    hui::orion::ShareListing listing(
        [](const hui::sys::SmbLogin &, const std::string &folder, int depth, std::size_t,
           std::vector<hui::sys::SmbEntry> *entries, std::string *)
        {
            EXPECT_EQ(folder, "games/ps5");
            EXPECT_EQ(depth, 0);
            hui::sys::SmbEntry archive;
            archive.path = "moon.zip";
            hui::sys::SmbEntry star;
            star.path = "Star Path";
            star.directory = true;
            hui::sys::SmbEntry apple;
            apple.path = "apple";
            apple.directory = true;
            *entries = {archive, star, apple};
            return true;
        });
    ASSERT_TRUE(listing.start({}, "games/ps5"));
    listing.wait();
    std::vector<hui::sys::SmbEntry> entries;
    std::string error = "x";
    ASSERT_TRUE(listing.take_finished(&entries, &error));
    EXPECT_TRUE(error.empty());
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].path, "apple");
    EXPECT_EQ(entries[1].path, "Star Path");
    EXPECT_EQ(entries[2].path, "moon.zip");
}

} // namespace
