// ORION Media Center - Tests: pictures that move (animated GIF and WebP) and their caps.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/animation.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace hui
{
namespace
{

// An 8 x 4 GIF of three frames, red (50 ms), blue (200 ms), green (0 ms),
// each drawn over the one before.
std::string traffic_gif()
{
    static constexpr std::uint8_t kBytes[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x08, 0x00, 0x04, 0x00, 0x81, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0xff, 0x0b,
        0x4e, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2e, 0x30, 0x03, 0x01, 0x00,
        0x00, 0x00, 0x21, 0xf9, 0x04, 0x04, 0x05, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00,
        0x00, 0x08, 0x00, 0x04, 0x00, 0x00, 0x08, 0x0c, 0x00, 0x01, 0x08, 0x1c, 0x48, 0xb0,
        0xa0, 0xc1, 0x83, 0x04, 0x03, 0x02, 0x00, 0x21, 0xf9, 0x04, 0x05, 0x14, 0x00, 0x01,
        0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00, 0x81, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0c, 0x00, 0x01, 0x08,
        0x1c, 0x48, 0xb0, 0xa0, 0xc1, 0x83, 0x04, 0x03, 0x02, 0x00, 0x21, 0xf9, 0x04, 0x05,
        0x00, 0x00, 0x01, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00, 0x81,
        0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0c,
        0x00, 0x01, 0x08, 0x1c, 0x48, 0xb0, 0xa0, 0xc1, 0x83, 0x04, 0x03, 0x02, 0x00, 0x3b};
    return {reinterpret_cast<const char *>(kBytes), sizeof(kBytes)};
}

// The chunks of a WebP file, just enough to be probed.
std::string webp(bool animated, int frames, int width, int height)
{
    const auto le = [](std::uint32_t value, int bytes)
    {
        std::string out;
        for (int i = 0; i < bytes; ++i)
            out += static_cast<char>((value >> (8 * i)) & 0xff);
        return out;
    };
    std::string body = "WEBP";
    body += "VP8X" + le(10, 4);
    body += static_cast<char>(animated ? 0x02 : 0x00);
    body += std::string(3, '\0');
    body += le(static_cast<std::uint32_t>(width - 1), 3) +
            le(static_cast<std::uint32_t>(height - 1), 3);
    body += "ANIM" + le(6, 4) + std::string(6, '\0');
    for (int i = 0; i < frames; ++i)
        body += "ANMF" + le(3, 4) + std::string(4, '\0'); // odd size: padded
    return "RIFF" + le(static_cast<std::uint32_t>(body.size()), 4) + body;
}

TEST(Animation, ProbeCountsTheFramesOfAGif)
{
    library::AnimationInfo info;
    ASSERT_TRUE(library::probe_animation(traffic_gif(), &info));
    EXPECT_EQ(info.width, 8);
    EXPECT_EQ(info.height, 4);
    EXPECT_EQ(info.frames, 3);
}

TEST(Animation, ProbeRefusesStillAndBrokenPictures)
{
    library::AnimationInfo info;
    EXPECT_FALSE(library::probe_animation("", &info));
    EXPECT_FALSE(library::probe_animation("\x89PNG\r\n\x1a\n not really", &info));
    // Cut after the first frame: one frame is a still picture.
    const std::string gif = traffic_gif();
    EXPECT_FALSE(library::probe_animation(gif.substr(0, 82), &info));
    EXPECT_EQ(info.frames, 0);
    // Cut anywhere else: never read past the end (the sanitizers watch).
    for (std::size_t cut = 0; cut < gif.size(); ++cut)
        library::probe_animation(gif.substr(0, cut), &info);
}

TEST(Animation, ProbeReadsAnimatedWebpChunks)
{
    library::AnimationInfo info;
    ASSERT_TRUE(library::probe_animation(webp(true, 5, 640, 360), &info));
    EXPECT_EQ(info.width, 640);
    EXPECT_EQ(info.height, 360);
    EXPECT_EQ(info.frames, 5);
    // The animation flag is what says so, and it needs a frame.
    EXPECT_FALSE(library::probe_animation(webp(false, 5, 640, 360), &info));
    EXPECT_FALSE(library::probe_animation(webp(true, 0, 640, 360), &info));
}

TEST(Animation, PlanShrinksFramesToFitTheCaps)
{
    library::AnimationPlan plan;
    // Small and short: kept as it is.
    EXPECT_EQ(library::plan_animation({480, 270, 60}, &plan), library::AnimationRefusal::none);
    EXPECT_EQ(plan.factor, 1);
    EXPECT_EQ(plan.bytes, std::size_t{480} * 270 * 4 * 60);
    // 1080p: at most 1280 on a side, so half.
    EXPECT_EQ(library::plan_animation({1920, 1080, 10}, &plan), library::AnimationRefusal::none);
    EXPECT_EQ(plan.factor, 2);
    EXPECT_EQ(plan.width, 960);
    EXPECT_EQ(plan.height, 540);
    // Long at 1080p: shrunk until every frame fits in the budget.
    EXPECT_EQ(library::plan_animation({1920, 1080, 200}, &plan), library::AnimationRefusal::none);
    EXPECT_LE(plan.bytes, library::kAnimationBytes);
    EXPECT_GE(std::max(plan.width, plan.height), library::kAnimationSmallest);
    // Too long to keep at a size worth showing, or more frames than kept.
    EXPECT_EQ(library::plan_animation({1920, 1080, 400}, &plan), library::AnimationRefusal::length);
    EXPECT_EQ(library::plan_animation({64, 64, library::kAnimationFrames + 1}, &plan),
              library::AnimationRefusal::length);
    // A 4K frame is refused before anything is decoded.
    EXPECT_EQ(library::plan_animation({3840, 2160, 2}, &plan), library::AnimationRefusal::canvas);
    EXPECT_EQ(plan.factor, 1);
    EXPECT_EQ(plan.bytes, 0u);
}

TEST(Animation, DecoderGivesEveryFrameWithItsDelay)
{
    const std::string gif = traffic_gif();
    library::AnimationDecoder decoder;
    ASSERT_TRUE(decoder.open(gif, 1));
    library::Image frame;
    int delay = 0;
    const std::uint8_t expected[3][3] = {{255, 0, 0}, {0, 0, 255}, {0, 255, 0}};
    const int delays[3] = {50, 200, 100}; // 0 shows as browsers show it
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(decoder.next(&frame, &delay)) << i;
        ASSERT_EQ(frame.width, 8);
        ASSERT_EQ(frame.height, 4);
        EXPECT_EQ(delay, delays[i]);
        for (int c = 0; c < 3; ++c)
            EXPECT_EQ(frame.rgba[static_cast<std::size_t>(c)], expected[i][c]) << i;
        EXPECT_EQ(frame.rgba[3], 255);
    }
    EXPECT_FALSE(decoder.next(&frame, &delay));
    EXPECT_TRUE(frame.rgba.empty());
}

TEST(Animation, DecoderShrinksByThePlansFactor)
{
    const std::string gif = traffic_gif();
    library::AnimationDecoder decoder;
    ASSERT_TRUE(decoder.open(gif, 2));
    library::Image frame;
    int delay = 0;
    ASSERT_TRUE(decoder.next(&frame, &delay));
    EXPECT_EQ(frame.width, 4);
    EXPECT_EQ(frame.height, 2);
    EXPECT_EQ(frame.rgba[0], 255);
}

TEST(Animation, DecoderRefusesWhatIsNotAMovingPicture)
{
    library::AnimationDecoder decoder;
    EXPECT_FALSE(decoder.open("", 1));
    EXPECT_FALSE(decoder.open("not a picture at all", 1));
    library::Image frame;
    int delay = 0;
    EXPECT_FALSE(decoder.next(&frame, &delay));
}

TEST(Animation, ClockLoopsOnceEveryFrameIsThere)
{
    library::AnimationClock clock;
    clock.advance(1.0f); // nothing yet: nothing to do
    EXPECT_EQ(clock.frame, 0u);
    clock.delays = {100, 200};
    clock.advance(0.05f);
    EXPECT_EQ(clock.frame, 0u);
    clock.advance(0.06f);
    EXPECT_EQ(clock.frame, 1u);
    // More frames may come: it waits on the last one there.
    clock.advance(1.0f);
    EXPECT_EQ(clock.frame, 1u);
    clock.complete = true;
    clock.advance(0.01f);
    EXPECT_EQ(clock.frame, 0u);
    // A long stall moves on at most one lap, never spinning.
    clock.advance(1000.0f);
    EXPECT_LT(clock.frame, 2u);
}

TEST(Animation, GifStillReadsAsItsFirstFrame)
{
    // A GIF chosen where a still picture is wanted (a title's art).
    library::Image image;
    ASSERT_TRUE(library::decode_png(traffic_gif(), 64, &image));
    EXPECT_EQ(image.width, 8);
    EXPECT_EQ(image.rgba[0], 255);
}

} // namespace
} // namespace hui
