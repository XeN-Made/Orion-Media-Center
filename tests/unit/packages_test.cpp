// ORION Media Center - Tests: PS4 and PS5 packages, read and matched against what is installed.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "orion/packages.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{

using hui::orion::PackageId;
using hui::orion::PackageKind;
using hui::orion::PackageState;

// The magic numbers PS4 and PS5 packages start with.
const std::string_view kCnt("\x7f"
                            "CNT",
                            4);
const std::string_view kFih("\x7f"
                            "FIH",
                            4);

// A head of `size` bytes with `magic` first and the content ID at `at`.
std::string head(std::string_view magic, std::size_t at, std::string_view id, std::size_t size)
{
    std::string bytes(size, '\0');
    bytes.replace(0, magic.size(), magic);
    bytes.replace(at, id.size(), id);
    return bytes;
}

TEST(Packages, ReadTheirContentIdWhereverItIs)
{
    PackageId id;
    ASSERT_TRUE(hui::orion::read_package(
        head(kCnt, 0x40, "EP9000-CUSA00495_00-BEYONDTWOSOULS00", 0x1000), &id));
    EXPECT_EQ(id.kind, PackageKind::ps4);
    EXPECT_EQ(id.title_id, "CUSA00495");
    EXPECT_EQ(id.label, "BEYONDTWOSOULS00");

    // A PS5 package: the PS4 header further in.
    ASSERT_TRUE(hui::orion::read_package(
        head(kFih, 0x2040, "UP0000-PPSA08560_00-FULLGAMEUNLOCK00", 0x4000), &id));
    EXPECT_EQ(id.kind, PackageKind::ps5);
    EXPECT_EQ(id.content_id, "UP0000-PPSA08560_00-FULLGAMEUNLOCK00");

    ASSERT_TRUE(hui::orion::read_package(
        head("PS5MPKG1", 0x10, "UP0000-PPSA08560_00-PART_ONE_0000000", 0x100), &id));
    EXPECT_EQ(id.kind, PackageKind::parts);
    EXPECT_EQ(id.label, "PART_ONE_0000000");

    EXPECT_FALSE(hui::orion::read_package("PK\x03\x04 not a package", &id));
    EXPECT_FALSE(hui::orion::read_package(kCnt,
                                          &id)); // too short
    EXPECT_EQ(hui::orion::find_content_id("x UP0000-PPSA0856_00-SHORT y"), "");
}

// What the helper left: the apps with their metadata, and the add-ons.
TEST(Packages, AreMatchedAgainstWhatIsInstalled)
{
    const std::string dir = ::testing::TempDir() + "orion-packages";
    const std::string apps = dir + "/system-apps";
    hui::save::remove_tree(dir);
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    ASSERT_TRUE(hui::save::ensure_directory(apps));
    ASSERT_TRUE(hui::save::ensure_directory(apps + "/PPSA08560"));
    ASSERT_TRUE(hui::save::ensure_directory(apps + "/CUSA00495"));
    hui::save::write_atomic(apps + "/index.txt", "PPSA08560\nCUSA00495\n");
    hui::save::write_atomic(apps + "/PPSA08560/param.json",
                            R"({"contentId": "UP0000-PPSA08560_00-0000000000000000"})");
    hui::save::write_atomic(
        apps + "/CUSA00495/param.sfo",
        std::string("\0PSF CONTENT_ID\0EP9000-CUSA00495_00-BEYONDTWOSOULS00\0", 52));
    hui::save::write_atomic(apps + "/addcont.txt", "PPSA08560\tFULLGAMEUNLOCK00\n");
    const hui::orion::Installed installed = hui::orion::read_installed(dir);
    EXPECT_EQ(installed.apps.at("CUSA00495"), "EP9000-CUSA00495_00-BEYONDTWOSOULS00");

    const auto state = [&](std::string_view content)
    {
        PackageId id;
        hui::orion::read_package(head(kCnt, 0x40, content, 0x100), &id);
        return hui::orion::package_state(id, installed);
    };
    EXPECT_EQ(state("UP0000-PPSA08560_00-FULLGAMEUNLOCK00"), PackageState::installed);
    EXPECT_EQ(state("UP0000-PPSA08560_00-SOUNDTRACK000000"), PackageState::not_installed);
    EXPECT_EQ(state("EP9000-CUSA00495_00-BEYONDTWOSOULS00"), PackageState::installed);
    EXPECT_EQ(state("UP0000-PPSA99999_00-0000000000000000"), PackageState::no_game);
    EXPECT_EQ(hui::orion::package_state(PackageId(), installed), PackageState::unknown);
}

} // namespace
