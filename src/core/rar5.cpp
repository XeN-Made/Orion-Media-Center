// ORION Media Center - A RAR5 archive whose files are stored as they are: where each one's bytes
// lie. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier: GPL-3.0-or-later
//
// The layout is RARLAB's "RAR 5.0 archive format" technote: a signature, then
// blocks of <CRC32> <size> <type> <flags> [extra size] [data size] <fields>
// <extra area>, each followed by its data. Numbers are "vints": seven bits a
// byte, low first, the top bit set on all but the last.

#include "core/rar5.hpp"

#include "core/save_file.hpp"

#include <cstring>
#include <string_view>

namespace hui
{

namespace
{

constexpr char kSignature[] = "Rar!\x1a\x07\x01\x00";
constexpr std::size_t kSignatureSize = 8;
// The technote's limit for a header; anything larger is damage.
constexpr std::uint64_t kMostHeader = 2u << 20;
// A first read takes most headers whole: a file's name is the long part.
constexpr std::size_t kFirstRead = 4096;
constexpr std::size_t kMostFiles = 1000000;

enum Type : std::uint64_t
{
    kMain = 1,
    kFile = 2,
    kService = 3,
    kEncryption = 4,
    kEnd = 5,
};

enum Flags : std::uint64_t
{
    kHasExtra = 0x01,
    kHasData = 0x02,
    kSkipIfUnknown = 0x04,
    kSplitBefore = 0x08,
    kSplitAfter = 0x10,
};

// Reads numbers off a header's bytes; once one is cut short, every read fails.
struct Cursor
{
    std::string_view bytes;
    std::size_t at = 0;
    bool ok = true;

    std::uint64_t vint()
    {
        std::uint64_t value = 0;
        for (int shift = 0; ok && shift < 70; shift += 7)
        {
            if (at >= bytes.size())
                break;
            const auto byte = static_cast<unsigned char>(bytes[at++]);
            value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0)
                return value;
        }
        ok = false;
        return 0;
    }
    std::uint32_t u32()
    {
        if (!ok || bytes.size() - at < 4)
        {
            ok = false;
            return 0;
        }
        std::uint32_t value = 0;
        for (int i = 3; i >= 0; --i)
            value =
                (value << 8) | static_cast<unsigned char>(bytes[at + static_cast<std::size_t>(i)]);
        at += 4;
        return value;
    }
    std::string_view take(std::uint64_t size)
    {
        if (!ok || size > bytes.size() - at)
        {
            ok = false;
            return {};
        }
        const std::string_view part = bytes.substr(at, static_cast<std::size_t>(size));
        at += static_cast<std::size_t>(size);
        return part;
    }
};

// What a file's extra area says: encrypted, or a link (no place in a game).
void read_extra(std::string_view extra, bool *encrypted, bool *link)
{
    Cursor cursor{extra};
    while (cursor.ok && cursor.at < extra.size())
    {
        const std::uint64_t size = cursor.vint();
        const std::string_view record = cursor.take(size);
        Cursor inner{record};
        const std::uint64_t type = inner.vint();
        if (type == 0x01)
            *encrypted = true;
        else if (type == 0x05)
            *link = true;
    }
}

} // namespace

