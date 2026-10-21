// ORION Media Center - Checked save container and atomic file writes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hui::save
{

namespace
{

constexpr char kMagic[4] = {'H', 'U', 'I', 'L'};
constexpr std::size_t kHeader = 12;
constexpr std::size_t kTrailer = 4;

void put_u16(std::string &out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xff));
    out.push_back(static_cast<char>(value >> 8));
}

void put_u32(std::string &out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 0xff));
}

std::uint32_t get_u32(std::string_view data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i)
        value =
            (value << 8) | static_cast<unsigned char>(data[offset + static_cast<std::size_t>(i)]);
    return value;
}

std::uint16_t get_u16(std::string_view data, std::size_t offset)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(data[offset]) |
                                      (static_cast<unsigned char>(data[offset + 1]) << 8));
}

std::string errno_text(const char *operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

} // namespace

std::uint32_t crc32(std::string_view data)
{
    std::uint32_t crc = 0xffffffffu;
    for (char c : data)
    {
        crc ^= static_cast<unsigned char>(c);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::string encode(Kind kind, std::uint16_t version, std::string_view payload)
{
    std::string out(kMagic, sizeof(kMagic));
    put_u16(out, version);
    put_u16(out, static_cast<std::uint16_t>(kind));
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    out.append(payload);
    put_u32(out, crc32(out));
    return out;
}

Decoded decode(Kind kind, std::string_view data)
{
    Decoded result;
    if (data.size() < kHeader + kTrailer || std::memcmp(data.data(), kMagic, 4) != 0)
    {
        result.error = "not an ORION Media Center save";
        return result;
    }
    if (get_u16(data, 6) != static_cast<std::uint16_t>(kind))
    {
        result.error = "unexpected save kind";
        return result;
    }
    const std::uint32_t size = get_u32(data, 8);
    if (data.size() != kHeader + size + kTrailer)
    {
        result.error = "save size mismatch";
        return result;
    }
    if (crc32(data.substr(0, kHeader + size)) != get_u32(data, kHeader + size))
    {
        result.error = "save checksum mismatch";
        return result;
    }
    result.ok = true;
    result.version = get_u16(data, 4);
    result.payload.assign(data.substr(kHeader, size));
    return result;
}

std::string write_atomic(const std::string &path, std::string_view data)
{
    const std::string temporary = path + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return errno_text("open");
    std::size_t written = 0;
    while (written < data.size())
    {
        const ssize_t result = ::write(fd, data.data() + written, data.size() - written);
        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            const std::string error = errno_text("write");
            ::close(fd);
            ::unlink(temporary.c_str());
            return error;
        }
        written += static_cast<std::size_t>(result);
    }
    if (::fsync(fd) != 0)
    {
        const std::string error = errno_text("fsync");
        ::close(fd);
        ::unlink(temporary.c_str());
        return error;
    }
    if (::close(fd) != 0)
    {
        ::unlink(temporary.c_str());
        return errno_text("close");
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0)
    {
        // Some filesystems refuse to replace an existing file.
        ::unlink(path.c_str());
        if (::rename(temporary.c_str(), path.c_str()) != 0)
        {
            const std::string error = errno_text("rename");
            ::unlink(temporary.c_str());
            return error;
        }
    }
    return {};
}

bool read_file(const std::string &path, std::string *data, std::size_t max_bytes)
{
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    std::string buffer;
    char chunk[4096];
    for (;;)
    {
        const ssize_t result = ::read(fd, chunk, sizeof(chunk));
        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            ::close(fd);
            return false;
        }
        if (result == 0)
            break;
        buffer.append(chunk, static_cast<std::size_t>(result));
        if (buffer.size() > max_bytes)
        {
            ::close(fd);
            return false;
        }
    }
    ::close(fd);
    *data = std::move(buffer);
    return true;
}

bool ensure_directory(const std::string &path)
{
    if (::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST)
    {
        struct stat info
        {
        };
        return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
    }
    return false;
}

