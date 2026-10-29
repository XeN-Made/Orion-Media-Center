// ORION Media Center - A PS5 game copied off a network drive, or unpacked from an archive on it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/game_copy.hpp"

#include "platform/ps5/smb_connection.hpp"

// smb2.h first: libsmb2.h uses its types.
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <langinfo.h>
#include <vector>

// libarchive asks the C library about the locale and local time, which the
// console's has not got. ORION has no locale but UTF-8 names, one byte per
// character as far as the C library's own conversions go (it writes no more
// than that), and file times are kept as they are, in UTC.
extern "C" int ___mb_cur_max(void)
{
    return 1;
}

extern "C" char *nl_langinfo(nl_item item)
{
    static char utf8[] = "UTF-8";
    static char none[] = "";
    return item == CODESET ? utf8 : none;
}

extern "C" struct tm *localtime_r(const time_t *when, struct tm *out)
{
    // Days since 1970 to a civil date (Howard Hinnant's algorithm).
    const long long seconds = static_cast<long long>(*when);
    long long days = seconds / 86400;
    long long rest = seconds % 86400;
    if (rest < 0)
    {
        rest += 86400;
        --days;
    }
    *out = {};
    out->tm_hour = static_cast<int>(rest / 3600);
    out->tm_min = static_cast<int>(rest / 60 % 60);
    out->tm_sec = static_cast<int>(rest % 60);
    out->tm_wday = static_cast<int>((days % 7 + 11) % 7); // 1970-01-01 was a Thursday
    const long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long day = doy - (153 * mp + 2) / 5 + 1;
    const long long month = mp < 10 ? mp + 3 : mp - 9;
    const long long year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    out->tm_mday = static_cast<int>(day);
    out->tm_mon = static_cast<int>(month - 1);
    out->tm_year = static_cast<int>(year - 1900);
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    static const int kBefore[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    out->tm_yday = kBefore[month - 1] + static_cast<int>(day) - 1 + (leap && month > 2 ? 1 : 0);
    return out;
}

namespace hui::sys
{

namespace
{

constexpr std::size_t kMostEntries = 200000; // files and folders in one game folder

bool stopped(const CopyProgress *progress)
{
    return progress != nullptr && progress->cancel.load();
}

void add_done(CopyProgress *progress, std::uint64_t bytes)
{
    if (progress != nullptr)
        progress->done += bytes;
}

// Every folder of a path made, like mkdir -p.
bool make_folders(const std::string &path)
{
    for (std::size_t at = 1; at <= path.size(); ++at)
    {
        if (at < path.size() && path[at] != '/')
            continue;
        const std::string part = path.substr(0, at);
        if (::mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

bool write_at(int fd, const void *data, std::size_t size, std::int64_t offset)
{
    const char *bytes = static_cast<const char *>(data);
    while (size > 0)
    {
        const ssize_t put = ::pwrite(fd, bytes, size, static_cast<off_t>(offset));
        if (put < 0 && errno == EINTR)
            continue;
        if (put <= 0)
            return false;
        bytes += put;
        size -= static_cast<std::size_t>(put);
        offset += put;
    }
    return true;
}

// An archive's name for a file, as a path under the folder it unpacks to:
// none that climbs out of it ("..", or a path from the root).
bool inside(const char *name, std::string *path)
{
    path->clear();
    if (name == nullptr)
        return false;
    std::string part;
    const std::string whole = name;
    for (std::size_t at = 0; at <= whole.size(); ++at)
    {
        const char c = at < whole.size() ? whole[at] : '/';
        if (c != '/' && c != '\\')
        {
            part += c;
            continue;
        }
        if (part == "..")
            return false;
        if (!part.empty() && part != ".")
            *path += (path->empty() ? "" : "/") + part;
        part.clear();
    }
    return !path->empty();
}

std::string last_part(const std::string &path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// One file of the share written to `to`.
bool copy_one(SmbConnection &smb, const std::string &from, const std::string &to,
              CopyProgress *progress, std::string *error)
{
    smb2fh *file = smb2_open(smb.get(), from.c_str(), O_RDONLY);
    if (file == nullptr)
    {
        *error = from + ": " + smb.said();
        return false;
    }
    const int fd = ::open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        smb2_close(smb.get(), file);
        *error = "cannot write " + to + " (errno " + std::to_string(errno) + ")";
        return false;
    }
    // As much as the server takes in one read, up to 1 MB.
    std::uint32_t chunk = smb2_get_max_read_size(smb.get());
    if (chunk == 0 || chunk > (1u << 20))
        chunk = 1u << 20;
    std::vector<std::uint8_t> buffer(chunk);
    std::int64_t at = 0;
    bool ok = true;
    for (;;)
    {
        if (stopped(progress))
        {
            *error = "stopped";
            ok = false;
            break;
        }
        const int got = smb2_read(smb.get(), file, buffer.data(), chunk);
        if (got < 0)
        {
            *error = from + ": " + smb.said();
            ok = false;
            break;
        }
        if (got == 0)
            break;
        if (!write_at(fd, buffer.data(), static_cast<std::size_t>(got), at))
        {
            *error = "could not write " + to + " (disk full?)";
            ok = false;
            break;
        }
        at += got;
        add_done(progress, static_cast<std::uint64_t>(got));
    }
    smb2_close(smb.get(), file);
    if (::close(fd) != 0 && ok)
    {
        *error = "could not finish " + to;
        ok = false;
    }
    return ok;
}

bool copy_folder(SmbConnection &smb, const std::string &folder, const std::string &into,
                 CopyProgress *progress, std::string *error)
{
    std::vector<SmbEntry> entries;
    if (!smb.list(folder, "", 64, kMostEntries, &entries, error))
        return false;
    std::uint64_t total = 0;
    for (const SmbEntry &entry : entries)
        total += entry.directory ? 0 : entry.size;
    if (progress != nullptr)
        progress->total = total;
    for (const SmbEntry &entry : entries)
    {
        const std::string to = into + "/" + entry.path;
        if (entry.directory)
        {
            if (!make_folders(to))
            {
                *error = "cannot make " + to;
                return false;
            }
            continue;
        }
        const std::size_t slash = to.find_last_of('/');
        if (!make_folders(to.substr(0, slash)) ||
            !copy_one(smb, folder.empty() ? entry.path : folder + "/" + entry.path, to, progress,
                      error))
        {
            if (error->empty())
                *error = "cannot make " + to.substr(0, slash);
            return false;
        }
    }
    return true;
}

// ---- archives, read off the share as libarchive asks ----

struct Volume;
struct Reading
{
    SmbConnection *smb = nullptr;
    CopyProgress *progress = nullptr;
    std::vector<std::uint8_t> buffer;
    std::string error;
};
struct Volume
{
    Reading *reading = nullptr;
    std::string path;
    smb2fh *file = nullptr;
};

int volume_open(archive *, void *data)
{
    Volume *volume = static_cast<Volume *>(data);
    if (volume->file != nullptr)
        return ARCHIVE_OK;
    volume->file = smb2_open(volume->reading->smb->get(), volume->path.c_str(), O_RDONLY);
    if (volume->file == nullptr)
    {
        volume->reading->error = volume->path + ": " + volume->reading->smb->said();
        return ARCHIVE_FATAL;
    }
    return ARCHIVE_OK;
}

int volume_close(archive *, void *data)
{
    Volume *volume = static_cast<Volume *>(data);
    if (volume->file != nullptr)
        smb2_close(volume->reading->smb->get(), volume->file);
    volume->file = nullptr;
    return ARCHIVE_OK;
}

la_ssize_t volume_read(archive *, void *data, const void **block)
{
    Volume *volume = static_cast<Volume *>(data);
    Reading *reading = volume->reading;
    if (stopped(reading->progress))
    {
        reading->error = "stopped";
        return -1;
    }
    if (volume->file == nullptr && volume_open(nullptr, data) != ARCHIVE_OK)
        return -1;
    const int got = smb2_read(reading->smb->get(), volume->file, reading->buffer.data(),
                              static_cast<std::uint32_t>(reading->buffer.size()));
    if (got < 0)
    {
        reading->error = volume->path + ": " + reading->smb->said();
        return -1;
    }
    *block = reading->buffer.data();
    add_done(reading->progress, static_cast<std::uint64_t>(got));
    return got;
}

la_int64_t volume_seek(archive *, void *data, la_int64_t offset, int whence)
{
    Volume *volume = static_cast<Volume *>(data);
    if (volume->file == nullptr && volume_open(nullptr, data) != ARCHIVE_OK)
        return ARCHIVE_FATAL;
    std::uint64_t now = 0;
    const std::int64_t at =
        smb2_lseek(volume->reading->smb->get(), volume->file, offset, whence, &now);
    return at < 0 ? ARCHIVE_FATAL : static_cast<la_int64_t>(at);
}

int volume_switch(archive *a, void *from, void *to)
{
    volume_close(a, from);
    return volume_open(a, to);
}

bool unpack(SmbConnection &smb, const std::vector<std::string> &volumes, const std::string &into,
            CopyProgress *progress, std::string *error)
{
    // The size to read is the volumes' sizes together.
    std::uint64_t total = 0;
    for (const std::string &path : volumes)
    {
        smb2_stat_64 info = {};
        if (smb2_stat(smb.get(), path.c_str(), &info) == 0)
            total += info.smb2_size;
    }
    if (progress != nullptr)
        progress->total = total;

    Reading reading;
    reading.smb = &smb;
    reading.progress = progress;
    std::uint32_t chunk = smb2_get_max_read_size(smb.get());
    if (chunk == 0 || chunk > (1u << 20))
        chunk = 1u << 20;
    reading.buffer.resize(chunk);
    std::vector<Volume> parts(volumes.size());
    for (std::size_t i = 0; i < volumes.size(); ++i)
    {
        parts[i].reading = &reading;
        parts[i].path = volumes[i];
    }

    archive *a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_format_7zip(a);
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);
    archive_read_support_filter_all(a);
    archive_read_set_open_callback(a, volume_open);
    archive_read_set_read_callback(a, volume_read);
    archive_read_set_seek_callback(a, volume_seek);
    archive_read_set_close_callback(a, volume_close);
    archive_read_set_switch_callback(a, volume_switch);
    // Several volumes read as one stream, in order (a .part2.rar after a .part1.rar).
    archive_read_set_callback_data(a, parts.data());
    for (std::size_t i = 1; i < parts.size(); ++i)
        archive_read_append_callback_data(a, &parts[i]);

    bool ok = archive_read_open1(a) == ARCHIVE_OK;
    std::vector<char> data;
    archive_entry *entry = nullptr;
    while (ok)
    {
        const int next = archive_read_next_header(a, &entry);
        if (next == ARCHIVE_EOF)
            break;
        if (next != ARCHIVE_OK && next != ARCHIVE_WARN)
        {
            ok = false;
            break;
        }
        std::string path;
        const char *name = archive_entry_pathname_utf8(entry);
        if (!inside(name != nullptr ? name : archive_entry_pathname(entry), &path))
            continue;
        const std::string to = into + "/" + path;
        const mode_t type = archive_entry_filetype(entry);
        if (type == AE_IFDIR)
        {
            if (!make_folders(to))
            {
                *error = "cannot make " + to;
                ok = false;
            }
            continue;
        }
        if (type != AE_IFREG)
            continue; // links and devices have no place in a game
        if (!make_folders(to.substr(0, to.find_last_of('/'))))
        {
            *error = "cannot make the folder of " + to;
            ok = false;
            break;
        }
        const int fd = ::open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0)
        {
            *error = "cannot write " + to + " (errno " + std::to_string(errno) + ")";
            ok = false;
            break;
        }
        for (;;)
        {
            const void *block = nullptr;
            std::size_t size = 0;
            la_int64_t offset = 0;
            const int got = archive_read_data_block(a, &block, &size, &offset);
            if (got == ARCHIVE_EOF)
                break;
            if (got != ARCHIVE_OK && got != ARCHIVE_WARN)
            {
                ok = false;
                break;
            }
            if (!write_at(fd, block, size, offset))
            {
                *error = "could not write " + to + " (disk full?)";
                ok = false;
                break;
            }
        }
        if (::close(fd) != 0 && ok)
        {
            *error = "could not finish " + to;
            ok = false;
        }
    }
    if (!ok && error->empty())
    {
        const char *said = archive_error_string(a);
        *error = !reading.error.empty() ? reading.error
                 : said != nullptr      ? std::string(said)
                                        : std::string("the archive could not be read");
    }
    archive_read_free(a);
    for (Volume &part : parts)
        volume_close(nullptr, &part);
    return ok;
}

} // namespace

bool copy_game(const GameSource &source, const std::string &into, CopyProgress *progress,
               std::string *error)
{
    error->clear();
    if (source.paths.empty())
    {
        *error = "nothing to copy";
        return false;
    }
    SmbConnection smb;
    if (!smb.open(source.login, error))
        return false;
    switch (source.kind)
    {
    case GameSourceKind::folder:
        return copy_folder(smb, source.paths[0], into, progress, error);
    case GameSourceKind::file:
    {
        smb2_stat_64 info = {};
        if (progress != nullptr && smb2_stat(smb.get(), source.paths[0].c_str(), &info) == 0)
            progress->total = info.smb2_size;
        return copy_one(smb, source.paths[0], into + "/" + last_part(source.paths[0]), progress,
                        error);
    }
    case GameSourceKind::archive:
        return unpack(smb, source.paths, into, progress, error);
    case GameSourceKind::none:
        break;
    }
    *error = "nothing to copy";
    return false;
}

} // namespace hui::sys
