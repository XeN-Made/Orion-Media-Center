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
    std::string sums;    // its SHA256SUMS; empty when it has none
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

// ---- installing an update ----
//
// ORION cannot replace its own files while it runs. It downloads the
// release's ZIP, checks it against the release's SHA256SUMS, and unpacks
// its title folder into <update_dir>/stage, with files.txt (one path a line,
// relative to the title folder) and version.txt. The launch helper copies
// those files over the installed ones once ORION has closed, keeps the old
// ones in <update_dir>/previous, and opens ORION again (payload/launcher,
// "@update"); how it went is in <update_dir>/result.txt.

// The hash `sums` (a SHA256SUMS file) gives for `file_name`, lowercase; empty
// when it names no such file.
std::string sum_for(std::string_view sums, std::string_view file_name);

// Unpacks the title folder of a release ZIP ("PPSA99061/...") into `stage`
// and writes files.txt. Entries outside it, or with "..", are refused.
bool stage_update(std::string_view zip, const std::string &title_id, const std::string &stage,
                  std::string *error);

class UpdateInstaller
{
  public:
    enum class State
    {
        idle,
        downloading,
        unpacking,
        ready, // staged: <update_dir>/stage holds the new files
        failed,
    };

    explicit UpdateInstaller(HttpGet get);
    ~UpdateInstaller();
    UpdateInstaller(const UpdateInstaller &) = delete;
    UpdateInstaller &operator=(const UpdateInstaller &) = delete;

    // False while one is running.
    bool start(Release release, std::string title_id, std::string update_dir);
    State state() const;
    Release release() const;
    std::string error() const;
    // True once after it ended.
    bool take_finished();

  private:
    HttpGet get_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    mutable std::mutex mutex_;
    State state_ = State::idle;
    Release release_;
    std::string error_;
};

} // namespace hui::orion
