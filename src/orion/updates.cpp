// ORION Media Center - Update check: is a newer ORION released on GitHub?
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/updates.hpp"

#include "core/mapped_file.hpp"
#include "core/save_file.hpp"
#include "core/sha256.hpp"
#include "core/zip.hpp"
#include "library/json.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace hui::orion
{

namespace
{

// GitHub's answer for a release is a few kilobytes, more with long notes.
constexpr std::size_t kMaxAnswer = 1u << 20;
// A release ZIP is about 39 MB.
constexpr std::size_t kMaxZip = 160u << 20;

// "01.000.010" to {1, 0, 10}; empty for anything else.
std::vector<int> version_parts(std::string_view text)
{
    std::vector<int> parts;
    int value = 0;
    int digits = 0;
    for (const char c : text)
    {
        if (c >= '0' && c <= '9')
        {
            if (++digits > 6)
                return {};
            value = value * 10 + (c - '0');
        }
        else if (c == '.' && digits > 0)
        {
            parts.push_back(value);
            value = 0;
            digits = 0;
        }
        else
            return {};
    }
    if (digits == 0)
        return {};
    parts.push_back(value);
    return parts.size() == 3 ? parts : std::vector<int>();
}

} // namespace

void update_log(const std::string &path, const std::string &line)
{
    if (path.empty())
        return;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    const std::string text =
        std::to_string(static_cast<long long>(std::time(nullptr))) + "  " + line + "\n";
    const ssize_t written = ::write(fd, text.data(), text.size());
    (void)written;
    ::close(fd);
}

bool parse_release(std::string_view text, Release *out)
{
    json::Value root;
    if (!json::parse(text, &root) || !root.is_object())
        return false;
    Release release;
    release.version = root["tag_name"].string_or("");
    if (version_parts(release.version).empty())
        return false;
    release.page = root["html_url"].string_or("");
    for (const json::Value &asset : root["assets"].elements())
    {
        const std::string name = asset["name"].string_or("");
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".zip") == 0 && release.zip.empty())
        {
            release.zip = asset["browser_download_url"].string_or("");
            release.zip_size = static_cast<std::size_t>(asset["size"].number_or(0.0));
        }
        else if (name == "SHA256SUMS")
            release.sums = asset["browser_download_url"].string_or("");
    }
    *out = std::move(release);
    return true;
}

int compare_versions(std::string_view a, std::string_view b)
{
    const std::vector<int> left = version_parts(a);
    const std::vector<int> right = version_parts(b);
    if (left.empty() || right.empty())
        return left.empty() == right.empty() ? 0 : (left.empty() ? -1 : 1);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (left[i] != right[i])
            return left[i] < right[i] ? -1 : 1;
    }
    return 0;
}

UpdateCheck::UpdateCheck(HttpGet get) : get_(std::move(get))
{
}

UpdateCheck::~UpdateCheck()
{
    if (worker_.joinable())
        worker_.join();
}

bool UpdateCheck::start(std::string current_version)
{
    if (running_.load())
        return false;
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::checking;
        error_.clear();
    }
    running_ = true;
    worker_ = std::thread(
        [this, current = std::move(current_version)]
        {
            std::string body;
            std::string error;
            Release release;
            State state = State::failed;
            update_log(log_, "check: asking GitHub (this is ORION " + current + ")");
            if (source_ != kLatestRelease)
                update_log(log_, "check: asking " + source_ + " (update-feed.txt)");
            if (!get_ || !get_(source_, kMaxAnswer, &body, &error))
                error = error.empty() ? std::string("no network") : error;
            else if (!parse_release(body, &release))
                error = "GitHub's answer has no release in it";
            else
                state = compare_versions(release.version, current) > 0 ? State::available
                                                                       : State::up_to_date;
            update_log(log_, state == State::failed ? "check: failed: " + error
                             : state == State::available
                                 ? "check: " + release.version + " is newer"
                                 : "check: " + release.version + " is not newer");
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = state;
                latest_ = std::move(release);
                error_ = std::move(error);
            }
            running_ = false;
            finished_ = true;
        });
    return true;
}

