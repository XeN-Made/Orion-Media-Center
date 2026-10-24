// ORION Media Center - Wide covers: small copies of each title's 16:9 home-screen picture.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/icon.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hui::orion
{

// A tab whose covers are wide (Landscape, 7:5) shows a console title's
// home-screen picture (pic0.png, 16:9) instead of its square icon with the
// sides filled. Those pictures are 3840 x 2160: decoded, 33 MB each, and a
// few hundred milliseconds of work, too much for a tile coming into view.
// So each is made small once, on a thread of its own, as <folder>/<ID>.tga
// (the decoder reads TGA, and writing one needs no encoder); one that cannot
// be read gets <ID>.none.
//
//   WideCopies wide;
//   wide.start({{"PPSA08560", "/data/orion/backdrops/PPSA08560.png"}}, "/data/orion/wide");
class WideCopies
{
  public:
    static constexpr int kWideSize = 512; // the longer side of a copy, at most

    WideCopies() = default;
    ~WideCopies();
    WideCopies(const WideCopies &) = delete;
    WideCopies &operator=(const WideCopies &) = delete;

    // (title ID, its picture). False while a run is going.
    bool start(std::vector<std::pair<std::string, std::string>> pictures, std::string folder);
    bool running() const
    {
        return running_.load();
    }
    // Copies made by the run that ended last.
    int made() const;
    void wait();

  private:
    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    int made_ = 0;
};

// An image as an uncompressed 32-bit TGA (top row first), what WideCopies
// writes.
std::string encode_tga(const library::Image &image);

} // namespace hui::orion
