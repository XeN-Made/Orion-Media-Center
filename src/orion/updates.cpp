// ORION Media Center - Update check: is a newer ORION released on GitHub?
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/updates.hpp"

#include "library/json.hpp"

#include <utility>
#include <vector>

namespace hui::orion
{

namespace
{

// GitHub's answer for a release is a few kilobytes, more with long notes.
constexpr std::size_t kMaxAnswer = 1u << 20;

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
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".zip") == 0)
        {
            release.zip = asset["browser_download_url"].string_or("");
            break;
        }
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

} // namespace hui::orion
