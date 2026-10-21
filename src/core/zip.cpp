// ORION Media Center - Reading a ZIP archive held in memory (stored and deflated entries).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/zip.hpp"

#include "third_party/stb/image.h" // stbi_zlib_decode_noheader_buffer: raw deflate

#include <cstdint>
#include <limits>
#include <vector>

namespace hui
{

namespace
{

constexpr std::uint32_t kEndOfDirectory = 0x06054b50;
constexpr std::uint32_t kDirectoryEntry = 0x02014b50;
constexpr std::uint32_t kLocalHeader = 0x04034b50;
// A file this big is not something ORION unpacks (its largest is ~35 MB).
constexpr std::uint32_t kMaxEntry = 256u << 20;

std::uint32_t u16(std::string_view bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at])) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + 1])) << 8;
}

std::uint32_t u32(std::string_view bytes, std::size_t at)
{
    return u16(bytes, at) | u16(bytes, at + 2) << 16;
}

bool fail(std::string *error, std::string why)
{
    if (error != nullptr)
        *error = std::move(why);
    return false;
}

} // namespace

bool unzip(std::string_view archive, const ZipVisitor &each, std::string *error)
{
    // The end-of-directory record is in the last 64 KiB (plus its own 22).
    if (archive.size() < 22)
        return fail(error, "not a ZIP archive");
    std::size_t end = std::string_view::npos;
    const std::size_t lowest = archive.size() > 65557 ? archive.size() - 65557 : 0;
    for (std::size_t at = archive.size() - 22;; --at)
    {
        if (u32(archive, at) == kEndOfDirectory)
        {
            end = at;
            break;
        }
        if (at == lowest)
            break;
    }
    if (end == std::string_view::npos)
        return fail(error, "not a ZIP archive");
    const std::uint32_t count = u16(archive, end + 10);
    std::size_t at = u32(archive, end + 16);
    if (count == 0xffff || at == 0xffffffff)
        return fail(error, "ZIP64 archives are not read");
    std::vector<char> out;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (at + 46 > archive.size() || u32(archive, at) != kDirectoryEntry)
            return fail(error, "a broken central directory");
        const std::uint32_t flags = u16(archive, at + 8);
        const std::uint32_t method = u16(archive, at + 10);
        const std::uint32_t packed = u32(archive, at + 20);
        const std::uint32_t size = u32(archive, at + 24);
        const std::uint32_t name_length = u16(archive, at + 28);
        const std::uint32_t extra_length = u16(archive, at + 30);
        const std::uint32_t comment_length = u16(archive, at + 32);
        const std::size_t local = u32(archive, at + 42);
        if (at + 46 + name_length > archive.size())
            return fail(error, "a broken central directory");
        const std::string name(archive.substr(at + 46, name_length));
        at += 46 + name_length + extra_length + comment_length;
        if (!name.empty() && name.back() == '/')
            continue; // a folder
        if ((flags & 1u) != 0)
            return fail(error, name + " is encrypted");
        if (size > kMaxEntry || packed > kMaxEntry)
            return fail(error, name + " is too big");
        if (local + 30 > archive.size() || u32(archive, local) != kLocalHeader)
            return fail(error, name + ": a broken local header");
        const std::size_t data = local + 30 + u16(archive, local + 26) + u16(archive, local + 28);
        if (data + packed > archive.size())
            return fail(error, name + " is cut short");
        const std::string_view stored = archive.substr(data, packed);
        if (method == 0)
        {
            if (packed != size)
                return fail(error, name + ": sizes disagree");
            if (!each(name, stored))
                return false;
            continue;
        }
        if (method != 8)
            return fail(error, name + ": compression method " + std::to_string(method));
        out.resize(size == 0 ? 1 : size);
        const int written = stbi_zlib_decode_noheader_buffer(
            out.data(), static_cast<int>(out.size()), stored.data(), static_cast<int>(packed));
        if (written < 0 || static_cast<std::uint32_t>(written) != size)
            return fail(error, name + " does not inflate");
        if (!each(name, std::string_view(out.data(), size)))
            return false;
    }
    return true;
}

} // namespace hui
