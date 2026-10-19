// ps5-homebrew-ui - ORION's box art downloader: libretro thumbnails for RetroArch's games.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/art_fetcher.hpp"

#include "core/save_file.hpp"
#include "library/art_match.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace hui::orion
{

namespace
{

// An index page lists every picture of a system: a few hundred kilobytes for
// the big sets. A box is rarely over a megabyte.
constexpr std::size_t kMaxIndex = 16u << 20;
constexpr std::size_t kMaxPicture = 8u << 20;
// After this many failures in a row the network is taken to be gone.
constexpr int kGiveUpAfter = 3;

void append_line(const std::string &path, const std::string &line)
{
    if (path.empty())
        return;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    const std::string text = line + "\n";
    const ssize_t written = ::write(fd, text.data(), text.size());
    (void)written;
    ::close(fd);
}

bool is_png(const std::string &bytes)
{
    return bytes.size() > 8 && bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
}

} // namespace

bool make_directories(const std::string &path)
{
    for (std::size_t at = 1; at <= path.size(); ++at)
    {
        if (at != path.size() && path[at] != '/')
            continue;
        const std::string part = path.substr(0, at);
        if (::mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
            return false;
    }
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

ArtFetcher::ArtFetcher(HttpGet get) : get_(std::move(get))
{
}

ArtFetcher::~ArtFetcher()
{
    cancel();
}

bool ArtFetcher::start(std::vector<ArtJob> jobs, std::string log_path)
{
    if (running_.load())
        return false;
    if (worker_.joinable())
        worker_.join();
    cancel_ = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        progress_ = Progress();
        progress_.total = static_cast<int>(jobs.size());
        finished_ = false;
    }
    running_ = true;
    worker_ = std::thread([this, jobs = std::move(jobs), log = std::move(log_path)]() mutable
                          { run(std::move(jobs), std::move(log)); });
    return true;
}

ArtFetcher::Progress ArtFetcher::progress() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

std::vector<ArtFound> ArtFetcher::take_found()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(found_, {});
}

bool ArtFetcher::take_finished()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(finished_, false);
}

std::vector<std::string> ArtFetcher::take_missing()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(missing_, {});
}

void ArtFetcher::cancel()
{
    cancel_ = true;
    wait();
}

void ArtFetcher::wait()
{
    if (worker_.joinable())
        worker_.join();
}

void ArtFetcher::run(std::vector<ArtJob> jobs, std::string log_path)
{
    // One index per system, fetched when its first game comes up.
    append_line(log_path, "art thread running: " + std::to_string(jobs.size()) + " games");
    std::map<std::string, std::vector<std::string>> indexes;
    std::map<std::string, std::string> index_errors;
    int failures_in_a_row = 0;
    const auto settle = [&](const ArtJob &job, int ArtFetcher::Progress::*counter,
                            const std::string &note, const std::string &error)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ++progress_.done;
        ++(progress_.*counter);
        if (!error.empty() && progress_.error.empty())
            progress_.error = error;
        if (counter == &Progress::missing)
            missing_.push_back(job.id);
        append_line(log_path, job.system + "/" + job.rom_file + ": " + note);
    };
    for (const ArtJob &job : jobs)
    {
        if (cancel_.load())
            break;
        if (failures_in_a_row >= kGiveUpAfter)
        {
            settle(job, &Progress::failed, "skipped (no network)", "");
            continue;
        }
        const std::string folder =
            std::string(kBase) + "/" + library::url_encode(job.system) + "/Named_Boxarts/";
        if (indexes.find(job.system) == indexes.end() &&
            index_errors.find(job.system) == index_errors.end())
        {
            std::string page;
            std::string error;
            if (get_(folder, kMaxIndex, &page, &error))
            {
                indexes[job.system] = library::parse_art_index(page);
                failures_in_a_row = 0;
            }
            else
            {
                index_errors[job.system] = error.empty() ? std::string("no index") : error;
                ++failures_in_a_row;
            }
        }
        if (const auto bad = index_errors.find(job.system); bad != index_errors.end())
        {
            settle(job, &Progress::failed, "index: " + bad->second, bad->second);
            continue;
        }
        const std::string picture = library::match_art(job.rom_file, indexes[job.system]);
        if (picture.empty())
        {
            settle(job, &Progress::missing, "no picture", "");
            continue;
        }
        std::string bytes;
        std::string error;
        if (!get_(folder + library::url_encode(picture), kMaxPicture, &bytes, &error))
        {
            ++failures_in_a_row;
            settle(job, &Progress::failed, picture + ": " + error, error);
            continue;
        }
        failures_in_a_row = 0;
        if (!is_png(bytes))
        {
            settle(job, &Progress::failed, picture + ": not a PNG", "not a PNG");
            continue;
        }
        const std::size_t slash = job.target.rfind('/');
        if (slash == std::string::npos || !make_directories(job.target.substr(0, slash)))
        {
            settle(job, &Progress::failed, "cannot make " + job.target,
                   "cannot write the art folder");
            continue;
        }
        const std::string written = save::write_atomic(job.target, bytes);
        if (!written.empty())
        {
            settle(job, &Progress::failed, "write: " + written, written);
            continue;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            found_.push_back({job.id, job.target});
        }
        settle(job, &Progress::found, picture, "");
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
    }
    running_ = false;
}

} // namespace hui::orion
