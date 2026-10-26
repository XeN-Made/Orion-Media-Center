// ORION Media Center - RomM: the games on a RomM server, listed and downloaded.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/emulators.hpp"
#include "orion/steamgriddb.hpp"

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

// keys.txt's lines for RomM (orion/steamgriddb.hpp reads and writes them):
// the server's address and the client token pairing gave.
inline constexpr const char *kRommUrl = "romm_url";
inline constexpr const char *kRommToken = "romm_token";
// A pairing code typed on the key page (orion/key_page.hpp), waiting for
// ORION to exchange it; removed once it has.
inline constexpr const char *kRommPairCode = "romm_pair_code";

// A system on the server: its folder name there ("gba", "psx") is what
// ORION knows it by (library::system_of_folder).
struct RommPlatform
{
    long long id = 0;
    std::string slug;
    std::string fs_slug;
    std::string name;
    int rom_count = 0;
};

// A game on the server.
struct RommGame
{
    long long id = 0;
    long long platform_id = 0;
    std::string fs_slug; // its system's folder on the server
    std::string name;    // as RomM names it (or its file's, unidentified)
    std::string fs_name; // the file ("Moon Garden (USA).gba"), or the folder of a multi-file game
    std::uint64_t size = 0;
    std::string cover;  // the large cover's path on the server, or empty
    bool multi = false; // several files: RomM sends them as one ZIP
};

// "192.168.4.20:3000", "http://nas:3000/" or "https://romm.example.com" as
// a base address ("http://192.168.4.20:3000"); empty when it is none.
// Without a scheme, http is assumed for a local address and https for
// anything else.
std::string romm_base(std::string_view typed);

std::string romm_platforms_url(const std::string &base);
std::string romm_games_url(const std::string &base, long long platform_id, int offset, int limit);
// The game's file (a ZIP of all of them for a multi-file game).
std::string romm_content_url(const std::string &base, const RommGame &game);
// The cover's address: a path on the server, or an address of its own.
std::string romm_cover_url(const std::string &base, const RommGame &game);
std::vector<std::string> romm_headers(const std::string &token);

bool parse_romm_platforms(std::string_view json, std::vector<RommPlatform> *platforms);
// One page of games; *total is the server's count of all of them (-1 when it
// does not say).
bool parse_romm_games(std::string_view json, const std::vector<RommPlatform> &platforms,
                      std::vector<RommGame> *games, long long *total);
// The token from an exchanged pairing code's answer, or empty.
std::string parse_romm_pairing(std::string_view json);

// The games as ORION keeps them between starts (<state>/romm-games.tsv), so
// the library lists them without asking the server.
std::string romm_catalogue_text(const std::vector<RommGame> &games);
std::vector<RommGame> read_romm_catalogue(std::string_view text);

using HttpPost = std::function<bool(const std::string &url, const std::vector<std::string> &headers,
                                    const std::string &body, std::size_t limit, std::string *answer,
                                    std::string *error)>;
using HttpDownloadWith =
    std::function<bool(const std::string &url, const std::vector<std::string> &headers,
                       const std::string &path, std::size_t limit, std::string *error)>;

// Pairing: the code RomM shows (Profile, Client tokens, Pair) exchanged for
// a token, which goes in keys.txt. Blocking. Empty when it worked, else why not.
std::string romm_pair(const HttpPost &post, const std::string &base, const std::string &code,
                      std::string *token);

// The whole catalogue, then the covers ORION has not got yet, on a thread
// of its own; the catalogue goes to <state>/romm-games.tsv, the covers to
// <state>/romm/covers/<id>.<png|jpg>.
//
//   RommSync sync(get, download);
//   sync.start(base, token, "/data/orion");
//   ...every frame: if (sync.take_finished()) the library lists them again
class RommSync
{
  public:
    struct Progress
    {
        int platforms = 0;
        int games = 0;
        int covers = 0;
        std::string error; // the first failure; empty when it all worked
    };

    RommSync(HttpGetWith get, HttpDownloadWith download);
    ~RommSync();
    RommSync(const RommSync &) = delete;
    RommSync &operator=(const RommSync &) = delete;

    bool start(std::string base, std::string token, std::string state_dir);
    bool running() const
    {
        return running_.load();
    }
    bool take_finished()
    {
        return finished_.exchange(false);
    }
    Progress progress() const;
    void cancel()
    {
        cancel_ = true;
    }
    void wait();

  private:
    void run(std::string base, std::string token, std::string state_dir);

    HttpGetWith get_;
    HttpDownloadWith download_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> cancel_{false};
    mutable std::mutex mutex_;
    Progress progress_;
};

// One file fetched on a thread of its own: a game from RomM the first time
// it is played. The file grows at <path>.part until it is whole.
//
//   FileDownload download(http_download_with);
//   download.start(url, headers, "/data/orion/romm/games/gba/Moon Garden.gba");
//   ...every frame: if (download.take_finished()) download.error() or play it
class FileDownload
{
  public:
    explicit FileDownload(HttpDownloadWith download);
    ~FileDownload();
    FileDownload(const FileDownload &) = delete;
    FileDownload &operator=(const FileDownload &) = delete;

    // False while one is running, or when the folder cannot be made.
    bool start(std::string url, std::vector<std::string> headers, std::string path);
    bool running() const
    {
        return running_.load();
    }
    bool take_finished()
    {
        return finished_.exchange(false);
    }
    // Empty when the last one worked.
    std::string error() const;
    // The bytes written so far (the .part file's size).
    std::uint64_t written() const;
    void wait();

  private:
    HttpDownloadWith download_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    mutable std::mutex mutex_;
    std::string error_;
    std::string path_;
};

// Where a game is kept once downloaded: <folder>/<system folder>/<file>
// (a multi-file game as <file>.zip, which RetroArch opens).
std::string romm_game_path(const std::string &folder, const RommGame &game);

// Where ORION keeps what comes from RomM: <state>/romm (games/, covers/).
std::string romm_folder(const std::string &state_dir);

// The catalogue's games as the library lists them (library::add_remote_games),
// with the covers the last sync brought.
std::vector<library::RemoteGame> romm_remote_games(const std::vector<RommGame> &games,
                                                   const std::string &base,
                                                   const std::string &state_dir);

} // namespace hui::orion
