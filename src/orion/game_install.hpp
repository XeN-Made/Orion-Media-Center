// ORION Media Center - PS5 games installed off a network drive: what, where to, and the job.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/stack_thread.hpp"
#include "orion/package_server.hpp"
#include "orion/drives.hpp"
#include "orion/packages.hpp"
#include "orion/shares.hpp"
#include "platform/ps5/game_copy.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hui::orion
{

// What a file on a share is to the installer, by its name: an image
// ShadowMountPlus mounts (.ffpkg, .ffpfs, .exfat), an archive (.zip, .7z,
// .rar), or nothing. A later volume of an archive (x.part2.rar, x.r00,
// x.7z.002) is nothing: the first one stands for them all.
sys::GameSourceKind install_kind(std::string_view name);

// The volumes of the archive whose first one is `first`, among the names in
// its folder, in the order they are read: x.part1.rar, x.part2.rar...;
// x.rar, x.r00, x.r01...; x.7z.001, x.7z.002... One volume alone is itself.
std::vector<std::string> archive_volumes(const std::vector<std::string> &names,
                                         const std::string &first);

// The shallowest folder ("" for the top) that holds sce_sys/param.json,
// among files given as paths under one folder ("Game/sce_sys/param.json").
bool game_root(const std::vector<std::string> &files, std::string *root);

// A folder name from what the game came as: its archive's name without the
// volume and archive endings ("Moon Garden.part1.rar": "Moon Garden"),
// without characters a drive refuses.
std::string install_name(std::string_view source);

// Where a game can go: the homebrew folder on the console and on each drive
// ShadowMountPlus looks at, those there now, with the space free on each.
struct InstallPlace
{
    std::string path;       // "/mnt/usb0/homebrew"
    std::string drive;      // "/mnt/usb0", or "/data" for the console's own
    std::uint64_t free = 0; // bytes
};
// The drives a game can go to, with what is free on each, as space_of
// says (space_from_report on a console: inside ORION's sandbox the system
// refuses statfs, and asking it there ended ORION). A drive slot with less
// than a gigabyte is an empty one.
std::vector<InstallPlace> install_places(const SpaceOf &space_of);

// One folder of a share listed on a thread of its own (SMB waits on the
// network), for the install browser: its folders first, then its files.
// Up to `most` bytes of a file on a share from `offset` (a package's head).
using ShareHeadReader =
    std::function<bool(const sys::SmbLogin &login, const std::string &path, std::uint64_t offset,
                       std::size_t most, std::string *bytes)>;
bool read_share_head(const sys::SmbLogin &login, const std::string &path, std::uint64_t offset,
                     std::size_t most, std::string *bytes);
// A package on a share, read where the installer asks (sys::SmbFile).
std::unique_ptr<PackageSource> open_share_package(const sys::SmbLogin &login,
                                                  const std::string &path, std::string *error);

class ShareListing
{
  public:
    // With `heads`, each .pkg in the folder (60 at most) is read for what it is.
    explicit ShareListing(SmbLister list, ShareHeadReader heads = {});
    ~ShareListing();
    ShareListing(const ShareListing &) = delete;
    ShareListing &operator=(const ShareListing &) = delete;

    bool start(sys::SmbLogin login, std::string folder);
    bool running() const
    {
        return running_.load();
    }
    // True once after a listing ended; *error is empty when it worked.
    bool take_finished(std::vector<sys::SmbEntry> *entries, std::string *error);
    // The packages of the last listing taken, by their path in the folder.
    const std::map<std::string, PackageId> &packages() const
    {
        return taken_packages_;
    }
    void wait();

  private:
    SmbLister list_;
    ShareHeadReader heads_;
    std::map<std::string, PackageId> packages_;
    std::map<std::string, PackageId> taken_packages_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    bool finished_ = false;
    std::vector<sys::SmbEntry> entries_;
    std::string error_;
};

// Copies (or unpacks) a game into a hidden folder beside its place, then
// moves the game's folder, or its images, out of it into the place: a game
// that stops half way leaves nothing behind but that folder, cleared next time.
//
//   GameInstall install(sys::copy_game);
//   install.start(source, "Moon Garden", "/data/homebrew");
//   ...every frame: install.done(), install.total();
//   if (install.take_finished(&result)) ...
class GameInstall
{
  public:
    using Copier = std::function<bool(const sys::GameSource &source, const std::string &into,
                                      sys::CopyProgress *progress, std::string *error)>;
    // Why a game did not go in, for the screen to say in its words.
    enum class Problem : std::uint8_t
    {
        none,
        copy,          // the share or the archive: detail says what it said
        stopped,       // cancel()
        no_game,       // nothing with sce_sys/param.json in it, and no image
        already_there, // detail: the folder or image in the way
        cannot_write,  // detail: where
    };
    struct Result
    {
        bool ok = false;
        Problem problem = Problem::none;
        std::string detail;
        std::string installed; // where the game is now
    };

    explicit GameInstall(Copier copy);
    ~GameInstall();
    GameInstall(const GameInstall &) = delete;
    GameInstall &operator=(const GameInstall &) = delete;

    // False while one is going.
    bool start(sys::GameSource source, std::string name, std::string place);
    // Each step goes to log (on the worker thread): what it did last, if
    // the app ends while it works.
    void set_log(std::function<void(const std::string &line)> log)
    {
        log_ = std::move(log);
    }
    bool running() const
    {
        return running_.load();
    }
    void cancel()
    {
        progress_.cancel = true;
    }
    std::uint64_t done() const
    {
        return progress_.done.load();
    }
    std::uint64_t total() const
    {
        return progress_.total.load();
    }
    // The name of the game going (or gone) in: main thread only.
    const std::string &name() const
    {
        return name_;
    }
    // True once after it ended.
    bool take_finished(Result *result);
    void wait();

  private:
    Result run(const sys::GameSource &source, const std::string &place, const std::string &name);

    Copier copy_;
    std::string name_;
    sys::CopyProgress progress_;
    std::function<void(const std::string &line)> log_;
    StackThread worker_; // unpacking runs deep: a stack of its own size
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    bool finished_ = false;
    Result result_;
};

} // namespace hui::orion
