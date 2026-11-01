// ORION Media Center - Sharing the console's drives: what ORION asks the share payload for.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORION writes <state>/smb-share.txt and sends payload/smbshare to the ELF
// loader; the payload serves the drives over SMB (a PC opens \\<console>)
// and keeps going after ORION closes. It reads the file again whenever it
// changes, and stops when it says off. What it is doing it writes to
// <state>/smb-share-status.txt.
//
//   orion::ConsoleShare share = orion::read_console_share(state);
//   share.on = true;
//   orion::write_console_share(state, share);   // then send the payload
//   settings.set_console_share(share, orion::console_share_problem(state), address);

#pragma once

#include <string>

namespace hui::orion
{

struct ConsoleShare
{
    bool on = false;
    std::string user = "ps5";
    std::string password; // empty: not set yet, and nothing is shared
};

ConsoleShare read_console_share(const std::string &state_dir);
bool write_console_share(const std::string &state_dir, const ConsoleShare &share);

// Why the payload could not share, for Settings; empty while it shares
// (or has said nothing).
std::string console_share_problem(const std::string &state_dir);

} // namespace hui::orion