std::vector<std::string> list_files(const std::string &directory)
{
    std::vector<std::string> names;
    std::string index;
    if (read_file(directory + "/index.txt", &index, 1u << 20))
    {
        std::size_t start = 0;
        while (start < index.size())
        {
            std::size_t end = index.find('\n', start);
            if (end == std::string::npos)
                end = index.size();
            std::string name = index.substr(start, end - start);
            if (!name.empty() && name.back() == '\r')
                name.pop_back();
            if (!name.empty() && name.find('/') == std::string::npos && name != "index.txt")
                names.push_back(std::move(name));
            start = end + 1;
        }
        return names;
    }
    for (DirectoryEntry &entry : list_directory(directory))
    {
        if (entry.name != "index.txt")
            names.push_back(std::move(entry.name));
    }
    return names;
}

std::vector<DirectoryEntry> list_directory(const std::string &directory, ListingStats *stats)
{
    ListingStats local;
    ListingStats &seen = stats != nullptr ? *stats : local;
    seen = ListingStats();
    std::vector<DirectoryEntry> entries;
    const auto add = [&](const char *name, std::size_t length, unsigned type)
    {
        ++seen.records;
        DirectoryEntry entry;
        entry.name.assign(name, length);
        if (entry.name == "." || entry.name == "..")
            return;
        if (type == DT_DIR || type == DT_REG)
        {
            entry.directory = type == DT_DIR;
        }
        else
        {
            // DT_UNKNOWN (some file systems leave the type out) or a link.
            struct stat info
            {
            };
            if (::stat((directory + "/" + entry.name).c_str(), &info) != 0)
            {
                if (seen.stat_failures++ == 0)
                    seen.stat_error = errno;
                return;
            }
            entry.directory = S_ISDIR(info.st_mode);
        }
        entries.push_back(std::move(entry));
    };
#if defined(__PROSPERO__)
    const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
    {
        seen.open_error = errno;
        return entries;
    }
    // Records are FreeBSD 11 dirents: fileno (u32), reclen (u16), type (u8),
    // namlen (u8), then the name. getdents refuses (EINVAL) a buffer smaller
    // than the file system's I/O block: 64 KiB on the console's /data, where
    // 16 KiB returned nothing. Start at the block size and grow on refusal.
    constexpr std::size_t kMaxBuffer = 1u << 20;
    struct stat folder
    {
    };
    std::size_t size = 64u << 10;
    if (::fstat(fd, &folder) == 0 && folder.st_blksize > 0)
        size = std::max(size, static_cast<std::size_t>(folder.st_blksize));
    std::vector<std::uint64_t> storage((size + 7) / 8); // 8-byte aligned records
    for (;;)
    {
        char *buffer = reinterpret_cast<char *>(storage.data());
        const int got = ::getdents(fd, buffer, static_cast<int>(storage.size() * 8));
        ++seen.reads;
        if (got < 0 && errno == EINVAL && storage.size() * 8 < kMaxBuffer)
        {
            storage.resize(storage.size() * 2); // nothing was consumed: retry larger
            continue;
        }
        if (got < 0)
            seen.read_error = errno;
        if (got <= 0)
            break;
        for (int at = 0; at < got;)
        {
            const dirent *record = reinterpret_cast<const dirent *>(buffer + at);
            if (record->d_reclen == 0)
                break;
            if (record->d_fileno != 0)
                add(record->d_name, record->d_namlen, record->d_type);
            else
                ++seen.empty_records;
            at += record->d_reclen;
        }
    }
    ::close(fd);
#else
    DIR *dir = ::opendir(directory.c_str());
    if (dir == nullptr)
    {
        seen.open_error = errno;
        return entries;
    }
    while (const dirent *record = ::readdir(dir))
        add(record->d_name, std::strlen(record->d_name), record->d_type);
    ::closedir(dir);
#endif
    std::sort(entries.begin(), entries.end(),
              [](const DirectoryEntry &a, const DirectoryEntry &b) { return a.name < b.name; });
    return entries;
}

} // namespace hui::save
