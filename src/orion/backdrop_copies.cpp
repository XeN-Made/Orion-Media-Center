// ps5-homebrew-ui - Copies of the system's home-screen pictures, fetched through local FTP.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/backdrop_copies.hpp"

#include "core/save_file.hpp"
#include "orion/art_fetcher.hpp"
#include "orion/local_ftp.hpp"

#include <utility>

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

bool BackdropCopies::start(std::vector<std::string> ids, std::string folder, int port)
{
    if (running_.load())
        return false;
    wait();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result_ = Result();
    }
    running_ = true;
    worker_ = std::thread([this, ids = std::move(ids), folder = std::move(folder), port]() mutable
                          { run(std::move(ids), std::move(folder), port); });
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

void BackdropCopies::run(std::vector<std::string> ids, std::string folder, int port)
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
        for (const char *name : {"/pic0.png", "/pic1.png"})
        {
            if (!got && ftp.get("/user/appmeta/" + id + name, kMaxPicture, &bytes))
                got = save::write_atomic(folder + "/" + id + ".png", bytes).empty();
        }
        if (got)
            ++result.copied;
        else if (save::write_atomic(folder + "/" + id + ".none", "").empty())
            ++result.none;
    }
    ftp.close();
    result.finished = true;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        result_ = result;
    }
    running_ = false;
}

} // namespace hui::orion
