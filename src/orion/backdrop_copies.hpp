// ORION Media Center - Copies of the system's home-screen pictures, fetched through local FTP.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hui::orion
{

// For titles that ship no picture of their own (disc images, mostly), the
// console's copy in /user/appmeta/<ID>/pic0.png (or pic1.png), fetched from
// the FTP server on the loopback (orion/local_ftp.hpp) on a thread of its own
// and kept as <folder>/<ID>.png. A title the system has none for gets an
// empty <ID>.none, so it is not asked about again. Without an FTP server
// nothing is written, and the next start tries again.
//
//   BackdropCopies copies;
//   copies.start({"PPSA08560", "PPSA17905"}, "/data/orion/backdrops", 2121);
class BackdropCopies
{
  public:
    struct Result
    {
        int copied = 0;
        int none = 0;
        bool server = false; // an FTP server answered
        bool finished = false;
    };

    BackdropCopies() = default;
    ~BackdropCopies();
    BackdropCopies(const BackdropCopies &) = delete;
    BackdropCopies &operator=(const BackdropCopies &) = delete;

    // False while a run is going. pictures: the files of /user/appmeta/<ID>
    // to try, in order ("/icon0.png" for the sharper icons).
    bool start(std::vector<std::string> ids, std::string folder, int port,
               std::vector<std::string> pictures = {"/pic0.png", "/pic1.png"});
    bool running() const
    {
        return running_.load();
    }
    Result result() const;
    // Waits for the run (tests).
    void wait();

  private:
    void run(std::vector<std::string> ids, std::string folder, int port,
             std::vector<std::string> pictures);

    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    Result result_;
};

} // namespace hui::orion
