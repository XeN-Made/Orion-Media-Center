// ORION Media Center - A small SMB 2.1 server: the console's drives as Windows network shares.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What Windows' File Explorer needs to browse, copy to and from, rename and
// delete on a share, and nothing more:
//
//   - SMB 2.1 only (2.0.2 when that is all a client offers): signing with
//     HMAC-SHA256, no encryption, no leases or oplocks, no durable handles;
//   - NTLMv2 sign-in, inside SPNEGO or bare, against one user name and the
//     MD4 hash of its password (Windows 11 refuses guests and unsigned
//     servers, so both are required here);
//   - the shares listed in IPC$ through srvsvc's NetShareEnumAll, so
//     \\<address> shows them in Explorer.
//
// Portable C over POSIX sockets, threads and files: the payload runs it on
// the console, tools/smb-share-host.sh on a PC (where Windows can test it on
// another port: net use \\<wsl address>\share /TCPPORT:<port>).
//
//   smb_config_t config = {0};
//   config.port = 445;
//   smb_set_password(&config, "orion", "secret");
//   smb_add_share(&config, "usb0", "/mnt/usb0");
//   config.keep_going = still_on;          // asked about once a second
//   smb_serve(&config);                     // returns when it says stop

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMB_MAX_SHARES 16

typedef struct
{
    char name[32];  // as Windows shows it: "usb0"
    char path[256]; // where it is: "/mnt/usb0"
} smb_share_t;

typedef struct
{
    int port;
    char computer[16]; // the name it gives itself ("PS5"), upper case
    char user[64];     // compared without regard to case
    uint8_t nt_hash[16];
    int read_only;
    smb_share_t shares[SMB_MAX_SHARES];
    int share_count;
    // Asked about once a second; 0 ends smb_serve. NULL: forever.
    int (*keep_going)(void *context);
    void *context;
    // A line for the log (no newline), or NULL for none.
    void (*log)(void *context, const char *line);
} smb_config_t;

void smb_set_password(smb_config_t *config, const char *user, const char *password);
int smb_add_share(smb_config_t *config, const char *name, const char *path);

// Listens on config->port (all addresses) and serves until keep_going says
// stop. 0 when it stopped as asked, else the errno of what failed (EADDRINUSE:
// another server has the port).
int smb_serve(const smb_config_t *config);

// ---- exposed for the tests ----

// Checks an NTLMv2 response against the password's hash; on success the
// session's base key goes to session_key. 1 when it matches.
int smb_ntlmv2_check(const uint8_t nt_hash[16], const char *user, const char *domain,
                     const uint8_t server_challenge[8], const uint8_t *response,
                     size_t response_size, uint8_t session_key[16]);

// A path a client asked for (UTF-16LE, backslashes) under a share's root, as
// UTF-8 with slashes. 0 for a name that leaves the share ("..") or names a
// stream (":"), else the length written.
size_t smb_share_path(const char *root, const uint8_t *name, size_t name_size, char *out,
                      size_t out_size);

// Windows' wildcards over a name: * ? and the DOS forms < > ". Without
// regard to case.
int smb_wildcard_match(const char *pattern, const char *name);

#ifdef __cplusplus
}
#endif
