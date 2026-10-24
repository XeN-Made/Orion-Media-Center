// ORION Media Center - ORION's box art downloader: libretro thumbnails for RetroArch's games.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/art_fetcher.hpp"

#include "core/save_file.hpp"
#include "core/zip.hpp"
#include "library/art_match.hpp"
#include "library/json.hpp"
#include "orion/wide_copies.hpp"

#include <algorithm>
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

// GameTDB's covers are JPEG. They keep the target's .png name, where
// attach_rom_art looks; the decoder goes by the bytes, not the name.
bool is_picture(const std::string &bytes)
{
    return is_png(bytes) || (bytes.size() > 3 && bytes.compare(0, 3, "\xff\xd8\xff", 3) == 0);
}

// XboxUnity, where PS5X360 (and Aurora) find covers, by the game's title ID.
constexpr const char *kXboxUnity = "https://xboxunity.net/Resources/Lib/";
constexpr const char *kXbox360 = "Microsoft - Xbox 360";

// GameTDB's Switch list: about 3.5 MB zipped, read once, then kept as a
// small index beside the Switch covers.
constexpr const char *kSwitchList = "https://www.gametdb.com/switchtdb.zip?LANG=EN";
constexpr std::size_t kMaxSwitchIndex = 4u << 20;

} // namespace

std::string pick_xboxunity_cover(std::string_view json)
{
    json::Value root;
    if (!json::parse(json, &root) || !root.is_object())
        return "";
    std::string best;
    int best_score = -1;
    for (const json::Value &cover : root["Covers"].elements())
    {
        const std::string id = cover["CoverID"].string_or("");
        if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos)
            continue;
        // Ratings are 1 to 5; an official scan outranks any of them.
        const std::string rating = cover["Rating"].string_or("");
        int score =
            rating.size() == 1 && rating[0] >= '0' && rating[0] <= '9' ? rating[0] - '0' : 0;
        if (cover["Official"].string_or("") == "1")
            score += 10;
        if (score > best_score)
        {
            best = id;
            best_score = score;
        }
    }
    return best;
}

bool front_of_sleeve(const library::Image &sleeve, library::Image *front)
{
    *front = library::Image();
    if (sleeve.width <= 0 || sleeve.height <= 0 ||
        sleeve.rgba.size() != static_cast<std::size_t>(sleeve.width) * sleeve.height * 4)
        return false;
    if (sleeve.width <= sleeve.height)
    {
        *front = sleeve;
        return true;
    }
    // A case's front is 13.5 by 19 cm.
    const int width = std::min(sleeve.width, (sleeve.height * 135 + 95) / 190);
    front->width = width;
    front->height = sleeve.height;
    front->rgba.resize(static_cast<std::size_t>(width) * sleeve.height * 4);
    const std::size_t row = static_cast<std::size_t>(width) * 4;
    const std::size_t from = static_cast<std::size_t>(sleeve.width - width) * 4;
    for (int y = 0; y < sleeve.height; ++y)
    {
        const std::size_t line = static_cast<std::size_t>(y) * sleeve.width * 4;
        std::copy_n(sleeve.rgba.begin() + static_cast<std::ptrdiff_t>(line + from), row,
                    front->rgba.begin() + static_cast<std::ptrdiff_t>(y * row));
    }
    return true;
}

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

const std::string &ArtFetcher::switch_ids(const ArtJob &job, const std::string &log_path)
{
    if (switch_loaded_)
        return switch_index_;
    switch_loaded_ = true;
    const std::size_t slash = job.target.rfind('/');
    const std::string folder = slash == std::string::npos ? "." : job.target.substr(0, slash);
    const std::string cache = folder + "/gametdb-switch.tsv";
    if (save::read_file(cache, &switch_index_, kMaxSwitchIndex) && !switch_index_.empty())
        return switch_index_;
    switch_index_.clear();
    std::string archive;
    std::string error;
    if (!get_(kSwitchList, kMaxIndex, &archive, &error))
    {
        append_line(log_path, "GameTDB's Switch list: " + error);
        return switch_index_;
    }
    unzip(
        archive,
        [&](const std::string &name, std::string_view data)
        {
            if (name == "switchtdb.xml")
                switch_index_ = library::gametdb_title_index(data);
            return true;
        },
        &error);
    std::string().swap(archive);
    if (!switch_index_.empty() && make_directories(folder))
        save::write_atomic(cache, switch_index_);
    append_line(log_path,
                "GameTDB's Switch list: " +
                    std::to_string(std::count(switch_index_.begin(), switch_index_.end(), '\n')) +
                    " titles" + (error.empty() ? "" : " (" + error + ")"));
    return switch_index_;
}

