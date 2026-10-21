// ORION Media Center - Update check: is a newer ORION released on GitHub?
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/updates.hpp"

#include "core/save_file.hpp"
#include "core/sha256.hpp"
#include "core/zip.hpp"
#include "library/json.hpp"

#include <cctype>
#include <cstdio>
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
            release.zip = asset["browser_download_url"].string_or("");
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
            if (!get_ || !get_(kLatestRelease, kMaxAnswer, &body, &error))
                error = error.empty() ? std::string("no network") : error;
            else if (!parse_release(body, &release))
                error = "GitHub's answer has no release in it";
            else
                state = compare_versions(release.version, current) > 0 ? State::available
                                                                       : State::up_to_date;
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

bool stage_update(std::string_view zip, const std::string &title_id, const std::string &stage,
                  std::string *error)
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
    const std::string prefix = title_id + "/";
    std::string list;
    std::string why;
    const bool read = unzip(
        zip,
        [&](const std::string &name, std::string_view data)
        {
            if (name.compare(0, prefix.size(), prefix) != 0)
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

UpdateInstaller::UpdateInstaller(HttpGet get) : get_(std::move(get))
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
    }
    running_ = true;
    worker_ = std::thread(
        [this, release = std::move(release), id = std::move(title_id), dir = std::move(update_dir)]
        {
            const auto set = [&](State state, std::string error = {})
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_ = state;
                error_ = std::move(error);
            };
            std::string zip;
            std::string sums;
            std::string error;
            const std::size_t slash = release.zip.rfind('/');
            const std::string zip_name =
                slash == std::string::npos ? release.zip : release.zip.substr(slash + 1);
            if (release.zip.empty() || release.sums.empty())
                set(State::failed, "the release has no ZIP or no SHA256SUMS");
            else if (!get_ || !get_(release.zip, kMaxZip, &zip, &error))
                set(State::failed, "the download failed: " + error);
            else if (!get_(release.sums, kMaxAnswer, &sums, &error))
                set(State::failed, "SHA256SUMS could not be read: " + error);
            else if (const std::string want = sum_for(sums, zip_name); want.empty())
                set(State::failed, "SHA256SUMS does not list " + zip_name);
            else if (sha256_hex(zip) != want)
                set(State::failed, "the download does not match SHA256SUMS");
            else
            {
                set(State::unpacking);
                save::ensure_directory(dir);
                const std::string stage = dir + "/stage";
                std::remove((stage + "/files.txt").c_str());
                if (!stage_update(zip, id, stage, &error))
                    set(State::failed, error);
                else
                {
                    save::write_atomic(stage + "/version.txt", release.version + "\n");
                    set(State::ready);
                }
            }
            running_ = false;
            finished_ = true;
        });
    return true;
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
