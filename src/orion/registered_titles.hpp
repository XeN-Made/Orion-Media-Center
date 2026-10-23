// ORION Media Center - The titles the system has registered: ShadowMountPlus's list and local FTP.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace hui::orion
{

// ORION's own scan looks in the homebrew folders. The home screen also has
// installed packages (PS4 and PS5) and images ShadowMountPlus mounted from
// anywhere it looks (a drive's root, an exFAT image named by its contents).
// The system lists them all in /user/app, which ORION's sandbox cannot read;
// the console's FTP server can (orion/local_ftp.hpp), when one runs, and it
// knows each title's category from its metadata (/user/appmeta/<ID>/
// param.json for PS5, /system_data/priv/appmeta/<ID>/param.sfo for PS4).
// ShadowMountPlus's own list (orion/shadowmount_api.hpp) adds every title it
// mounts or found installed, so its backups show here with no FTP server.
// On a thread of its own this writes them all to <state>/registered.tsv
// (library::merge_registered reads it), and copies each one's picture to
// <state>/icons/<ID>.png once.
//
//   RegisteredTitles registered;
//   registered.start("/data/orion", 2121);
//   ...every frame:
//   if (registered.take_finished() && registered.result().changed) merge();
class RegisteredTitles
{
  public:
    struct Result
    {
        int titles = 0;       // with metadata
        int icons = 0;        // pictures copied this run
        bool server = false;  // ShadowMountPlus or an FTP server answered
        bool changed = false; // the cache is not what it was
    };

    RegisteredTitles() = default;
    ~RegisteredTitles();
    RegisteredTitles(const RegisteredTitles &) = delete;
    RegisteredTitles &operator=(const RegisteredTitles &) = delete;

    // False while a run is going.
    bool start(std::string state_dir, int port);
    bool running() const
    {
        return running_.load();
    }
    // True once after a run ended.
    bool take_finished();
    Result result() const;
    void wait();

  private:
    void run(std::string state_dir, int port);

    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    Result result_;
    bool finished_ = false;
};

} // namespace hui::orion
