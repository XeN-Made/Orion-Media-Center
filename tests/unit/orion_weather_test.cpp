// ORION Media Center - Tests: the weather over the player's background picture.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/prefs.hpp"
#include "orion/weather.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace hui
{
namespace
{

using Kind = orion::Weather::Kind;

// The middle of every shape drawn, summed: changes when anything moves.
float centre_sum(const gfx::DrawList &list)
{
    float sum = 0.0f;
    for (const gfx::Instance &instance : list.instances())
        sum +=
            instance.rect[0] + instance.rect[2] * 0.5f + instance.rect[1] + instance.rect[3] * 0.5f;
    return sum;
}

TEST(OrionWeather, KindsFollowThePrefsKeys)
{
    // The Settings row stores a kPictureEffects key; the design casts its
    // place to a Kind, so the two orders must agree.
    EXPECT_EQ(static_cast<Kind>(orion::picture_effect_index("none")), Kind::none);
    EXPECT_EQ(static_cast<Kind>(orion::picture_effect_index("snow")), Kind::snow);
    EXPECT_EQ(static_cast<Kind>(orion::picture_effect_index("rain")), Kind::rain);
    EXPECT_EQ(static_cast<Kind>(orion::picture_effect_index("fireflies")), Kind::fireflies);
    EXPECT_EQ(orion::picture_effect_index("hail"), 0);
}

TEST(OrionWeather, NoneDrawsNothing)
{
    orion::Weather weather;
    weather.update(0.5f, false);
    gfx::DrawList list;
    weather.draw(list);
    EXPECT_TRUE(list.empty());
}

TEST(OrionWeather, EveryKindIsOnScreenAtOnceAndCheap)
{
    for (const Kind kind : {Kind::snow, Kind::rain, Kind::fireflies})
    {
        orion::Weather weather;
        weather.set_kind(kind);
        gfx::DrawList list;
        weather.draw(list);
        // Scattered over the whole screen, not falling in from the top.
        int low = 0;
        for (const gfx::Instance &instance : list.instances())
            low += instance.rect[1] > gfx::kVirtualHeight * 0.5f ? 1 : 0;
        EXPECT_GT(low, 0) << static_cast<int>(kind);
        // One or two quads a particle: a few hundred, one batch.
        const auto count = static_cast<int>(list.instances().size());
        EXPECT_GE(count, orion::Weather::count(kind));
        EXPECT_LE(count, 2 * orion::Weather::count(kind));
        EXPECT_LE(count, 600);
    }
}

TEST(OrionWeather, ParticlesStayAroundTheScreen)
{
    for (const Kind kind : {Kind::snow, Kind::rain, Kind::fireflies})
    {
        orion::Weather weather;
        weather.set_kind(kind);
        for (int i = 0; i < 60 * 60; ++i)
            weather.update(1.0f / 60.0f, false);
        gfx::DrawList list;
        weather.draw(list);
        for (const gfx::Instance &instance : list.instances())
        {
            EXPECT_GT(instance.rect[0] + instance.rect[2], -200.0f);
            EXPECT_LT(instance.rect[0], gfx::kVirtualWidth + 200.0f);
            EXPECT_GT(instance.rect[1] + instance.rect[3], -200.0f);
            EXPECT_LT(instance.rect[1], gfx::kVirtualHeight + 200.0f);
        }
    }
}

TEST(OrionWeather, ReducedMotionIsNearlyStill)
{
    // How far everything moves in a tenth of a second.
    const auto travel = [](bool reduced)
    {
        orion::Weather weather;
        weather.set_kind(Kind::snow);
        weather.update(0.0f, reduced);
        gfx::DrawList before;
        weather.draw(before);
        weather.update(0.1f, reduced);
        gfx::DrawList after;
        weather.draw(after);
        return std::fabs(centre_sum(after) - centre_sum(before));
    };
    const float moved = travel(false);
    const float crept = travel(true);
    EXPECT_GT(moved, 0.0f);
    EXPECT_GT(crept, 0.0f);
    EXPECT_LT(crept, moved * 0.3f);
}

TEST(OrionWeather, ChangingKindStartsAfreshAndTheSameKindKeepsGoing)
{
    orion::Weather weather;
    weather.set_kind(Kind::rain);
    weather.update(0.2f, false);
    gfx::DrawList first;
    weather.draw(first);
    weather.set_kind(Kind::rain); // asked every frame: nothing changes
    gfx::DrawList again;
    weather.draw(again);
    EXPECT_FLOAT_EQ(centre_sum(first), centre_sum(again));
    weather.set_kind(Kind::fireflies);
    EXPECT_EQ(weather.kind(), Kind::fireflies);
    weather.set_kind(Kind::none);
    gfx::DrawList none;
    weather.draw(none);
    EXPECT_TRUE(none.empty());
}

} // namespace
} // namespace hui
