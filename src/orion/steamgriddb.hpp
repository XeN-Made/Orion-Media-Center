// ORION Media Center - SteamGridDB: covers for games the other box art sources lack.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <string_view>
#include <vector>

namespace hui::orion
{

// SteamGridDB (steamgriddb.com) keeps covers made by its community, for
// games on every system and for homebrew. Its API wants a key, and each
// player brings their own (free, from their profile there): none is built
// into ORION. Keys of other services live beside it in <state>/keys.txt:
//
//   # ORION's keys for other services, one a line.
//   steamgriddb = 0123456789abcdef0123456789abcdef
//
// Covers come as PNG or JPEG only: the image decoder reads no WebP.

// A key from <state_dir>/keys.txt; empty when there is none.
std::string read_service_key(const std::string &state_dir, std::string_view service);
// Writes (or, empty, removes) a key, keeping the file's other lines. Empty
// when it worked, else why not.
std::string write_service_key(const std::string &state_dir, std::string_view service,
                              std::string_view key);

inline constexpr const char *kSteamGridDb = "steamgriddb";

// One cover: the picture, and a small copy of it.
struct GridCover
{
    std::string url;
    std::string thumb;
};

// What a name is searched by: a ROM's tags ("(USA)", "[!]") and extra
// spaces left out, so "Chrono Trigger (USA) [!]" finds Chrono Trigger.
std::string grid_query(std::string_view name);
// The API's addresses: games by name, and a game's covers (portrait, PNG or
// JPEG, still, neither NSFW nor joke covers).
std::string grid_search_url(std::string_view name);
std::string grid_covers_url(long long game);
// Its answers: the games' IDs, best first; the covers. False when the answer
// is not a successful one.
bool parse_grid_search(std::string_view json, std::vector<long long> *games);
bool parse_grid_covers(std::string_view json, std::vector<GridCover> *covers);

// Fetches a URL with request headers into *body (at most limit bytes).
using HttpGetWith =
    std::function<bool(const std::string &url, const std::vector<std::string> &headers,
                       std::size_t limit, std::string *body, std::string *error)>;

// Up to `most` covers for a name: the best-matching game's. Blocking (call
// it from a worker thread). False with *error when SteamGridDB could not be
// asked; true with no covers when it knows no such game.
bool find_grid_covers(const HttpGetWith &get, const std::string &key, std::string_view name,
                      std::size_t most, std::vector<GridCover> *covers, std::string *error);

// Find other art on a title: its covers on SteamGridDB, on a thread of
// their own. The small copies arrive one by one (to look through on the
// title itself); the one the player keeps is then fetched in full.
//
//   GridPicker picker(http_get_with);
//   picker.search(key, "Moon Garden", "/data/orion/art/picks", "rom-…");
//   ...every frame: show picker.take_ready(); when the player keeps one:
//   picker.keep(index, "/data/orion/custom-art/rom-…");   // + its extension
//   if (picker.take_finished()) ... picker.kept() or picker.error()
class GridPicker
{
  public:
    static constexpr std::size_t kMostCovers = 8;

    explicit GridPicker(HttpGetWith get);
    ~GridPicker();
    GridPicker(const GridPicker &) = delete;
    GridPicker &operator=(const GridPicker &) = delete;

    // Small copies go to <folder>/<id>-<n>.<png|jpg>. False while busy.
    bool search(std::string key, std::string name, std::string folder, std::string id);
    // Cover `index` in full, to <target_stem>.<png|jpg>. False while busy.
    bool keep(std::size_t index, std::string target_stem);
    bool busy() const
    {
        return running_.load();
    }
    // Stops a search after the download under way: the player has gone on
    // (each small copy can take a minute on a slow line).
    void cancel()
    {
        cancelled_ = true;
    }
    // Small copies written since the last call: (index, path).
    std::vector<std::pair<std::size_t, std::string>> take_ready();
    // The covers the search found (0 until it has an answer).
    std::size_t count() const;
    // True once after a search or a keep ended.
    bool take_finished();
    std::string error() const;
    // Where keep wrote the picture; empty when it did not.
    std::string kept() const;
    // Waits for the thread (tests, and before the picker goes).
    void wait();

  private:
    void begin(std::function<void()> work);

    HttpGetWith get_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelled_{false};
    mutable std::mutex mutex_;
    std::vector<GridCover> covers_;
    std::vector<std::pair<std::size_t, std::string>> ready_;
    std::string error_;
    std::string kept_;
    bool finished_ = false;
};

} // namespace hui::orion
