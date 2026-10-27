// ORION Media Center - QR codes: a short text as a square of dark and light modules.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace hui
{

// A QR code (ISO/IEC 18004) of up to 180 bytes, for an address a phone
// scans off the screen: byte mode, error correction level M (a code still
// reads with about 15% of it smudged), versions 1 to 9, the mask that
// scores best. Draw it with a light margin of four modules around it.
//
//   QrCode code;
//   if (make_qr("http://192.168.1.20:9062", &code))
//       for (int y = 0; y < code.size; ++y)
//           for (int x = 0; x < code.size; ++x)
//               if (code.dark(x, y)) fill_square(x, y);
struct QrCode
{
    int size = 0;                      // modules per side: 21 for version 1, 4 more for each after
    std::vector<std::uint8_t> modules; // size * size, row by row; 1 is dark
    bool dark(int x, int y) const
    {
        return modules[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                       static_cast<std::size_t>(x)] != 0;
    }
};

// False (and *code empty) for a text too long for version 9.
bool make_qr(std::string_view text, QrCode *code);

} // namespace hui