UpdateCheck::State UpdateCheck::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

Release UpdateCheck::latest() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

std::string UpdateCheck::error() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

bool UpdateCheck::take_finished()
{
    return finished_.exchange(false);
}

std::string sum_for(std::string_view sums, std::string_view file_name)
{
    // "<64 hex>  <name>" (or " *<name>" for binary mode), a line each.
    std::size_t at = 0;
    while (at < sums.size())
    {
        std::size_t end = sums.find('\n', at);
        if (end == std::string_view::npos)
            end = sums.size();
        std::string_view line = sums.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.remove_suffix(1);
        const std::size_t space = line.find(' ');
        if (space != 64)
            continue;
        std::string_view name = line.substr(space);
        while (!name.empty() && (name.front() == ' ' || name.front() == '*'))
            name.remove_prefix(1);
        if (name != file_name)
            continue;
        std::string hash(line.substr(0, 64));
        for (char &c : hash)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return hash;
    }
    return {};
}

std::string update_source(const std::string &state_dir)
{
    std::string text;
    if (state_dir.empty() || !save::read_file(state_dir + "/update-feed.txt", &text, 1024))
        return kLatestRelease;
    const std::size_t end = text.find_first_of("\r\n");
    const std::string url = text.substr(0, end);
    const std::string_view api = kReleasesApi;
    if (url.size() <= api.size() || url.compare(0, api.size(), api) != 0 ||
        url.find_first_of(" \t?#") != std::string::npos)
        return kLatestRelease;
    return url;
}

bool stage_update(std::string_view zip, const std::string &title_id, const std::string &stage,
                  std::string *error, const std::string &next)
{
    // ensure_directory makes one level: each folder of a path in turn.
    const auto folders = [&](const std::string &relative)
    {
        save::ensure_directory(stage);
        for (std::size_t slash = relative.find('/'); slash != std::string::npos;
             slash = relative.find('/', slash + 1))
        {
            if (!save::ensure_directory(stage + "/" + relative.substr(0, slash)))
                return false;
        }
        return true;
    };
    // The title folder: this ORION's, or the next title ID's, whichever the
    // release has first.
    std::string prefix;
    std::string list;
    std::string why;
    const bool read = unzip(
        zip,
        [&](const std::string &name, std::string_view data)
        {
            if (prefix.empty())
            {
                if (name.compare(0, title_id.size() + 1, title_id + "/") == 0)
                    prefix = title_id + "/";
                else if (!next.empty() && name.compare(0, next.size() + 1, next + "/") == 0)
                    prefix = next + "/";
            }
            if (prefix.empty() || name.compare(0, prefix.size(), prefix) != 0)
                return true; // beside the title folder (SHA256SUMS): not installed
            const std::string relative = name.substr(prefix.size());
            if (relative.empty() || relative.front() == '/' ||
                relative.find("..") != std::string::npos ||
                relative.find('\\') != std::string::npos ||
                relative.find('\n') != std::string::npos)
            {
                why = "the update holds an unsafe path: " + name;
                return false;
            }
            if (!folders(relative))
            {
                why = "could not make the folders for " + relative;
                return false;
            }
            const std::string failed = save::write_atomic(stage + "/" + relative, data);
            if (!failed.empty())
            {
                why = "could not write " + relative + ": " + failed;
                return false;
            }
            list += relative + "\n";
            return true;
        },
        &why);
    if (!read || list.empty())
    {
        if (error != nullptr)
            *error = why.empty() ? "the update holds no " + title_id + " folder" : why;
        return false;
    }
    const std::string failed = save::write_atomic(stage + "/files.txt", list);
    if (!failed.empty())
    {
        if (error != nullptr)
            *error = "could not write files.txt: " + failed;
        return false;
    }
    return true;
}

UpdateInstaller::UpdateInstaller(HttpGet get, HttpDownload download)
    : get_(std::move(get)), download_(std::move(download))
{
}

