// ORION Media Center - Wide covers: small copies of each title's 16:9 home-screen picture.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/wide_copies.hpp"

#include "core/save_file.hpp"

#include <cstdint>

namespace hui::orion
{

namespace
{

constexpr std::size_t kMaxPicture = 24u << 20; // one 4K home-screen picture

} // namespace

std::string encode_tga(const library::Image &image)
{
    std::string out(18, '\0');
    out[2] = 2; // uncompressed true colour
    out[12] = static_cast<char>(image.width & 0xff);
    out[13] = static_cast<char>((image.width >> 8) & 0xff);
    out[14] = static_cast<char>(image.height & 0xff);
    out[15] = static_cast<char>((image.height >> 8) & 0xff);
    out[16] = 32;
    out[17] = 0x28; // top row first, 8 bits of alpha
    out.reserve(out.size() + image.rgba.size());
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4)
    {
        // TGA keeps blue, green, red, alpha.
        out += static_cast<char>(image.rgba[i + 2]);
        out += static_cast<char>(image.rgba[i + 1]);
        out += static_cast<char>(image.rgba[i]);
        out += static_cast<char>(image.rgba[i + 3]);
    }
    return out;
}

WideCopies::~WideCopies()
{
    wait();
}

void WideCopies::wait()
{
    if (worker_.joinable())
        worker_.join();
}

int WideCopies::made() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return made_;
}

bool WideCopies::start(std::vector<std::pair<std::string, std::string>> pictures,
                       std::string folder)
{
    if (running_.load())
        return false;
    wait();
    running_ = true;
    worker_ = std::thread(
        [this, pictures = std::move(pictures), folder = std::move(folder)]
        {
            int made = 0;
            save::ensure_directory(folder);
            for (const auto &[id, path] : pictures)
            {
                std::string bytes;
                library::Image image;
                // Wide only: a square picture is no better than the icon.
                if (save::read_file(path, &bytes, kMaxPicture) &&
                    library::decode_png(bytes, kWideSize, &image) &&
                    image.width * 5 > image.height * 6 &&
                    save::write_atomic(folder + "/" + id + ".tga", encode_tga(image)).empty())
                    ++made;
                else
                    save::write_atomic(folder + "/" + id + ".none", "");
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                made_ = made;
            }
            running_ = false;
        });
    return true;
}

} // namespace hui::orion