void ArtFetcher::run(std::vector<ArtJob> jobs, std::string log_path)
{
    switch_loaded_ = false;
    switch_index_.clear();
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
    // A picture downloaded: written where the library looks, and reported.
    const auto keep = [&](const ArtJob &job, const std::string &bytes, const std::string &note)
    {
        const std::size_t slash = job.target.rfind('/');
        if (slash == std::string::npos || !make_directories(job.target.substr(0, slash)))
        {
            settle(job, &Progress::failed, "cannot make " + job.target,
                   "cannot write the art folder");
            return;
        }
        const std::string written = save::write_atomic(job.target, bytes);
        if (!written.empty())
        {
            settle(job, &Progress::failed, "write: " + written, written);
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            found_.push_back({job.id, job.target});
        }
        settle(job, &Progress::found, note, "");
    };
    // The last source: SteamGridDB, by the game's name, when the player has
    // a key. True when it had a cover (written and reported by keep).
    const std::string grid_key = grid_key_;
    const HttpGetWith grid_get = grid_get_;
    const auto from_grid = [&](const ArtJob &job)
    {
        if (grid_key.empty() || !grid_get)
            return false;
        const std::size_t dot = job.rom_file.rfind('.');
        const std::string name = job.name.empty() ? job.rom_file.substr(0, dot) : job.name;
        std::vector<GridCover> covers;
        std::string error;
        std::string bytes;
        if (!find_grid_covers(grid_get, grid_key, name, 1, &covers, &error) || covers.empty() ||
            !get_(covers.front().url, kMaxPicture, &bytes, &error) || !is_picture(bytes))
        {
            append_line(log_path, job.system + "/" + job.rom_file + ": SteamGridDB: " +
                                      (error.empty() ? std::string("no cover") : error));
            return false;
        }
        keep(job, bytes, "SteamGridDB: " + covers.front().url);
        return true;
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
        // XboxUnity by the Xbox 360 game's title ID; a miss goes on to the
        // other sources.
        if (job.system == kXbox360 && !job.serial.empty())
        {
            std::string info;
            std::string error;
            std::string bytes;
            library::Image sleeve;
            library::Image front;
            if (!get_(std::string(kXboxUnity) + "CoverInfo.php?titleid=" + job.serial, kMaxIndex,
                      &info, &error))
                append_line(log_path, job.system + "/" + job.rom_file + ": XboxUnity: " + error);
            else if (const std::string cover = pick_xboxunity_cover(info); cover.empty())
                append_line(log_path, job.system + "/" + job.rom_file +
                                          ": XboxUnity has no cover for " + job.serial);
            else if (get_(std::string(kXboxUnity) + "Cover.php?size=large&cid=" + cover,
                          kMaxPicture, &bytes, &error) &&
                     is_picture(bytes) && library::decode_png(bytes, 2048, &sleeve) &&
                     front_of_sleeve(sleeve, &front))
            {
                failures_in_a_row = 0;
                // The decoder goes by the bytes (see is_picture).
                keep(job, encode_tga(front), "XboxUnity: cover " + cover);
                continue;
            }
            else
                append_line(log_path, job.system + "/" + job.rom_file + ": XboxUnity cover " +
                                          cover + ": " +
                                          (error.empty() ? std::string("not a picture") : error));
        }
        // GameTDB by the game's ID: what PS5-ECAM names its games by, and the
        // only covers for Switch games (found by name, see switch_ids).
        if (const std::string platform = library::gametdb_platform(job.system);
            !platform.empty() && (!job.serial.empty() || platform == "switch"))
        {
            std::string id = job.serial;
            if (id.empty())
                id = library::gametdb_id(switch_ids(job, log_path), job.name);
            if (id.empty())
            {
                if (!from_grid(job))
                    settle(job, &Progress::missing, "no GameTDB ID for \"" + job.name + "\"", "");
                continue;
            }
            std::string bytes;
            std::string used;
            for (const std::string &url : library::gametdb_covers(platform, id))
            {
                std::string error;
                if (get_(url, kMaxPicture, &bytes, &error) && is_picture(bytes))
                {
                    used = url;
                    break;
                }
            }
            if (used.empty())
            {
                if (!from_grid(job))
                    settle(job, &Progress::missing, "GameTDB has no cover for " + id, "");
                continue;
            }
            failures_in_a_row = 0;
            keep(job, bytes, used);
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
            if (!from_grid(job))
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
        keep(job, bytes, picture);
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
    }
    running_ = false;
}

} // namespace hui::orion