UpdateInstaller::~UpdateInstaller()
{
    if (worker_.joinable())
        worker_.join();
}

bool UpdateInstaller::start(Release release, std::string title_id, std::string update_dir)
{
    if (running_.load())
        return false;
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::downloading;
        release_ = release;
        error_.clear();
        download_path_ = update_dir + "/" + title_id + ".zip.part";
    }
    running_ = true;
    worker_ = std::thread(
        [this, release = std::move(release), id = std::move(title_id), dir = std::move(update_dir),
         next = next_title_]
        {
            const auto set = [&](State state, std::string error = {})
            {
                static constexpr const char *kNames[] = {"idle", "downloading", "unpacking",
                                                         "ready", "failed"};
                update_log(log_, std::string("install: ") + kNames[static_cast<int>(state)] +
                                     (error.empty() ? "" : ": " + error));
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = state;
                error_ = std::move(error);
            };
            // Each step says where it is in the log first, so a stall shows.
            const auto run = [&]
            {
                update_log(log_, "install: " + release.version + " from " + release.zip);
                if (release.zip.empty() || release.sums.empty())
                    return set(State::failed, "the release has no ZIP or no SHA256SUMS");
                const std::size_t slash = release.zip.rfind('/');
                const std::string zip_name =
                    slash == std::string::npos ? release.zip : release.zip.substr(slash + 1);
                std::string error;
                std::string sums;
                update_log(log_, "install: reading SHA256SUMS");
                if (!get_ || !get_(release.sums, kMaxAnswer, &sums, &error))
                    return set(State::failed, "SHA256SUMS could not be read: " + error);
                const std::string want = sum_for(sums, zip_name);
                if (want.empty())
                    return set(State::failed, "SHA256SUMS does not list " + zip_name);
                // The ZIP goes straight to a file and is read through a
                // mapping: held in memory it would not fit the app's heap.
                save::ensure_directory(dir);
                const std::string zip_path = dir + "/" + id + ".zip";
                update_log(log_, "install: downloading to " + zip_path);
                if (!download_ || !download_(release.zip, zip_path, kMaxZip, &error))
                    return set(State::failed, "the download failed: " + error);
                MappedFile zip;
                if (!zip.open(zip_path, &error))
                    return set(State::failed, error);
                update_log(log_, "install: downloaded " + std::to_string(zip.view().size()) +
                                     " bytes; checking its SHA-256");
                if (sha256_hex(zip.view()) != want)
                    return set(State::failed, "the download does not match SHA256SUMS");
                set(State::unpacking);
                const std::string stage = dir + "/stage";
                std::remove((stage + "/files.txt").c_str());
                if (!stage_update(zip.view(), id, stage, &error, next))
                    return set(State::failed, error);
                save::write_atomic(stage + "/version.txt", release.version + "\n");
                std::remove(zip_path.c_str()); // unpacked: no longer needed
                set(State::ready);
            };
            run();
            running_ = false;
            finished_ = true;
        });
    return true;
}

bool UpdateInstaller::resume(const Release &release, const std::string &update_dir)
{
    if (running_.load())
        return false;
    std::string staged;
    std::string list;
    if (!save::read_file(update_dir + "/stage/version.txt", &staged, 64) ||
        !save::read_file(update_dir + "/stage/files.txt", &list, 1u << 20) || list.empty())
        return false;
    while (!staged.empty() && (staged.back() == '\n' || staged.back() == '\r'))
        staged.pop_back();
    if (staged != release.version)
        return false;
    update_log(log_, "install: " + release.version + " is already unpacked; ready again");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::ready;
        release_ = release;
        error_.clear();
    }
    finished_ = true;
    return true;
}

std::string UpdateInstaller::download_path() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return download_path_;
}

UpdateInstaller::State UpdateInstaller::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

Release UpdateInstaller::release() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return release_;
}

std::string UpdateInstaller::error() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

bool UpdateInstaller::take_finished()
{
    return finished_.exchange(false);
}

} // namespace hui::orion
