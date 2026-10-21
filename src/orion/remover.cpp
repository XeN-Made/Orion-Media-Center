// ps5-homebrew-ui - Removing a game through ShadowMountPlus: off the home screen, or deleted.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/remover.hpp"

#include "library/json.hpp"

#include <chrono>
#include <utility>

namespace hui::orion
{

namespace
{

constexpr int kMaxPolls = 6 * 60 * 60; // a delete job is given up on after six hours of looks

} // namespace

Remover::Remover(Post post) : post_(std::move(post))
{
}

Remover::~Remover()
{
    wait();
}

bool Remover::start(Kind kind, std::string title_id, std::string name)
{
    if (running_.load())
        return false;
    wait();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        progress_ = Progress();
        progress_.kind = kind;
        progress_.title_id = title_id;
        progress_.name = std::move(name);
        progress_.running = true;
    }
    finished_ = false;
    running_ = true;
    worker_ = std::thread([this, kind, id = std::move(title_id)]() mutable { run(kind, id); });
    return true;
}

Remover::Progress Remover::progress() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

bool Remover::take_finished()
{
    return finished_.exchange(false);
}

void Remover::wait()
{
    if (worker_.joinable())
        worker_.join();
}

bool Remover::call(const std::string &path, const std::string &json, std::string *reply,
                   std::string *error)
{
    if (!post_(path, json, reply, error))
        return false;
    json::Value value;
    if (!json::parse(*reply, &value) || !value.is_object())
    {
        *error = "ShadowMountPlus gave an answer ORION does not understand";
        return false;
    }
    if (value["status"].number_or(-1.0) != 0.0)
    {
        *error = value["error"].string_or("ShadowMountPlus said no");
        return false;
    }
    return true;
}

void Remover::run(Kind kind, std::string title_id)
{
    const std::string id = "{\"title_id\": " + json::quote(title_id);
    std::string reply;
    std::string error;
    bool ok = true;
    if (kind == Kind::erase)
    {
        ok = call("/api/v1/games/delete", id + ", \"confirm\": true}", &reply, &error);
        json::Value started;
        json::parse(reply, &started);
        // Already gone (or on a drive not plugged in): nothing to wait for.
        bool active = ok && !started["source_missing"].bool_or(false);
        for (int poll = 0; active && poll < kMaxPolls; ++poll)
        {
            if (poll_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
            if (!call("/api/v1/games/storage/status", "{}", &reply, &error))
            {
                ok = false;
                break;
            }
            json::Value job;
            json::parse(reply, &job);
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                progress_.done_bytes =
                    static_cast<std::uint64_t>(job["processed_bytes"].number_or(0.0));
                progress_.total_bytes =
                    static_cast<std::uint64_t>(job["total_bytes"].number_or(0.0));
            }
            active = job["active"].bool_or(false);
            if (!active && job["result_status"].number_or(0.0) != 0.0)
            {
                ok = false;
                error = job["result_error"].string_or("the delete did not finish");
            }
        }
        if (active)
        {
            ok = false;
            error = "the delete is taking too long; ShadowMountPlus is still on it";
        }
    }
    // Off the home screen: what removing is, and what follows a delete. After
    // a delete ShadowMountPlus may have let the title go already, so a refusal
    // then changes nothing: the files are gone, which is what was asked.
    if (kind == Kind::home_screen)
        ok = call("/api/v1/games/uninstall", id + "}", &reply, &error);
    else if (ok)
    {
        std::string ignored;
        call("/api/v1/games/uninstall", id + "}", &reply, &ignored);
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        progress_.running = false;
        progress_.finished = true;
        progress_.ok = ok;
        progress_.error = ok ? std::string() : error;
    }
    finished_ = true;
    running_ = false;
}

} // namespace hui::orion
