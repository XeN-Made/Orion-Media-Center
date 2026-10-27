// ORION Media Center - Tests: a title's own music, ATRAC9 decoded.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION's own home-screen music (sce_sys/snd0.at9) is a real ATRAC9 file,
// the kind every game has: it is what these decode.

#include "audio/title_music.hpp"
#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace hui
{
namespace
{

std::string orion_song()
{
    std::string data;
    EXPECT_TRUE(
        save::read_file(std::string(HUI_SOURCE_DIR) + "/sce_sys/snd0.at9", &data, 8u << 20));
    return data;
}

TEST(TitleMusic, ReadsAnAtrac9Header)
{
    audio::At9Info info;
    std::string error;
    ASSERT_TRUE(audio::parse_at9(orion_song(), &info, &error)) << error;
    EXPECT_EQ(info.sample_rate, 48000);
    EXPECT_GE(info.channels, 1);
    EXPECT_GT(info.block_align, 0);
    EXPECT_GT(info.data_size, 0u);

    EXPECT_FALSE(audio::parse_at9("RIFF\0\0\0\0WAVE", &info, &error));
    EXPECT_FALSE(audio::parse_at9("OggS not a wave", &info, &error));
    // A PCM wave is not ATRAC9.
    std::string pcm = "RIFF____WAVEfmt ";
    pcm += std::string("\x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0\x04\0\x10\0", 20);
    EXPECT_FALSE(audio::parse_at9(pcm, &info, &error));
}

TEST(TitleMusic, DecodesAndLoopsAnAtrac9Song)
{
    audio::At9Track track;
    ASSERT_EQ(track.open(orion_song()), "");
    // Ten seconds: past the end of a short song, so it loops; never silent
    // throughout, never out of range.
    std::vector<float> out(48000 * 2);
    double energy = 0.0;
    for (int second = 0; second < 10; ++second)
    {
        ASSERT_EQ(track.decode(out.data(), 48000), 48000);
        for (const float sample : out)
        {
            ASSERT_TRUE(std::isfinite(sample));
            ASSERT_LE(std::fabs(sample), 1.0f);
            energy += static_cast<double>(sample) * sample;
        }
    }
    EXPECT_GT(energy, 1.0);

    audio::At9Track broken;
    EXPECT_NE(broken.open("not a song"), "");
    EXPECT_EQ(broken.decode(out.data(), 100), 0);
}

// A quiet preview is brought up, a loud one down, so every title's song
// plays about as loud; never so far up that its peak would clip.
TEST(TitleMusic, BringsEverySongToOneLoudness)
{
    std::vector<float> quiet(48000 * 2);
    std::vector<float> loud(48000 * 2);
    for (std::size_t i = 0; i < quiet.size(); ++i)
    {
        const float wave = std::sin(static_cast<float>(i) * 0.01f);
        quiet[i] = 0.05f * wave;
        loud[i] = 0.3f * wave;
    }
    const float up = audio::title_loudness_gain(quiet.data(), quiet.size());
    const float down = audio::title_loudness_gain(loud.data(), loud.size());
    EXPECT_GT(up, 1.5f);
    EXPECT_LT(down, 0.5f);
    // The same loudness after: within a few per cent.
    EXPECT_NEAR(0.05f * up, 0.3f * down, 0.005f);
    // A quiet song with one loud click: held below clipping.
    quiet[100] = 0.9f;
    EXPECT_LE(audio::title_loudness_gain(quiet.data(), quiet.size()) * 0.9f, 0.95f + 1e-4f);
    // Silence: left as it is.
    std::vector<float> silence(1000, 0.0f);
    EXPECT_FLOAT_EQ(audio::title_loudness_gain(silence.data(), silence.size()), 1.0f);
}

} // namespace
} // namespace hui
