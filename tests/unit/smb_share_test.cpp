// ORION Media Center - Tests: the SMB share server's hashes, sign-in check, paths and wildcards.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../payload/smbshare/smb_crypto.h"
#include "../../payload/smbshare/smb_server.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

std::string hex(const std::uint8_t *data, std::size_t size)
{
    std::string text;
    char pair[3];
    for (std::size_t i = 0; i < size; ++i)
    {
        std::snprintf(pair, sizeof(pair), "%02x", data[i]);
        text += pair;
    }
    return text;
}

std::vector<std::uint8_t> utf16(const std::string &text)
{
    std::vector<std::uint8_t> out;
    for (const char c : text)
    {
        out.push_back(static_cast<std::uint8_t>(c));
        out.push_back(0);
    }
    return out;
}

// The published vectors: RFC 1320 (MD4), RFC 1321 (MD5), FIPS 180-2
// (SHA-256), RFC 2104 and 4231 (their HMACs).
TEST(SmbShareCrypto, MatchesThePublishedVectors)
{
    std::uint8_t out[32];
    smb_md4("abc", 3, out);
    EXPECT_EQ(hex(out, 16), "a448017aaf21d8525fc10ae87aa6729d");
    smb_md5("abc", 3, out);
    EXPECT_EQ(hex(out, 16), "900150983cd24fb0d6963f7d28e17f72");
    // 80 bytes: past one block.
    std::string digits;
    for (int i = 0; i < 8; ++i)
        digits += "1234567890";
    smb_md5(digits.data(), digits.size(), out);
    EXPECT_EQ(hex(out, 16), "57edf4a22be3c955ac49da2e2107b67a");
    smb_md4(digits.data(), digits.size(), out);
    EXPECT_EQ(hex(out, 16), "e33b4ddc9c38f2199c3e7b164fcc0536");

    smb_sha256_t sha;
    smb_sha256_init(&sha);
    smb_sha256_update(&sha, "abc", 3);
    smb_sha256_final(&sha, out);
    EXPECT_EQ(hex(out, 32), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::vector<std::uint8_t> key16(16, 0x0b);
    smb_hmac_md5(key16.data(), key16.size(), "Hi There", 8, out);
    EXPECT_EQ(hex(out, 16), "9294727a3638bb1c13f48ef8158bfc9d");

    const std::vector<std::uint8_t> key20(20, 0x0b);
    smb_hmac_sha256_t mac;
    smb_hmac_sha256_init(&mac, key20.data(), key20.size());
    smb_hmac_sha256_update(&mac, "Hi ", 3); // in two pieces, as signing feeds it
    smb_hmac_sha256_update(&mac, "There", 5);
    smb_hmac_sha256_final(&mac, out);
    EXPECT_EQ(hex(out, 32), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    std::uint8_t text[] = {'P', 'l', 'a', 'i', 'n', 't', 'e', 'x', 't'};
    const std::uint8_t rc4_key[] = {'K', 'e', 'y'};
    smb_rc4(rc4_key, sizeof(rc4_key), text, sizeof(text));
    EXPECT_EQ(hex(text, sizeof(text)), "bbf316e8d940af0ad3");
}

// [MS-NLMP] 4.2.2.1.2 and 4.2.4: user "User", domain "Domain", password
// "Password", the server's challenge 0123456789abcdef.
TEST(SmbShareCrypto, ChecksMicrosoftsNtlmv2Example)
{
    std::uint8_t hash[16];
    smb_nt_hash("Password", hash);
    EXPECT_EQ(hex(hash, 16), "a4f49c406510bdcab6824ee7c30fd852");

    const std::uint8_t challenge[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    std::vector<std::uint8_t> temp = {1, 1, 0, 0, 0, 0, 0, 0}; // versions, then zeros
    temp.insert(temp.end(), 8, 0);                             // the time
    temp.insert(temp.end(), 8, 0xaa);                          // the client's challenge
    temp.insert(temp.end(), 4, 0);
    const auto pair = [&](std::uint16_t id, const std::string &value)
    {
        const std::vector<std::uint8_t> wide = utf16(value);
        temp.push_back(static_cast<std::uint8_t>(id));
        temp.push_back(0);
        temp.push_back(static_cast<std::uint8_t>(wide.size()));
        temp.push_back(0);
        temp.insert(temp.end(), wide.begin(), wide.end());
    };
    pair(2, "Domain");
    pair(1, "Server");
    temp.insert(temp.end(), 4, 0); // the end of the pairs
    temp.insert(temp.end(), 4, 0);
    // The response is NTProofStr, then temp.
    const std::string proof = "68cd0ab851e51c96aabc927bebef6a1c";
    std::vector<std::uint8_t> response;
    for (std::size_t i = 0; i < proof.size(); i += 2)
        response.push_back(static_cast<std::uint8_t>(std::stoul(proof.substr(i, 2), nullptr, 16)));
    response.insert(response.end(), temp.begin(), temp.end());

    std::uint8_t key[16];
    ASSERT_EQ(
        smb_ntlmv2_check(hash, "User", "Domain", challenge, response.data(), response.size(), key),
        1);
    EXPECT_EQ(hex(key, 16), "8de40ccadbc14a82f15cb0ad0de95ca3");
    // The user's case does not matter; the domain and the password do.
    EXPECT_EQ(
        smb_ntlmv2_check(hash, "user", "Domain", challenge, response.data(), response.size(), key),
        1);
    EXPECT_EQ(
        smb_ntlmv2_check(hash, "User", "Other", challenge, response.data(), response.size(), key),
        0);
    std::uint8_t wrong[16];
    smb_nt_hash("password", wrong);
    EXPECT_EQ(
        smb_ntlmv2_check(wrong, "User", "Domain", challenge, response.data(), response.size(), key),
        0);
}

std::string share_path(const char *root, const std::string &name)
{
    const std::vector<std::uint8_t> wide = utf16(name);
    char out[512];
    const std::size_t n = smb_share_path(root, wide.data(), wide.size(), out, sizeof(out));
    return n == 0 ? std::string("(refused)") : std::string(out, n);
}

// What a client names stays under the share; ".." and streams are refused.
TEST(SmbShare, KeepsPathsInsideTheShare)
{
    EXPECT_EQ(share_path("/mnt/usb0", ""), "/mnt/usb0");
    EXPECT_EQ(share_path("/mnt/usb0/", ""), "/mnt/usb0");
    EXPECT_EQ(share_path("/mnt/usb0", "homebrew\\PPSA01234-app\\eboot.bin"),
              "/mnt/usb0/homebrew/PPSA01234-app/eboot.bin");
    EXPECT_EQ(share_path("/mnt/usb0", "\\games\\.\\a"), "/mnt/usb0/games/a");
    EXPECT_EQ(share_path("/mnt/usb0", "games\\..\\..\\etc"), "(refused)");
    EXPECT_EQ(share_path("/mnt/usb0", ".."), "(refused)");
    EXPECT_EQ(share_path("/mnt/usb0", "file.txt:Zone.Identifier"), "(refused)");
}

TEST(SmbShare, MatchesWindowsWildcards)
{
    EXPECT_TRUE(smb_wildcard_match("*", "anything.pkg"));
    EXPECT_TRUE(smb_wildcard_match("*", ""));
    EXPECT_TRUE(smb_wildcard_match("*.PKG", "game.pkg"));
    EXPECT_FALSE(smb_wildcard_match("*.pkg", "game.pkg.part"));
    EXPECT_TRUE(smb_wildcard_match("eboot.bin", "EBOOT.BIN"));
    EXPECT_TRUE(smb_wildcard_match("g?me*", "game.rar"));
    EXPECT_TRUE(smb_wildcard_match("<.<", "x.y")); // DOS forms
    EXPECT_FALSE(smb_wildcard_match("a", "ab"));
}

} // namespace
