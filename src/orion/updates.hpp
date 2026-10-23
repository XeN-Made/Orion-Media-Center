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
// Only this repository's releases may stand in for the newest one
// (UpdateCheck::set_source): a test release, a pre-release, a tag.
inline constexpr const char *kReleasesApi =
    "https://api.github.com/repos/XeN-Made/Orion-Media-Center/releases/";

// The release to ask for: the first line of <state_dir>/update-feed.txt when
// it names one of this repository's releases (a pre-release to try an update
// before it is the newest), else kLatestRelease.
std::string update_source(const std::string &state_dir);

struct Release
{
    std::string version;      // the tag: "01.001.000"
    std::string page;         // its page on GitHub
    std::string zip;          // the ZIP to install from; empty when it has none
    std::string sums;         // its SHA256SUMS; empty when it has none
    std::size_t zip_size = 0; // bytes, as GitHub lists it; 0 when unknown
};

// Appends "<time>  <line>" to the update log (orion's state dir,
// update/log.txt): every step of a check and an install, so a stall says
// where it stopped. Nothing when path is empty.
void update_log(const std::string &path, const std::string &line);

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

    // Where each step is written (update_log); empty for nowhere.
    void set_log(std::string path)
    {
        log_ = std::move(path);
    }
    // The release to ask about (update_source); kLatestRelease unless set.
    void set_source(std::string url)
    {
        source_ = std::move(url);
    }
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
    std::string log_;
    std::string source_ = kLatestRelease;
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
// and writes files.txt. Entries outside it, or with "..", are refused. A
// folder named `next` (ORION's next title ID, when it has one) is taken too:
// that release installs beside this ORION (concepts/library.cpp).
bool stage_update(std::string_view zip, const std::string &title_id, const std::string &stage,
                  std::string *error, const std::string &next = {});

// Fetches a URL into a file at `path` (at most limit bytes). False with
// *error set when it could not.
using HttpDownload = std::function<bool(const std::string &url, const std::string &path,
                                        std::size_t limit, std::string *error)>;

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

    UpdateInstaller(HttpGet get, HttpDownload download);
    ~UpdateInstaller();
    UpdateInstaller(const UpdateInstaller &) = delete;
    UpdateInstaller &operator=(const UpdateInstaller &) = delete;

    void set_log(std::string path)
    {
        log_ = std::move(path);
    }
    // ORION's next title ID: a release built for it is staged as well.
    void set_next_title(std::string id)
    {
        next_title_ = std::move(id);
    }
    // False while one is running.
    bool start(Release release, std::string title_id, std::string update_dir);
    // When <update_dir>/stage already holds this release, unpacked and
    // checked by an earlier start (version.txt and files.txt), it is ready
    // again without a download: true, and take_finished() says so.
    bool resume(const Release &release, const std::string &update_dir);
    State state() const;
    Release release() const;
    std::string error() const;
    // True once after it ended.
    bool take_finished();
    // Where the ZIP is written while it downloads, for its progress (its
    // size against Release::zip_size); empty before a start.
    std::string download_path() const;

  private:
    HttpGet get_;
    HttpDownload download_;
    std::string download_path_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    mutable std::mutex mutex_;
    State state_ = State::idle;
    Release release_;
    std::string error_;
    std::string log_;
    std::string next_title_;
};

} // namespace hui::orion
