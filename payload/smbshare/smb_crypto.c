// ORION Media Center - The hashes SMB 2.1 and NTLMv2 need: MD4, MD5, SHA-256, their HMACs, RC4.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "smb_crypto.h"

#include <string.h>

static uint32_t rotl(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static uint32_t rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// MD4 and MD5 share their padding: the message, 0x80, zeros, the bit length
// (little-endian) in the last 8 bytes of a 64-byte block.
typedef void (*md_block_fn)(uint32_t state[4], const uint8_t block[64]);

static void md_digest(md_block_fn block_fn, const void *data, size_t size, uint8_t out[16])
{
    uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    const uint8_t *bytes = (const uint8_t *)data;
    size_t left = size;
    while (left >= 64)
    {
        block_fn(state, bytes);
        bytes += 64;
        left -= 64;
    }
    uint8_t tail[128];
    memset(tail, 0, sizeof(tail));
    memcpy(tail, bytes, left);
    tail[left] = 0x80;
    const size_t blocks = left + 9 > 64 ? 2 : 1;
    const uint64_t bits = (uint64_t)size * 8u;
    for (int i = 0; i < 8; ++i)
        tail[blocks * 64 - 8 + (size_t)i] = (uint8_t)(bits >> (8 * i));
    for (size_t i = 0; i < blocks; ++i)
        block_fn(state, tail + i * 64);
    for (int i = 0; i < 4; ++i)
        put_le32(out + 4 * i, state[i]);
}

static void md4_block(uint32_t state[4], const uint8_t block[64])
{
    uint32_t x[16];
    for (int i = 0; i < 16; ++i)
        x[i] = le32(block + 4 * i);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
    static const int r1[4] = {3, 7, 11, 19};
    for (int i = 0; i < 16; ++i)
    {
        const uint32_t t = rotl(a + F(b, c, d) + x[i], r1[i % 4]);
        a = d;
        d = c;
        c = b;
        b = t;
    }
    static const int r2[4] = {3, 5, 9, 13};
    static const int o2[16] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
    for (int i = 0; i < 16; ++i)
    {
        const uint32_t t = rotl(a + G(b, c, d) + x[o2[i]] + 0x5a827999u, r2[i % 4]);
        a = d;
        d = c;
        c = b;
        b = t;
    }
    static const int r3[4] = {3, 9, 11, 15};
    static const int o3[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    for (int i = 0; i < 16; ++i)
    {
        const uint32_t t = rotl(a + H(b, c, d) + x[o3[i]] + 0x6ed9eba1u, r3[i % 4]);
        a = d;
        d = c;
        c = b;
        b = t;
    }
#undef F
#undef G
#undef H
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_block(uint32_t state[4], const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u,
        0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u,
        0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du,
        0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
        0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u,
        0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
        0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u,
        0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu,
        0xeb86d391u};
    static const int r[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t x[16];
    for (int i = 0; i < 16; ++i)
        x[i] = le32(block + 4 * i);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (int i = 0; i < 64; ++i)
    {
        uint32_t f;
        int g;
        if (i < 16)
        {
            f = (b & c) | (~b & d);
            g = i;
        }
        else if (i < 32)
        {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        }
        else if (i < 48)
        {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        }
        else
        {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const uint32_t t = d;
        d = c;
        c = b;
        b = b + rotl(a + f + k[i] + x[g], r[i]);
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void smb_md4(const void *data, size_t size, uint8_t out[16])
{
    md_digest(md4_block, data, size, out);
}

void smb_md5(const void *data, size_t size, uint8_t out[16])
{
    md_digest(md5_block, data, size, out);
}

void smb_hmac_md5(const void *key, size_t key_size, const void *data, size_t size,
                  uint8_t out[16])
{
    // Every key NTLM hands it is 16 bytes: no hashing of long keys needed,
    // but a long one is cut rather than overrun.
    uint8_t pad[64];
    memset(pad, 0, sizeof(pad));
    memcpy(pad, key, key_size > 64 ? 64 : key_size);
    // MD5 here is one-shot, so the inner message is built whole: (key ^
    // ipad) || data. The caller caps data at kMaxHmacData.
    static const size_t kMaxHmacData = 4096;
    if (size > kMaxHmacData)
        size = kMaxHmacData;
    uint8_t inner[64 + 4096];
    uint8_t outer[64 + 16];
    for (int i = 0; i < 64; ++i)
    {
        inner[i] = pad[i] ^ 0x36;
        outer[i] = pad[i] ^ 0x5c;
    }
    memcpy(inner + 64, data, size);
    uint8_t inner_hash[16];
    smb_md5(inner, 64 + size, inner_hash);
    memcpy(outer + 64, inner_hash, 16);
    smb_md5(outer, sizeof(outer), out);
}

static const uint32_t kSha256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static void sha256_block(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 |
               (uint32_t)block[4 * i + 2] << 8 | (uint32_t)block[4 * i + 3];
    for (int i = 16; i < 64; ++i)
    {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i)
    {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + kSha256[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void smb_sha256_init(smb_sha256_t *sha)
{
    static const uint32_t start[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(sha->state, start, sizeof(start));
    sha->length = 0;
    sha->used = 0;
}

void smb_sha256_update(smb_sha256_t *sha, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    sha->length += size;
    while (size > 0)
    {
        size_t take = 64 - sha->used;
        if (take > size)
            take = size;
        memcpy(sha->block + sha->used, bytes, take);
        sha->used += take;
        bytes += take;
        size -= take;
        if (sha->used == 64)
        {
            sha256_block(sha->state, sha->block);
            sha->used = 0;
        }
    }
}

void smb_sha256_final(smb_sha256_t *sha, uint8_t out[32])
{
    const uint64_t bits = sha->length * 8u;
    const uint8_t one = 0x80;
    const uint8_t zero = 0;
    smb_sha256_update(sha, &one, 1);
    while (sha->used != 56)
        smb_sha256_update(sha, &zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i)
        length[i] = (uint8_t)(bits >> (56 - 8 * i));
    smb_sha256_update(sha, length, 8);
    for (int i = 0; i < 8; ++i)
    {
        out[4 * i] = (uint8_t)(sha->state[i] >> 24);
        out[4 * i + 1] = (uint8_t)(sha->state[i] >> 16);
        out[4 * i + 2] = (uint8_t)(sha->state[i] >> 8);
        out[4 * i + 3] = (uint8_t)sha->state[i];
    }
}

void smb_hmac_sha256_init(smb_hmac_sha256_t *mac, const void *key, size_t key_size)
{
    uint8_t pad[64];
    memset(pad, 0, sizeof(pad));
    if (key_size > 64)
    {
        smb_sha256_t sha;
        smb_sha256_init(&sha);
        smb_sha256_update(&sha, key, key_size);
        smb_sha256_final(&sha, pad);
    }
    else
        memcpy(pad, key, key_size);
    uint8_t inner[64];
    for (int i = 0; i < 64; ++i)
    {
        inner[i] = pad[i] ^ 0x36;
        mac->outer_key[i] = pad[i] ^ 0x5c;
    }
    smb_sha256_init(&mac->inner);
    smb_sha256_update(&mac->inner, inner, 64);
}

void smb_hmac_sha256_update(smb_hmac_sha256_t *mac, const void *data, size_t size)
{
    smb_sha256_update(&mac->inner, data, size);
}

void smb_hmac_sha256_final(smb_hmac_sha256_t *mac, uint8_t out[32])
{
    uint8_t inner_hash[32];
    smb_sha256_final(&mac->inner, inner_hash);
    smb_sha256_t outer;
    smb_sha256_init(&outer);
    smb_sha256_update(&outer, mac->outer_key, 64);
    smb_sha256_update(&outer, inner_hash, 32);
    smb_sha256_final(&outer, out);
}

void smb_rc4(const uint8_t *key, size_t key_size, uint8_t *data, size_t size)
{
    uint8_t s[256];
    for (int i = 0; i < 256; ++i)
        s[i] = (uint8_t)i;
    uint8_t j = 0;
    for (int i = 0; i < 256; ++i)
    {
        j = (uint8_t)(j + s[i] + key[(size_t)i % key_size]);
        const uint8_t t = s[i];
        s[i] = s[j];
        s[j] = t;
    }
    uint8_t a = 0;
    uint8_t b = 0;
    for (size_t n = 0; n < size; ++n)
    {
        a = (uint8_t)(a + 1);
        b = (uint8_t)(b + s[a]);
        const uint8_t t = s[a];
        s[a] = s[b];
        s[b] = t;
        data[n] ^= s[(uint8_t)(s[a] + s[b])];
    }
}

void smb_nt_hash(const char *password, uint8_t out[16])
{
    // UTF-8 to UTF-16LE, characters outside the BMP as surrogate pairs.
    uint8_t wide[512];
    size_t size = 0;
    const uint8_t *p = (const uint8_t *)password;
    while (*p != 0 && size + 4 <= sizeof(wide))
    {
        uint32_t c = *p++;
        if (c >= 0xf0 && p[0] && p[1] && p[2])
        {
            c = (c & 0x07) << 18 | (uint32_t)(p[0] & 0x3f) << 12 | (uint32_t)(p[1] & 0x3f) << 6 |
                (p[2] & 0x3f);
            p += 3;
        }
        else if (c >= 0xe0 && p[0] && p[1])
        {
            c = (c & 0x0f) << 12 | (uint32_t)(p[0] & 0x3f) << 6 | (p[1] & 0x3f);
            p += 2;
        }
        else if (c >= 0xc0 && p[0])
        {
            c = (c & 0x1f) << 6 | (p[0] & 0x3f);
            p += 1;
        }
        if (c >= 0x10000)
        {
            c -= 0x10000;
            const uint32_t high = 0xd800 + (c >> 10);
            const uint32_t low = 0xdc00 + (c & 0x3ff);
            wide[size++] = (uint8_t)high;
            wide[size++] = (uint8_t)(high >> 8);
            c = low;
        }
        wide[size++] = (uint8_t)c;
        wide[size++] = (uint8_t)(c >> 8);
    }
    smb_md4(wide, size, out);
}
