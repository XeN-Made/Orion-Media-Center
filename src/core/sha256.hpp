// ORION Media Center - SHA-256, for checking downloads against published sums.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace hui
{

// FIPS 180-4 SHA-256, fed in pieces.
//
//   Sha256 hash;
//   hash.update(bytes);
//   std::string hex = hash.hex(); // "e3b0c442..."
class Sha256
{
  public:
    Sha256();
    void update(std::string_view bytes);
    // The digest as 64 lowercase hex digits. Ends the hash: call once.
    std::string hex();

  private:
    void block(const std::uint8_t *data);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0; // bytes
};

// The hex digest of bytes in one call.
std::string sha256_hex(std::string_view bytes);

} // namespace hui
