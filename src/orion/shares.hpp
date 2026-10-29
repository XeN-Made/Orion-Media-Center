// ORION Media Center - Network drives: SMB shares whose games ORION lists and copies on first play.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/emulators.hpp"
#include "platform/ps5/smb.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hui::orion
{

// A folder on a Windows share or a NAS, as the player gave it. Its games are
// in folders named for their systems ("gba", "snes", as ES-DE keeps them),
// or the folder is one of those itself. The password is not here: it lives
// in <state_dir>/keys.txt (share_password_key), never written into a page.
struct Share
{
    std::string id;     // "s1": stable, names its folder under <state>/shares
    std::string server; // "192.168.1.20", "nas"
    std::string share;  // "games"
    std::string folder; // inside the share, "/" between folders; "" its top
    std::string user;   // empty: a guest
    std::string domain;
};

// "\\nas\games\roms", "//nas/games/roms", "smb://nas/games/roms" or
// "nas/games": the server, the share and the folder in it. False without a
// server and a share.
bool parse_share_address(std::string_view text, Share *share);
// As Windows writes it: \\nas\games\roms.
std::string share_address(const Share &share);
// keys.txt's line for a share's password.
std::string share_password_key(const std::string &id);
// What to do about an error a share gave (a refused sign-in, a wrong share
// name), in plain words; "" when there is nothing to add. Marked for
// translation.
const char *share_hint(std::string_view error);

// <state_dir>/shares.json. Unknown or broken entries are left out.
std::vector<Share> load_shares(const std::string &state_dir);
bool save_shares(const std::string &state_dir, const std::vector<Share> &shares);
// A share with a fresh id ("s1", "s2"...) appended; its password saved.
const Share &add_share(std::vector<Share> &shares, Share share);

// How ORION signs in to a share (its password read from keys.txt).
sys::SmbLogin share_login(const Share &share, const std::string &state_dir);
// The same with a password in hand (a drive being tried before it is kept).
sys::SmbLogin share_login_with(const Share &share, const std::string &password);
// The user as typed: "PC\\user" (or "PC/user") signs in to that machine's or
// domain's account, as Windows writes it; anything else is the user alone.
void set_share_user(Share *share, std::string_view typed);

// A file a scan found on a share: which share, where in it (from the share's
// top), how big.
struct ShareFile
{
    std::string share_id;
    std::string path;
    std::uint64_t size = 0;
};
// <state_dir>/share-games.tsv: what the last scan found.
std::string share_files_text(const std::vector<ShareFile> &files);
std::vector<ShareFile> read_share_files(std::string_view text);

// The files the library lists as games: each in a folder named for a system
// (or the share's folder is one), as RemoteGame entries that play from
// <state>/shares/<id>/<system folder>/<file> once copied, and come from
// "smb://<id>/<path>". Which files a system's cores take is the library's to
// judge (RemoteGame::filtered).
std::vector<library::RemoteGame> share_remote_games(const std::vector<ShareFile> &files,
                                                    const std::vector<Share> &shares,
                                                    const std::string &state_dir);
inline constexpr std::string_view kShareScheme = "smb://";

// Copies "smb://<id>/<path>" to `path` (through <path>.part), signing in as
// the share's settings say. Blocking.
bool download_share_file(const std::string &state_dir, const std::string &url,
                         const std::string &path, std::string *error);

// Lists every share's folder (two levels down: <folder>/<system>/<game>) on a
// thread of its own and writes what it finds to share-games.tsv. The list is
// handed in so tests run without a network.
using SmbLister =
    std::function<bool(const sys::SmbLogin &login, const std::string &folder, int depth,
                       std::size_t most, std::vector<sys::SmbEntry> *entries, std::string *error)>;
// What happened with network drives, a line at a time, in
// <state>/shares.txt (shared with the logs): tries, finds, installs. Never a
// password.
void log_share(const std::string &state_dir, std::string_view line);

// The drives on the network, then the folders one of them shares, each
// asked on a thread of its own (both wait on the network).
//
//   DriveFinder finder(sys::smb_find_servers, sys::smb_shares);
//   finder.find("192.168.1.30");
//   ...every frame: if (finder.take_finished(&result)) ...
class DriveFinder
{
  public:
    using FindServers = std::function<std::vector<sys::SmbServer>(const std::string &own)>;
    using ListShares = std::function<bool(const sys::SmbLogin &login,
                                          std::vector<std::string> *shares, std::string *error)>;
    struct Result
    {
        bool servers_asked = false; // find() (else list())
        std::vector<sys::SmbServer> servers;
        std::vector<std::string> shares;
        std::string error;
    };

    DriveFinder(FindServers find, ListShares list);
    ~DriveFinder();
    DriveFinder(const DriveFinder &) = delete;
    DriveFinder &operator=(const DriveFinder &) = delete;

    bool find(std::string own);
    bool list(sys::SmbLogin login);
    bool running() const
    {
        return running_.load();
    }
    bool take_finished(Result *result);
    void wait();

  private:
    bool run(std::function<Result()> work);

    FindServers find_;
    ListShares list_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    bool finished_ = false;
    Result result_;
};

class ShareScan
{
  public:
    struct Result
    {
        int shares = 0;    // shares listed
        int files = 0;     // files found
        std::string error; // the first share that could not be read, and why
    };

    explicit ShareScan(SmbLister list);
    ~ShareScan();
    ShareScan(const ShareScan &) = delete;
    ShareScan &operator=(const ShareScan &) = delete;

    bool start(std::vector<Share> shares, std::string state_dir);
    bool running() const
    {
        return running_.load();
    }
    // True once after a scan ended; *result says what it did.
    bool take_finished(Result *result);
    void wait();

  private:
    SmbLister list_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    bool finished_ = false;
    Result result_;
};

} // namespace hui::orion
