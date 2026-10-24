// ORION Media Center - Tests: wide covers made from the home-screen pictures.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "library/icon.hpp"
#include "orion/wide_copies.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace hui
{
namespace
{

library::Image picture(int width, int height)
{
    library::Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < image.rgba.size(); i += 4)
    {
        image.rgba[i] = 200;     // red
        image.rgba[i + 1] = 100; // green
        image.rgba[i + 2] = 50;  // blue
        image.rgba[i + 3] = 255;
    }
    return image;
}

TEST(WideCopies, WritesATgaTheDecoderReads)
{
    library::Image back;
    ASSERT_TRUE(library::decode_png(orion::encode_tga(picture(6, 4)), 64, &back));
    EXPECT_EQ(back.width, 6);
    EXPECT_EQ(back.height, 4);
    EXPECT_EQ(back.rgba[0], 200); // red stays red: TGA keeps BGRA
    EXPECT_EQ(back.rgba[2], 50);
}

TEST(WideCopies, MakesSmallWideCopiesAndRefusesSquareOnes)
{
    const std::string dir = ::testing::TempDir() + "orion-wide";
    save::remove_tree(dir);
    ASSERT_TRUE(save::ensure_directory(dir));
    ASSERT_TRUE(
        save::write_atomic(dir + "/wide.tga", orion::encode_tga(picture(1280, 720))).empty());
    ASSERT_TRUE(
        save::write_atomic(dir + "/square.tga", orion::encode_tga(picture(512, 512))).empty());

    orion::WideCopies wide;
    ASSERT_TRUE(wide.start({{"PPSA90180", dir + "/wide.tga"},
                            {"PPSA90181", dir + "/square.tga"},
                            {"PPSA90182", dir + "/missing.png"}},
                           dir + "/out"));
    wide.wait();
    EXPECT_EQ(wide.made(), 1);
    std::string bytes;
    library::Image copy;
    ASSERT_TRUE(save::read_file(dir + "/out/PPSA90180.tga", &bytes, 8u << 20));
    ASSERT_TRUE(library::decode_png(bytes, 2048, &copy));
    EXPECT_LE(copy.width, orion::WideCopies::kWideSize);
    EXPECT_GT(copy.width * 5, copy.height * 6); // still wide
    EXPECT_TRUE(save::read_file(dir + "/out/PPSA90181.none", &bytes));
    EXPECT_TRUE(save::read_file(dir + "/out/PPSA90182.none", &bytes));
    save::remove_tree(dir);
}

} // namespace
} // namespace hui
