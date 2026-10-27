// ORION Media Center - The titles the system has registered: ShadowMountPlus's list and local FTP.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/registered_titles.hpp"

#include "core/save_file.hpp"
#include "library/library.hpp"
#include "orion/art_fetcher.hpp"
#include "orion/local_ftp.hpp"
#include "orion/shadowmount_api.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <map>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxParam = 256u << 10;
constexpr std::size_t kMaxIcon = 4u << 20;

void append_line(const std::string &path, const std::string &line)
{
    std::string text;
    save::read_file(path, &text, 64u << 10);
    // The last runs only: a short file to send when asked.
    std::size_t lines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    while (lines-- >= 40)
        text.erase(0, text.find('\n') + 1);
    save::write_atomic(path, text + std::to_string(static_cast<long long>(std::time(nullptr))) +
                                 "  " + line + "\n");
}

} // namespace

int ftp_port_of(const std::string &state_dir, int fallback)
{
    std::string text;
    if (state_dir.empty() || !save::read_file(state_dir + "/ftp-port.txt", &text, 64))
        return fallback;
    const int port = std::atoi(text.c_str());
    return port > 0 && port < 65536 ? port : fallback;
}

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
    // The port asked for first, then the other usual ones.
    std::vector<int> ports = {port};
    for (const int other : kFtpPorts)
    {
        if (std::find(ports.begin(), ports.end(), other) == ports.end())
            ports.push_back(other);
    }
    std::string tried;
    bool listed = false;
    for (const int candidate : ports)
    {
        listed = ftp.open(candidate) && ftp.list("/user/app", &apps);
        tried += (tried.empty() ? "" : ", ") + std::to_string(candidate) +
                 (listed ? " answered" : " no");
        if (listed)
        {
            result.ftp_port = candidate;
            break;
        }
        ftp.close();
        apps.clear();
    }
    if (listed)
        save::write_atomic(state_dir + "/ftp-port.txt", std::to_string(result.ftp_port) + "\n");
    int without_metadata = 0;
    // By title ID: the FTP server's (which knows each title's category from
    // its metadata) first, then ShadowMountPlus's for anything it missed.
    std::map<std::string, std::string> lines;
    const std::string icons = state_dir + "/icons";
    make_directories(icons);
    for (const FtpEntry &app : apps)
    {
        if (!app.folder || !library::is_title_id(app.name))
            continue;
        // Folders without metadata (payload placeholders) are no titles.
        library::Title title;
        std::string text;
        // Apps a payload installs as a link to its web page (Payload
        // Manager, ShadowMount's own tile) have no appmeta: their param.json
        // and icon are in /user/app/<ID>/sce_sys, so they are looked for there last.
        const std::string own = "/user/app/" + app.name + "/sce_sys/";
        const bool known =
            (ftp.get("/user/appmeta/" + app.name + "/param.json", kMaxParam, &text) &&
             library::read_param_json(text, &title)) ||
            (ftp.get("/system_data/priv/appmeta/" + app.name + "/param.sfo", kMaxParam, &text) &&
             library::read_param_sfo(text, &title)) ||
            (ftp.get(own + "param.json", kMaxParam, &text) &&
             library::read_param_json(text, &title));
        if (!known || title.id != app.name || title.name.empty())
        {
            ++without_metadata;
            continue;
        }
        lines[title.id] = library::registered_line(title);
        ++result.titles;
        const std::string icon = icons + "/" + app.name + ".png";
        struct stat info = {};
        if (::stat(icon.c_str(), &info) != 0 &&
            (ftp.get("/user/appmeta/" + app.name + "/icon0.png", kMaxIcon, &text) ||
             ftp.get(own + "icon0.png", kMaxIcon, &text)) &&
            save::write_atomic(icon, text).empty())
            ++result.icons;
    }
    ftp.close();
    // ShadowMountPlus's own list: every title it mounted or found installed,
    // backups on exFAT drives included, whether or not an FTP server runs.
    std::string answer;
    std::string error;
    std::vector<library::Title> mounted;
    const bool asked = shadowmount_request(kShadowMountPort, "/api/v1/games",
                                           R"({"include_size": false})", &answer, &error) &&
                       parse_shadowmount_games(answer, &mounted);
    for (const library::Title &title : mounted)
    {
        if (lines.count(title.id) != 0)
            continue;
        lines[title.id] = library::registered_line(title);
        ++result.titles;
        const std::string icon = icons + "/" + title.id + ".png";
        struct stat info = {};
        std::string picture;
        if (::stat(icon.c_str(), &info) != 0 &&
            shadowmount_request(kShadowMountPort, "/api/v1/games/icon?title_id=" + title.id, "",
                                &picture, &error, kMaxIcon) &&
            picture.size() > 8 && picture.compare(1, 3, "PNG") == 0 &&
            save::write_atomic(icon, picture).empty())
            ++result.icons;
    }
    // What ORION's payload read of /user/app itself: the titles no FTP
    // server was there to list.
    const std::string apps_dir = state_dir + "/" + kSystemApps;
    std::string index;
    int from_payload = 0;
    const bool payload = save::read_file(apps_dir + "/index.txt", &index, 64u << 10);
    for (std::size_t start = 0; payload && start < index.size();)
    {
        std::size_t end = index.find('\n', start);
        if (end == std::string::npos)
            end = index.size();
        const std::string id = index.substr(start, end - start);
        start = end + 1;
        if (!library::is_title_id(id) || lines.count(id) != 0)
            continue;
        library::Title title;
        std::string text;
        const std::string own = apps_dir + "/" + id;
        const bool known = (save::read_file(own + "/param.json", &text, kMaxParam) &&
                            library::read_param_json(text, &title)) ||
                           (save::read_file(own + "/param.sfo", &text, kMaxParam) &&
                            library::read_param_sfo(text, &title));
        if (!known || title.id != id || title.name.empty())
            continue;
        lines[id] = library::registered_line(title);
        ++from_payload;
        const std::string icon = icons + "/" + id + ".png";
        struct stat info = {};
        if (::stat(icon.c_str(), &info) != 0 &&
            save::read_file(own + "/icon0.png", &text, kMaxIcon) &&
            save::write_atomic(icon, text).empty())
            ++result.icons;
    }
    result.titles += from_payload;
    result.server = listed || asked || payload;
    append_line(
        state_dir + "/registered.txt",
        "FTP ports " + tried + "; /user/app: " + std::to_string(apps.size()) + " folders, " +
            std::to_string(without_metadata) + " without metadata; ShadowMountPlus: " +
            (asked ? std::to_string(mounted.size()) + " titles" : "no answer (" + error + ")") +
            "; ORION's payload: " +
            (payload ? std::to_string(from_payload) + " more" : std::string("no list yet")) + "; " +
            std::to_string(lines.size()) + " titles in all");
    if (result.server)
    {
        std::string cache;
        for (const auto &[id, line] : lines) // sorted by ID, as the cache always was
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
