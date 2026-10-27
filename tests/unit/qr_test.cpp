// ORION Media Center - Tests: QR codes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/qr.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>

#include <string>

namespace hui
{
namespace
{

// The format bits as a reader finds them beside the top-left finder: 15
// bits, error correction level and mask under their BCH code.
int format_bits(const QrCode &code)
{
    int bits = 0;
    const auto take = [&](int x, int y, int i) { bits |= (code.dark(x, y) ? 1 : 0) << i; };
    for (int i = 0; i <= 5; ++i)
        take(8, i, i);
    take(8, 7, 6);
    take(8, 8, 7);
    take(7, 8, 8);
    for (int i = 9; i < 15; ++i)
        take(14 - i, 8, i);
    return bits;
}

bool is_finder(const QrCode &code, int left, int top)
{
    for (int dy = 0; dy < 7; ++dy)
    {
        for (int dx = 0; dx < 7; ++dx)
        {
            const int ring = std::max(std::abs(dx - 3), std::abs(dy - 3));
            if (code.dark(left + dx, top + dy) != (ring != 2))
                return false;
        }
    }
    return true;
}

TEST(Qr, AShortAddressIsAVersionTwoCode)
{
    QrCode code;
    ASSERT_TRUE(make_qr("http://192.168.1.20:9062", &code)); // 24 bytes: version 2 at level M
    EXPECT_EQ(code.size, 25);
    ASSERT_EQ(code.modules.size(), 25u * 25u);
    EXPECT_TRUE(is_finder(code, 0, 0));
    EXPECT_TRUE(is_finder(code, code.size - 7, 0));
    EXPECT_TRUE(is_finder(code, 0, code.size - 7));
    for (int i = 8; i < code.size - 8; ++i)
    {
        EXPECT_EQ(code.dark(i, 6), i % 2 == 0); // the timing patterns
        EXPECT_EQ(code.dark(6, i), i % 2 == 0);
    }
    EXPECT_TRUE(code.dark(8, code.size - 8)); // the module that is always dark
    // The format: level M (00) and a mask, whose BCH remainder checks out.
    const int format = format_bits(code) ^ 0x5412;
    EXPECT_EQ(format >> 13, 0);
    int rest = format >> 10;
    for (int i = 0; i < 10; ++i)
        rest = (rest << 1) ^ ((rest >> 9) * 0x537);
    EXPECT_EQ(rest, format & 0x3ff);
}

TEST(Qr, LongerTextsTakeLargerVersionsUpToNine)
{
    QrCode code;
    ASSERT_TRUE(make_qr("", &code));
    EXPECT_EQ(code.size, 21);
    ASSERT_TRUE(make_qr(std::string(14, 'a'), &code)); // version 1 holds 14 bytes at M
    EXPECT_EQ(code.size, 21);
    ASSERT_TRUE(make_qr(std::string(15, 'a'), &code));
    EXPECT_EQ(code.size, 25);
    ASSERT_TRUE(make_qr(std::string(180, 'a'), &code)); // version 9: 182 data codewords
    EXPECT_EQ(code.size, 53);
    EXPECT_FALSE(make_qr(std::string(181, 'a'), &code));
    EXPECT_EQ(code.size, 0);
    EXPECT_TRUE(code.modules.empty());
}

} // namespace
} // namespace hui
