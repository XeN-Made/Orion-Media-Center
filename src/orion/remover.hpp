// ps5-homebrew-ui - Removing a game through ShadowMountPlus: off the home screen, or deleted.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace hui::orion
{

// ShadowMountPlus registers the console's homebrew games and keeps an API on
// the console (127.0.0.1:10101, JSON over POST, "status": 0 when it worked):
//
//   /api/v1/games/uninstall       {"title_id"}: off the home screen; the
//                                 game's folder or image stays
//   /api/v1/games/delete          {"title_id", "confirm": true}: starts a job
//                                 that erases the folder or image for good
//   /api/v1/games/storage/status  {}: that job (active, processed_bytes,
//                                 total_bytes, result_status, result_error)
//
// A Remover runs one removal at a time on a thread of its own. Deleting is
// delete, then the job watched to its end, then uninstall, so no icon is left
// behind for a game that is gone.
//
//   Remover remover([](auto path, auto json, auto *reply, auto *error)
//                   { return post(path, json, reply, error); });
//   remover.start(Remover::Kind::erase, "PPSA01234", "Some Game");
//   ... each frame:
//   const Remover::Progress now = remover.progress();
//   if (remover.take_finished()) report(now);
class Remover
{
  public:
    enum class Kind : std::uint8_t
    {
        home_screen, // uninstall only: the files stay
        erase,       // delete the files, then uninstall
    };

    // POSTs json to path on the API, the reply's body into *reply. False with
    // *error set when the API could not be reached.
    using Post = std::function<bool(const std::string &path, const std::string &json,
                                    std::string *reply, std::string *error)>;

    struct Progress
    {
        Kind kind = Kind::home_screen;
        std::string title_id;
        std::string name;
        bool running = false;
        bool finished = false; // the last run ended
        bool ok = false;       // ... and did what it was asked
        std::string error;     // why not, for the player
        std::uint64_t done_bytes = 0;
        std::uint64_t total_bytes = 0;
    };

    explicit Remover(Post post);
    ~Remover();
    Remover(const Remover &) = delete;
    Remover &operator=(const Remover &) = delete;

    // False while another removal runs.
    bool start(Kind kind, std::string title_id, std::string name);
    Progress progress() const;
    bool running() const
    {
        return running_.load();
    }
    // True once after a run ended.
    bool take_finished();
    // How long to wait between looks at a delete job (tests set 0).
    int poll_ms = 1000;
    // Waits for the run (tests).
    void wait();

  private:
    void run(Kind kind, std::string title_id);
    // One call: true when the API said status 0; its reply in *reply.
    bool call(const std::string &path, const std::string &json, std::string *reply,
              std::string *error);

    Post post_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    mutable std::mutex mutex_;
    Progress progress_;
};

} // namespace hui::orion
