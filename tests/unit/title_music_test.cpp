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

} // namespace
} // namespace hui
