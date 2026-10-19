// ps5-homebrew-ui - ORION's box art downloader: libretro thumbnails for RetroArch's games.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hui::orion
{

// One game that has no picture yet.
struct ArtJob
{
    std::string id;       // the Title's id ("rom-...")
    std::string system;   // "Nintendo - Nintendo DS": the thumbnail set's folder
    std::string rom_file; // "3055 - Chrono Trigger (U)(XenoPhobia).nds"
    std::string target;   // where the PNG goes: <state>/art/<system>/<rom stem>.png
};

struct ArtFound
{
    std::string id;
    std::string path;
};

// Fetches a URL into *body (at most limit bytes). False with *error set when
// it could not; a missing page is an error too.
using HttpGet = std::function<bool(const std::string &url, std::size_t limit, std::string *body,
                                   std::string *error)>;

// Downloads box art on a thread of its own, so the library keeps moving while
// it works: per system, one index page of Named_Boxarts, then one picture per
// game it matches (library/art_match.hpp). Pictures are written where
// library::attach_rom_art looks first, so the next start finds them without
// the network.
//
//   ArtFetcher art(http_get);
//   art.start(jobs, "/data/orion/art.txt");
//   ...every frame:
//   for (const ArtFound &found : art.take_found()) show(found.id, found.path);
//   if (art.take_finished()) report(art.progress());
class ArtFetcher
{
  public:
    static constexpr const char *kBase = "https://thumbnails.libretro.com";

    struct Progress
    {
        int total = 0;
        int done = 0;      // looked for, found or not
        int found = 0;     // downloaded
        int missing = 0;   // no picture in the set
        int failed = 0;    // the network or the console said no
        std::string error; // the first failure, for the report
    };

    explicit ArtFetcher(HttpGet get);
    ~ArtFetcher();
    ArtFetcher(const ArtFetcher &) = delete;
    ArtFetcher &operator=(const ArtFetcher &) = delete;

    // False while a run is going (the jobs are not taken). log_path, when
    // not empty, gets a line per game.
    bool start(std::vector<ArtJob> jobs, std::string log_path);
    bool running() const
    {
        return running_.load();
    }
    Progress progress() const;
    // Pictures written since the last call.
    std::vector<ArtFound> take_found();
    // True once after a run ended.
    bool take_finished();
    // The ids a run found nothing for (missing, not failed): not worth asking
    // again on every start.
    std::vector<std::string> take_missing();
    // Stops after the request in flight; waits for the thread.
    void cancel();
    // Waits for the run to end (tests).
    void wait();

  private:
    void run(std::vector<ArtJob> jobs, std::string log_path);

    HttpGet get_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    mutable std::mutex mutex_;
    Progress progress_;
    std::vector<ArtFound> found_;
    std::vector<std::string> missing_;
    bool finished_ = false;
};

// Creates a folder and its parents; true when it exists afterwards.
bool make_directories(const std::string &path);

} // namespace hui::orion
