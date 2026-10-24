// ORION Media Center - Tests: the drives Storage shows, and the titles on each.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/drives.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace hui
{
namespace
{

constexpr std::uint64_t kGb = 1000ull * 1000ull * 1000ull;

library::Title at(const std::string &path)
{
    library::Title title;
    title.id = path;
    title.path = path;
    return title;
}

// A title counts on the drive its folder, image or game file is on; the
// console's own folders are its storage.
TEST(Drives, TellsWhichDriveAPathIsOn)
{
    EXPECT_EQ(orion::drive_of("/mnt/usb1/homebrew/PPSA07570-app"), "/mnt/usb1");
    EXPECT_EQ(orion::drive_of("/mnt/ext0/homebrew/game.ffpfsc"), "/mnt/ext0");
    EXPECT_EQ(orion::drive_of("/mnt/usb0"), "/mnt/usb0");
    EXPECT_EQ(orion::drive_of("/data/homebrew/PPSA99062"), "/data");
    EXPECT_EQ(orion::drive_of("/user/app/PPSA01234"), "/data");
    EXPECT_EQ(orion::drive_of("/database"), ""); // not /data
    EXPECT_EQ(orion::drive_of(""), "");
}

// The drives that answer, in order (console, extended, USB), with their
// titles; one not plugged in is left out.
TEST(Drives, ListsTheDrivesThatAnswerWithTheirTitles)
{
    const orion::SpaceOf space = [](const std::string &path, orion::DriveSpace *out)
    {
        if (path == "/data")
            *out = {825 * kGb, 312 * kGb};
        else if (path == "/mnt/usb1")
            *out = {2000 * kGb, 1460 * kGb};
        else if (path == "/mnt/ext0")
            *out = {0, 0}; // answers, but holds nothing: not a drive
        else if (path == "/mnt/usb2")
            *out = {2097152, 1720320}; // the console's placeholder on an empty mount point
        else
            return false;
        return true;
    };
    const std::vector<library::Title> titles = {
        at("/data/homebrew/PPSA90101"), at("/user/app/PPSA90102"),
        at("/mnt/usb1/homebrew/UFC 5.ffpfsc"), at("/mnt/usb0/PSP/game.iso"), at("")};
    const std::vector<orion::Drive> drives = orion::find_drives(titles, space);
    ASSERT_EQ(drives.size(), 2u);
    EXPECT_EQ(drives[0].kind, orion::DriveKind::console);
    EXPECT_EQ(drives[0].titles, 2);
    EXPECT_EQ(drives[0].space.free, 312 * kGb);
    EXPECT_EQ(drives[1].kind, orion::DriveKind::usb);
    EXPECT_EQ(drives[1].number, 1); // USB drive 2
    EXPECT_EQ(drives[1].titles, 1);
    EXPECT_TRUE(orion::find_drives(titles, {}).empty());
}

// The launch helper's answer (drives-space.txt): a line per drive; a broken
// line, or more free than there is, says nothing.
TEST(Drives, ReadsTheHelpersAnswer)
{
    const orion::SpaceOf space = orion::space_from_report(
        "/data\t825000000000\t312000000000\n/mnt/usb1\t2000000000000\t1460000000000\n"
        "/mnt/usb0\tbroken\n/mnt/ext0\t100\t200\n");
    orion::DriveSpace got;
    ASSERT_TRUE(space("/data", &got));
    EXPECT_EQ(got.capacity, 825 * kGb);
    EXPECT_EQ(got.free, 312 * kGb);
    EXPECT_TRUE(space("/mnt/usb1", &got));
    EXPECT_FALSE(space("/mnt/usb0", &got));
    EXPECT_FALSE(space("/mnt/ext0", &got));
    EXPECT_FALSE(space("/mnt/usb2", &got));
    const std::vector<orion::Drive> drives =
        orion::find_drives({at("/mnt/usb1/homebrew/a")}, space);
    ASSERT_EQ(drives.size(), 2u);
    EXPECT_EQ(drives[1].titles, 1);
    EXPECT_FALSE(orion::space_from_report("")("/data", &got));
}

TEST(Drives, WritesSizesAsDrivesAreSold)
{
    EXPECT_EQ(orion::format_bytes(512ull * 1000 * 1000), "512 MB");
    EXPECT_EQ(orion::format_bytes(312 * kGb), "312 GB");
    EXPECT_EQ(orion::format_bytes(1800 * kGb), "1.8 TB");
    EXPECT_EQ(orion::format_bytes(0), "0 MB");
}

} // namespace
} // namespace hui
