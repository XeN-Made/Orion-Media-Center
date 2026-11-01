// ORION Media Center - The hashes SMB 2.1 and NTLMv2 need: MD4, MD5, SHA-256, their HMACs, RC4.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small and self-contained, for a payload that has no crypto library: none
// of this protects anything on its own (NTLM's design is old), it is what the
// protocol asks a server to compute.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void smb_md4(const void *data, size_t size, uint8_t out[16]);
void smb_md5(const void *data, size_t size, uint8_t out[16]);
void smb_hmac_md5(const void *key, size_t key_size, const void *data, size_t size,
                  uint8_t out[16]);

typedef struct
{
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} smb_sha256_t;

void smb_sha256_init(smb_sha256_t *sha);
void smb_sha256_update(smb_sha256_t *sha, const void *data, size_t size);
void smb_sha256_final(smb_sha256_t *sha, uint8_t out[32]);

// HMAC-SHA256 over two pieces (SMB signs a message whose signature field is
// zeroed: the caller passes the parts around it).
typedef struct
{
    smb_sha256_t inner;
    uint8_t outer_key[64];
} smb_hmac_sha256_t;

void smb_hmac_sha256_init(smb_hmac_sha256_t *mac, const void *key, size_t key_size);
void smb_hmac_sha256_update(smb_hmac_sha256_t *mac, const void *data, size_t size);
void smb_hmac_sha256_final(smb_hmac_sha256_t *mac, uint8_t out[32]);

// RC4 in place: NTLM's key exchange sends the session key under it.
void smb_rc4(const uint8_t *key, size_t key_size, uint8_t *data, size_t size);

// NTOWFv1: MD4 of the password in UTF-16LE (what ORION keeps instead of it).
void smb_nt_hash(const char *password, uint8_t out[16]);

#ifdef __cplusplus
}
#endif
