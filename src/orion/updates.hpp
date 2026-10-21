// ORION Media Center - Update check: is a newer ORION released on GitHub?
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "orion/art_fetcher.hpp" // HttpGet

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace hui::orion
{

// The newest release, as GitHub's API describes it. Every release's tag is
// the version in its param.json ("01.001.000"; .github/workflows/tooling.yml
// refuses any other), so tags and ORION's own version compare directly.
inline constexpr const char *kLatestRelease =
    "https://api.github.com/repos/XeN-Made/Orion-Media-Center/releases/latest";
inline constexpr const char *kReleasesPage = "github.com/XeN-Made/Orion-Media-Center/releases";

struct Release
{
    std::string version; // the tag: "01.001.000"
    std::string page;    // its page on GitHub
    std::string zip;     // the ZIP to install from; empty when it has none
};

// Reads GitHub's answer for one release. False when it is not one (no tag).
bool parse_release(std::string_view json, Release *out);

// Orders two versions "NN.NNN.NNN" part by part, as numbers: < 0, 0 or > 0.
// Anything that is not a version sorts first, so it never looks newer.
int compare_versions(std::string_view a, std::string_view b);

// Asks GitHub once, on a thread of its own (the request blocks for as long
// as the network takes); the library reads the answer when it is there.
//
//   UpdateCheck updates(http_get);
//   updates.start("01.000.010");
//   ...every frame:
//   if (updates.take_finished()) show(updates.state(), updates.latest());
class UpdateCheck
{
  public:
    enum class State
    {
        idle,
        checking,
        up_to_date,
        available, // latest() is newer than this ORION
        failed,    // error() says why
    };

    explicit UpdateCheck(HttpGet get);
    ~UpdateCheck();
    UpdateCheck(const UpdateCheck &) = delete;
    UpdateCheck &operator=(const UpdateCheck &) = delete;

    // False while a check is running.
    bool start(std::string current_version);
    State state() const;
    Release latest() const;
    std::string error() const;
    // True once after a check ended.
    bool take_finished();

  private:
    HttpGet get_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    mutable std::mutex mutex_;
    State state_ = State::idle;
    Release latest_;
    std::string error_;
};

} // namespace hui::orion
