// ORION Media Center - One signed-in SMB connection: the share listing's and the game copier's.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "platform/ps5/smb.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct smb2_context;

namespace hui::sys
{

// Console only (libsmb2). Closed when it goes.
class SmbConnection
{
  public:
    SmbConnection() = default;
    SmbConnection(const SmbConnection &) = delete;
    SmbConnection &operator=(const SmbConnection &) = delete;
    ~SmbConnection();

    bool open(const SmbLogin &login, std::string *error);
    smb2_context *get() const
    {
        return smb2_;
    }
    // libsmb2's last error, or "no answer".
    std::string said() const;
    // As smb_list, through this connection.
    bool list(const std::string &folder, const std::string &prefix, int depth, std::size_t most,
              std::vector<SmbEntry> *entries, std::string *error);

  private:
    smb2_context *smb2_ = nullptr;
    bool connected_ = false;
};

} // namespace hui::sys
