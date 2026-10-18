// ps5-homebrew-ui - Tests: decoding title icons.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "library/icon.hpp"

#include <gtest/gtest.h>

#include <string>

namespace hui
{
namespace
{

// A 256 x 256 RGB PNG: a vertical gradient from (24, 18, 64) with an amber
// disc of (255, 196, 92) in the middle.
std::string starfall_icon()
{
    std::string bytes;
    EXPECT_TRUE(save::read_file(std::string(HUI_SOURCE_DIR) +
                                    "/tests/fixtures/library/homebrew/PPSA90101/sce_sys/icon0.png",
                                &bytes));
    return bytes;
}

const std::uint8_t *pixel(const library::Image &image, int x, int y)
{
    return image.rgba.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
}

TEST(Icon, DecodesToOpaqueRgbaTopRowFirst)
{
    library::Image image;
    ASSERT_TRUE(library::decode_png(starfall_icon(), 256, &image));
    EXPECT_EQ(image.width, 256);
    EXPECT_EQ(image.height, 256);
    ASSERT_EQ(image.rgba.size(), 256u * 256u * 4u);
    const std::uint8_t *corner = pixel(image, 0, 0);
    EXPECT_EQ(corner[0], 24);
    EXPECT_EQ(corner[1], 18);
    EXPECT_EQ(corner[2], 64);
    EXPECT_EQ(corner[3], 255); // RGB gains an opaque alpha
    const std::uint8_t *centre = pixel(image, 128, 128);
    EXPECT_EQ(centre[0], 255);
    EXPECT_EQ(centre[1], 196);
    EXPECT_EQ(centre[2], 92);
}

TEST(Icon, ShrinksByAWholeFactor)
{
    library::Image half;
    ASSERT_TRUE(library::decode_png(starfall_icon(), 128, &half));
    EXPECT_EQ(half.width, 128);
    EXPECT_EQ(half.height, 128);
    EXPECT_EQ(pixel(half, 64, 64)[0], 255);

    // 256 does not fit in 100 at a factor of 2; 3 gives 85.
    library::Image third;
    ASSERT_TRUE(library::decode_png(starfall_icon(), 100, &third));
    EXPECT_EQ(third.width, 85);
    EXPECT_EQ(third.height, 85);
}

TEST(Icon, RefusesWhatIsNotAPng)
{
    library::Image image;
    EXPECT_FALSE(library::decode_png("", 256, &image));
    EXPECT_FALSE(library::decode_png("not a picture at all", 256, &image));
    std::string truncated = starfall_icon();
    truncated.resize(truncated.size() / 3);
    EXPECT_FALSE(library::decode_png(truncated, 256, &image));
    EXPECT_TRUE(image.rgba.empty());
    EXPECT_EQ(image.width, 0);
}

} // namespace
} // namespace hui
