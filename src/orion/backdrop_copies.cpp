// ORION Media Center - Copies of the system's home-screen pictures, fetched through local FTP.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/backdrop_copies.hpp"

#include "core/save_file.hpp"
#include "orion/art_fetcher.hpp"
#include "orion/local_ftp.hpp"

#include <cstdint>
#include <cstdio>
#include <utility>

#include <sys/stat.h>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxPicture = 24u << 20; // bytes of one 4K home-screen picture

} // namespace

BackdropCopies::~BackdropCopies()
{
    wait();
}

bool BackdropCopies::start(std::vector<std::string> ids, std::string folder, int port,
                           std::vector<std::string> pictures, std::string extension)
{
    if (running_.load())
        return false;
    wait();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result_ = Result();
    }
    running_ = true;
    worker_ = std::thread(
        [this, ids = std::move(ids), folder = std::move(folder), port,
         pictures = std::move(pictures), extension = std::move(extension)]() mutable {
            run(std::move(ids), std::move(folder), port, std::move(pictures), std::move(extension));
        });
    return true;
}

BackdropCopies::Result BackdropCopies::result() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return result_;
}

void BackdropCopies::wait()
{
    if (worker_.joinable())
        worker_.join();
}

void BackdropCopies::run(std::vector<std::string> ids, std::string folder, int port,
                         std::vector<std::string> pictures, std::string extension)
{
    Result result;
    LocalFtp ftp;
    result.server = ftp.open(port) && make_directories(folder);
    for (const std::string &id : ids)
    {
        if (!result.server)
            break;
        std::string bytes;
        bool got = false;
        // A copy made before is fetched again only when the system's picture
        // is another size now: a tool like Art Swap replaced it.
        const std::string copy = folder + "/" + id + extension;
        struct stat info = {};
        if (::stat(copy.c_str(), &info) == 0)
        {
            std::vector<FtpEntry> entries;
            if (!ftp.list("/user/appmeta/" + id, &entries))
                continue;
            std::uint64_t now = 0;
            for (const std::string &name : pictures)
            {
                for (const FtpEntry &entry : entries)
                {
                    if (now == 0 && "/" + entry.name == name)
                        now = entry.size;
                }
            }
            if (now == 0 || now == static_cast<std::uint64_t>(info.st_size))
                continue;
        }
        for (const std::string &name : pictures)
        {
            if (!got && ftp.get("/user/appmeta/" + id + name, kMaxPicture, &bytes))
                got = save::write_atomic(copy, bytes).empty();
        }
        if (got)
            ++result.copied;
        else if (save::write_atomic(folder + "/" + id + ".none", "").empty())
            ++result.none;
    }
    ftp.close();
    // The run ended: the next start may fetch again (see request_backdrops).
    std::remove((folder + "/.fetching").c_str());
    result.finished = true;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result_ = result;
    }
    running_ = false;
}

} // namespace hui::orion
