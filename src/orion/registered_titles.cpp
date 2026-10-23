// ORION Media Center - The titles the system has registered, read through local FTP.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/registered_titles.hpp"

#include "core/save_file.hpp"
#include "library/library.hpp"
#include "orion/art_fetcher.hpp"
#include "orion/local_ftp.hpp"

#include <algorithm>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxParam = 256u << 10;
constexpr std::size_t kMaxIcon = 4u << 20;

} // namespace

RegisteredTitles::~RegisteredTitles()
{
    wait();
}

bool RegisteredTitles::start(std::string state_dir, int port)
{
    if (running_.load())
        return false;
    wait();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result_ = Result();
        finished_ = false;
    }
    running_ = true;
    worker_ = std::thread([this, state_dir = std::move(state_dir), port]() mutable
                          { run(std::move(state_dir), port); });
    return true;
}

bool RegisteredTitles::take_finished()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(finished_, false);
}

RegisteredTitles::Result RegisteredTitles::result() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return result_;
}

void RegisteredTitles::wait()
{
    if (worker_.joinable())
        worker_.join();
}

void RegisteredTitles::run(std::string state_dir, int port)
{
    Result result;
    LocalFtp ftp;
    std::vector<FtpEntry> apps;
    result.server = ftp.open(port) && ftp.list("/user/app", &apps);
    std::vector<std::string> lines;
    const std::string icons = state_dir + "/icons";
    make_directories(icons);
    for (const FtpEntry &app : apps)
    {
        if (!app.folder || !library::is_title_id(app.name))
            continue;
        // Folders without metadata (payload placeholders) are no titles.
        library::Title title;
        std::string text;
        const bool known =
            (ftp.get("/user/appmeta/" + app.name + "/param.json", kMaxParam, &text) &&
             library::read_param_json(text, &title)) ||
            (ftp.get("/system_data/priv/appmeta/" + app.name + "/param.sfo", kMaxParam, &text) &&
             library::read_param_sfo(text, &title));
        if (!known || title.id != app.name || title.name.empty())
            continue;
        lines.push_back(library::registered_line(title));
        ++result.titles;
        const std::string icon = icons + "/" + app.name + ".png";
        struct stat info = {};
        if (::stat(icon.c_str(), &info) != 0 &&
            ftp.get("/user/appmeta/" + app.name + "/icon0.png", kMaxIcon, &text) &&
            save::write_atomic(icon, text).empty())
            ++result.icons;
    }
    ftp.close();
    if (result.server)
    {
        std::sort(lines.begin(), lines.end());
        std::string cache;
        for (const std::string &line : lines)
            cache += line;
        std::string before;
        save::read_file(state_dir + "/registered.tsv", &before, 4u << 20);
        result.changed = cache != before;
        if (result.changed)
            save::write_atomic(state_dir + "/registered.tsv", cache);
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result_ = result;
        finished_ = true;
    }
    running_ = false;
}

} // namespace hui::orion
