// ps5-homebrew-ui - A small FTP client for the console's own FTP server.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace hui::orion
{

// The system keeps every title's home-screen pictures in /user/appmeta,
// which ORION's sandbox cannot read. An FTP server payload, if the player
// runs one (ftpsrv on 2121 is common), can: ORION asks it for them over the
// loopback, logged in anonymously, in binary and passive mode.
//
//   LocalFtp ftp;
//   if (ftp.open(2121))
//       ftp.get("/user/appmeta/PPSA08560/pic0.png", 24 << 20, &bytes);
class LocalFtp
{
  public:
    LocalFtp() = default;
    ~LocalFtp();
    LocalFtp(const LocalFtp &) = delete;
    LocalFtp &operator=(const LocalFtp &) = delete;

    // Connects to 127.0.0.1:port and logs in. False when nothing answers.
    bool open(int port);
    // One file, up to limit bytes. False when the server has none.
    bool get(const std::string &path, std::size_t limit, std::string *bytes);
    void close();
    // The last reply, for logs.
    const std::string &last_reply() const
    {
        return reply_;
    }

  private:
    int reply();
    bool command(const std::string &line, int expect_class);

    int control_ = -1;
    std::string pending_; // bytes read past the last reply
    std::string reply_;
};

// Parts, exposed for tests.
// The port of a "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)" reply; 0 when it is not one.
int passive_port(std::string_view reply);
// The code a complete reply ends with ("230 ..."), or 0 while it is incomplete
// (a multi-line reply runs "230-..." until "230 ...").
int reply_code(std::string_view text, std::size_t *end);

} // namespace hui::orion
