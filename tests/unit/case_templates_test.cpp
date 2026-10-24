// ORION Media Center - Tests: case pictures, their cover windows and the names they go by.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/case_templates.hpp"

#include <gtest/gtest.h>

namespace hui
{
namespace
{

// A case picture: opaque all over, with a clear rectangle (x0..x1, y0..y1).
library::Image case_picture(int width, int height, int x0, int y0, int x1, int y1)
{
    library::Image picture;
    picture.width = width;
    picture.height = height;
    picture.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
            picture.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] = 0;
    }
    return picture;
}

// The window is the clear rectangle around the middle, as fractions.
TEST(CaseTemplates, FindTheCoverWindow)
{
    library::CaseWindow window;
    ASSERT_TRUE(library::find_case_window(case_picture(100, 200, 4, 30, 96, 196), &window));
    EXPECT_FLOAT_EQ(window.x, 0.04f);
    EXPECT_FLOAT_EQ(window.y, 0.15f);
    EXPECT_FLOAT_EQ(window.w, 0.92f);
    EXPECT_FLOAT_EQ(window.h, 0.83f);
    EXPECT_FALSE(window.solid);
    // Opaque and one colour all over: that colour is the window (here all of
    // it). A clear spot too small for a cover: none.
    ASSERT_TRUE(library::find_case_window(case_picture(100, 200, 0, 0, 0, 0), &window));
    EXPECT_TRUE(window.solid);
    EXPECT_FALSE(library::find_case_window(case_picture(100, 200, 45, 95, 55, 105), &window));
    // A white sleeve under a coloured header, saved without transparency.
    library::Image white = case_picture(100, 200, 0, 0, 0, 0);
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 100; ++x)
        {
            const bool header = y < 30;
            std::uint8_t *pixel = &white.rgba[(static_cast<std::size_t>(y) * 100 + x) * 4];
            pixel[0] = header ? 20 : 250;
            pixel[1] = header ? 60 : 250;
            pixel[2] = header ? 200 : 252;
        }
    }
    ASSERT_TRUE(library::find_case_window(white, &window));
    EXPECT_TRUE(window.solid);
    EXPECT_FLOAT_EQ(window.y, 0.15f);
    EXPECT_FLOAT_EQ(window.h, 0.85f);
    library::Image broken;
    broken.width = 100;
    broken.height = 100;
    EXPECT_FALSE(library::find_case_window(broken, &window));
}

// ps5 and ps4 for console titles, homebrew and media, and a game's system
// by its short name, with ES-DE's as the second to try.
TEST(CaseTemplates, NameEachKindOfCase)
{
    library::Title game;
    game.platform = library::Platform::ps5;
    EXPECT_EQ(library::case_key(game), "ps5");
    game.platform = library::Platform::ps4;
    EXPECT_EQ(library::case_key(game), "ps4");
    library::Title homebrew;
    homebrew.homebrew = true;
    EXPECT_EQ(library::case_key(homebrew), "homebrew");
    library::Title rom;
    rom.system = "Nintendo - Wii U";
    std::string alias;
    EXPECT_EQ(library::case_key(rom, &alias), "wiiu");
    EXPECT_EQ(alias, "wiiu");
    rom.system = "Nintendo - GameCube";
    EXPECT_EQ(library::case_key(rom, &alias), "ngc");
    EXPECT_EQ(alias, "gc");
    rom.system = "Sony - PlayStation 2";
    EXPECT_EQ(library::case_key(rom, &alias), "ps2");
}

} // namespace
} // namespace hui
