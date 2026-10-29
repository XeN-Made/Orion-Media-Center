// ORION Media Center - SMB shares: listing and copying files off a Windows share or a NAS.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace hui::sys
{

// Who ORION is to a share. An empty user signs in as a guest.
struct SmbLogin
{
    std::string server; // "192.168.1.20" or "nas"
    std::string share;  // "games"
    std::string user;
    std::string password;
    std::string domain; // usually empty (a workgroup)
};

struct SmbEntry
{
    std::string path; // from the folder listed, "/" between folders: "gba/Moon Garden.gba"
    bool directory = false;
    std::uint64_t size = 0;
};

// What is in a folder of the share ("" its top, "roms/gba" deeper), and in
// its folders down to `depth` more levels (0: the folder alone). At most
// `most` entries; hidden names (".x") are left out. Blocking: call it from a
// worker thread. On a PC it always says there is no network.
bool smb_list(const SmbLogin &login, const std::string &folder, int depth, std::size_t most,
              std::vector<SmbEntry> *entries, std::string *error);

// A file of the share copied to `local` (through <local>.part, renamed when
// whole); *done counts the bytes so far, and *cancel set stops it. Blocking.
bool smb_download(const SmbLogin &login, const std::string &path, const std::string &local,
                  std::atomic<std::uint64_t> *done, const std::atomic<bool> *cancel,
                  std::string *error);

// A machine answering SMB on the local network.
struct SmbServer
{
    std::string address; // "192.168.1.20"
    std::string name;    // its Windows (NetBIOS) name, when it says: "MEDIA-PC"
};

// The machines on the console's local network (the 256 addresses around
// `own`, "192.168.1.30") that take SMB connections, with their names. About
// two seconds, never more than ten. *trace says what each way found. Blocking.
// On a PC: none.
std::vector<SmbServer> smb_find_servers(const std::string &own, std::string *trace);

// The folders a server shares with this user (login.share is not used):
// disk shares only, not the hidden ones ending in "$". Blocking.
bool smb_shares(const SmbLogin &login, std::vector<std::string> *shares, std::string *error);

} // namespace hui::sys