Rar5Plan plan_rar5(std::size_t volumes, const Rar5ReadAt &read_at, std::vector<Rar5File> *files,
                   std::string *why)
{
    files->clear();
    why->clear();
    if (volumes == 0)
    {
        *why = "no volumes";
        return Rar5Plan::unreadable;
    }
    std::string bytes;
    // The last file goes on in the next volume.
    bool split_open = false;
    for (std::size_t volume = 0; volume < volumes; ++volume)
    {
        if (!read_at(volume, 0, kSignatureSize, &bytes))
        {
            *why = "volume " + std::to_string(volume + 1) + " could not be read";
            return Rar5Plan::unreadable;
        }
        if (bytes.size() < kSignatureSize ||
            std::memcmp(bytes.data(), kSignature, kSignatureSize) != 0)
        {
            *why = "volume " + std::to_string(volume + 1) + " is not RAR5";
            return Rar5Plan::not_rar5;
        }
        std::uint64_t at = kSignatureSize;
        bool more_volumes = false;
        for (;;)
        {
            if (!read_at(volume, at, kFirstRead, &bytes))
            {
                *why = "a header could not be read";
                return Rar5Plan::unreadable;
            }
            if (bytes.empty())
            {
                // The volume ends without its end block: the next one, if any, goes on.
                more_volumes = true;
                break;
            }
            Cursor head{bytes};
            const std::uint32_t crc = head.u32();
            const std::size_t size_at = head.at;
            const std::uint64_t header_size = head.vint();
            if (!head.ok || header_size == 0 || header_size > kMostHeader)
            {
                *why = "a header is damaged";
                return Rar5Plan::damaged;
            }
            const std::uint64_t whole = head.at + header_size;
            if (whole > bytes.size())
            {
                if (!read_at(volume, at, static_cast<std::size_t>(whole), &bytes))
                {
                    *why = "a header could not be read";
                    return Rar5Plan::unreadable;
                }
                if (whole > bytes.size())
                {
                    *why = "a header is cut short";
                    return Rar5Plan::damaged;
                }
            }
            const std::string_view header(bytes.data(), static_cast<std::size_t>(whole));
            if (save::crc32(header.substr(size_at)) != crc)
            {
                *why = "a header fails its checksum";
                return Rar5Plan::damaged;
            }
            Cursor cursor{header, head.at};
            const std::uint64_t type = cursor.vint();
            const std::uint64_t flags = cursor.vint();
            const std::uint64_t extra_size = (flags & kHasExtra) != 0 ? cursor.vint() : 0;
            const std::uint64_t data_size = (flags & kHasData) != 0 ? cursor.vint() : 0;
            if (!cursor.ok || extra_size > header.size() - cursor.at)
            {
                *why = "a header is damaged";
                return Rar5Plan::damaged;
            }
            const std::string_view extra =
                header.substr(header.size() - static_cast<std::size_t>(extra_size));
            const std::uint64_t data_at = at + whole;
            if (type == kEncryption)
            {
                *why = "its headers are encrypted";
                return Rar5Plan::unpack;
            }
            if (type == kEnd)
            {
                const std::uint64_t end_flags = cursor.vint();
                more_volumes = (end_flags & 0x01) != 0;
                break;
            }
            if (type == kFile)
            {
                const std::uint64_t file_flags = cursor.vint();
                cursor.vint(); // its size unpacked: the data's, when stored
                cursor.vint(); // attributes
                if ((file_flags & 0x02) != 0)
                    cursor.u32(); // modified
                if ((file_flags & 0x04) != 0)
                    cursor.u32(); // its CRC32
                const std::uint64_t compression = cursor.vint();
                cursor.vint(); // the system it was made on
                const std::string_view name = cursor.take(cursor.vint());
                if (!cursor.ok)
                {
                    *why = "a file's header is damaged";
                    return Rar5Plan::damaged;
                }
                bool encrypted = false;
                bool link = false;
                read_extra(extra, &encrypted, &link);
                if (encrypted)
                {
                    *why = std::string(name) + " is encrypted";
                    return Rar5Plan::unpack;
                }
                const bool directory = (file_flags & 0x01) != 0;
                if (!directory && ((compression >> 7) & 0x07) != 0)
                {
                    *why = std::string(name) + " is compressed";
                    return Rar5Plan::unpack;
                }
                const bool split_before = (flags & kSplitBefore) != 0;
                if (split_before != split_open)
                {
                    *why = std::string(name) + (split_before
                                                    ? " goes on from a volume that is not there"
                                                    : " is missing its end");
                    return Rar5Plan::damaged;
                }
                split_open = (flags & kSplitAfter) != 0;
                if (split_before)
                {
                    // The rest of the file the last volume ended in.
                    if (files->empty() || files->back().name != name || files->back().directory)
                    {
                        *why = std::string(name) + " goes on from a volume that is not there";
                        return Rar5Plan::damaged;
                    }
                    files->back().pieces.push_back({volume, data_at, data_size});
                    files->back().size += data_size;
                }
                else if (!link)
                {
                    if (files->size() >= kMostFiles)
                    {
                        *why = "too many files";
                        return Rar5Plan::unpack;
                    }
                    Rar5File file;
                    file.name = std::string(name);
                    file.directory = directory;
                    if (!directory)
                    {
                        file.size = data_size;
                        file.pieces.push_back({volume, data_at, data_size});
                    }
                    files->push_back(std::move(file));
                }
            }
            else if (type != kMain && type != kService && (flags & kSkipIfUnknown) == 0)
            {
                *why = "a block of type " + std::to_string(type) + " it does not know";
                return Rar5Plan::unpack;
            }
            at = data_at + data_size;
        }
        if (!more_volumes && !split_open)
            return Rar5Plan::stored;
    }
    // Out of volumes: complete unless a file was to go on in the next one.
    if (split_open)
    {
        *why = "a volume is missing";
        return Rar5Plan::damaged;
    }
    return Rar5Plan::stored;
}

} // namespace hui
