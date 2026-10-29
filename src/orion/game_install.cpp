// ORION Media Center - PS5 games installed off a network drive: what, where to, and the job.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/game_install.hpp"

#include "core/save_file.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <utility>

namespace hui::orion
{

namespace
{

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool ends(std::string_view text, std::string_view end)
{
    return text.size() > end.size() && text.substr(text.size() - end.size()) == end;
}

bool digits(std::string_view text, std::size_t least, std::size_t most, int *number)
{
    if (text.size() < least || text.size() > most)
        return false;
    int value = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    *number = value;
    return true;
}

// The ways an archive comes in volumes, on a name in lower case: "x.part3.rar"
// (base "x", 3), "x.r07" (base "x", 7; its first volume is "x.rar") and
// "x.7z.002" (base "x.7z", 2; the same for .zip and .rar).
bool part_rar(const std::string &name, std::string *base, int *number)
{
    if (!ends(name, ".rar"))
        return false;
    const std::string stem = name.substr(0, name.size() - 4);
    const std::size_t part = stem.rfind(".part");
    if (part == std::string::npos || !digits(stem.substr(part + 5), 1, 4, number))
        return false;
    *base = stem.substr(0, part);
    return true;
}

bool old_rar(const std::string &name, std::string *base, int *number)
{
    const std::size_t n = name.size();
    if (n < 5 || name[n - 4] != '.' || name[n - 3] != 'r' ||
        !digits(std::string_view(name).substr(n - 2), 2, 2, number))
        return false;
    *base = name.substr(0, n - 4);
    return true;
}

bool split(const std::string &name, std::string *base, int *number)
{
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || !digits(std::string_view(name).substr(dot + 1), 3, 3, number))
        return false;
    const std::string stem = name.substr(0, dot);
    if (!ends(stem, ".7z") && !ends(stem, ".zip") && !ends(stem, ".rar"))
        return false;
    *base = stem;
    return true;
}

std::string last_part(std::string_view path)
{
    const std::size_t slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

bool exists(const std::string &path)
{
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0;
}

// Every file under a folder, as paths below it.
void files_under(const std::string &folder, const std::string &prefix,
                 std::vector<std::string> *files)
{
    for (const save::DirectoryEntry &entry : save::list_directory(folder))
    {
        const std::string path = prefix.empty() ? entry.name : prefix + "/" + entry.name;
        if (entry.directory)
            files_under(folder + "/" + entry.name, path, files);
        else
            files->push_back(path);
    }
}

} // namespace

sys::GameSourceKind install_kind(std::string_view name)
{
    const std::string low = lower(last_part(name));
    if (ends(low, ".ffpkg") || ends(low, ".ffpfs") || ends(low, ".exfat"))
        return sys::GameSourceKind::file;
    std::string base;
    int number = 0;
    if (part_rar(low, &base, &number) || split(low, &base, &number))
        return number == 1 ? sys::GameSourceKind::archive : sys::GameSourceKind::none;
    if (old_rar(low, &base, &number))
        return sys::GameSourceKind::none;
    if (ends(low, ".zip") || ends(low, ".7z") || ends(low, ".rar"))
        return sys::GameSourceKind::archive;
    return sys::GameSourceKind::none;
}

std::vector<std::string> archive_volumes(const std::vector<std::string> &names,
                                         const std::string &first)
{
    const std::string low = lower(first);
    std::string base;
    int number = 0;
    // Each name of the same kind and base, with its number.
    std::vector<std::pair<int, std::string>> found;
    const auto gather = [&](bool (*kind)(const std::string &, std::string *, int *))
    {
        for (const std::string &name : names)
        {
            std::string other;
            int at = 0;
            if (kind(lower(name), &other, &at) && other == base)
                found.emplace_back(at, name);
        }
    };
    if (part_rar(low, &base, &number))
        gather(part_rar);
    else if (split(low, &base, &number))
        gather(split);
    else if (ends(low, ".rar"))
    {
        // x.rar, then x.r00, x.r01...
        base = low.substr(0, low.size() - 4);
        gather(old_rar);
        found.emplace_back(-1, first);
    }
    if (found.empty())
        return {first};
    std::sort(found.begin(), found.end());
    std::vector<std::string> volumes;
    for (auto &[at, name] : found)
        volumes.push_back(std::move(name));
    return volumes;
}

bool game_root(const std::vector<std::string> &files, std::string *root)
{
    constexpr std::string_view kParam = "sce_sys/param.json";
    bool found = false;
    std::size_t best = 0;
    for (const std::string &file : files)
    {
        std::string folder;
        if (file == kParam)
            folder.clear();
        else if (ends(file, "/" + std::string(kParam)))
            folder = file.substr(0, file.size() - kParam.size() - 1);
        else
            continue;
        const std::size_t depth =
            folder.empty()
                ? 0
                : 1 + static_cast<std::size_t>(std::count(folder.begin(), folder.end(), '/'));
        if (!found || depth < best)
        {
            found = true;
            best = depth;
            *root = folder;
        }
    }
    return found;
}

std::string install_name(std::string_view source)
{
    std::string name = last_part(source);
    std::string base;
    int number = 0;
    std::string low = lower(name);
    if (split(low, &base, &number))
        name.resize(base.size());
    low = lower(name);
    if (part_rar(low, &base, &number))
        name.resize(base.size());
    low = lower(name);
    for (const std::string_view end : {".rar", ".zip", ".7z"})
    {
        if (ends(low, end))
            name.resize(name.size() - end.size());
    }
    for (char &c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
            c = '_';
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
        name.pop_back();
    while (!name.empty() && (name.front() == ' ' || name.front() == '.'))
        name.erase(name.begin());
    return name.empty() ? std::string("Game") : name;
}

std::vector<InstallPlace> install_places()
{
    std::vector<std::string> drives = {"/data", "/mnt/ext0", "/mnt/ext1"};
    for (int i = 0; i < 8; ++i)
        drives.push_back("/mnt/usb" + std::to_string(i));
    struct stat mnt = {};
    const bool have_mnt = ::stat("/mnt", &mnt) == 0;
    std::vector<InstallPlace> places;
    for (const std::string &drive : drives)
    {
        struct stat info = {};
        if (::stat(drive.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
            continue;
        // A drive slot with nothing in it is a folder of /mnt itself.
        if (drive != "/data" && have_mnt && info.st_dev == mnt.st_dev)
            continue;
        struct statvfs space = {};
        if (::statvfs(drive.c_str(), &space) != 0 || space.f_blocks == 0)
            continue;
        InstallPlace place;
        place.drive = drive;
        place.path = drive + "/homebrew";
        place.free =
            static_cast<std::uint64_t>(space.f_bavail) * static_cast<std::uint64_t>(space.f_frsize);
        places.push_back(std::move(place));
    }
    return places;
}

ShareListing::ShareListing(SmbLister list) : list_(std::move(list))
{
}

ShareListing::~ShareListing()
{
    wait();
}

void ShareListing::wait()
{
    if (worker_.joinable())
        worker_.join();
}

bool ShareListing::start(sys::SmbLogin login, std::string folder)
{
    if (running_.load())
        return false;
    wait();
    running_ = true;
    worker_ = std::thread(
        [this, login = std::move(login), folder = std::move(folder)]()
        {
            std::vector<sys::SmbEntry> entries;
            std::string error;
            if (!list_(login, folder, 0, 4000, &entries, &error) && error.empty())
                error = "no answer";
            std::stable_sort(entries.begin(), entries.end(),
                             [](const sys::SmbEntry &a, const sys::SmbEntry &b)
                             {
                                 if (a.directory != b.directory)
                                     return a.directory;
                                 return lower(a.path) < lower(b.path);
                             });
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                entries_ = std::move(entries);
                error_ = std::move(error);
                finished_ = true;
            }
            running_ = false;
        });
    return true;
}

bool ShareListing::take_finished(std::vector<sys::SmbEntry> *entries, std::string *error)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!finished_)
        return false;
    finished_ = false;
    *entries = std::move(entries_);
    *error = std::move(error_);
    return true;
}

GameInstall::GameInstall(Copier copy) : copy_(std::move(copy))
{
}

GameInstall::~GameInstall()
{
    progress_.cancel = true;
    wait();
}

void GameInstall::wait()
{
    if (worker_.joinable())
        worker_.join();
}

bool GameInstall::start(sys::GameSource source, std::string name, std::string place)
{
    if (running_.load())
        return false;
    wait();
    name_ = std::move(name);
    progress_.done = 0;
    progress_.total = 0;
    progress_.cancel = false;
    running_ = true;
    worker_ = std::thread(
        [this, source = std::move(source), place = std::move(place), name = name_]()
        {
            Result result = run(source, place, name);
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                result_ = std::move(result);
                finished_ = true;
            }
            running_ = false;
        });
    return true;
}

bool GameInstall::take_finished(Result *result)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!finished_)
        return false;
    finished_ = false;
    *result = result_;
    return true;
}

GameInstall::Result GameInstall::run(const sys::GameSource &source, const std::string &place,
                                     const std::string &name)
{
    Result result;
    // The game's folder, unpacked or copied whole, is made in here and moved
    // out once it is all there: beside the place, outside what ShadowMountPlus
    // watches (it would see a game half copied), when that is the same drive
    // (a move must be a rename); /data/homebrew can be a mount of its own,
    // and then it is made inside, hidden by its dot.
    save::ensure_directory(place);
    const std::string parent = place.substr(0, place.find_last_of('/'));
    struct stat here = {};
    struct stat above = {};
    const bool beside = ::stat(place.c_str(), &here) == 0 && ::stat(parent.c_str(), &above) == 0 &&
                        here.st_dev == above.st_dev;
    const std::string staging = (beside ? parent : place) + "/.orion-install";
    save::remove_tree(staging);
    if (!save::ensure_directory(staging))
    {
        result.problem = Problem::cannot_write;
        result.detail = place;
        return result;
    }
    std::string error;
    if (!copy_(source, staging, &progress_, &error))
    {
        save::remove_tree(staging);
        result.problem = progress_.cancel.load() ? Problem::stopped : Problem::copy;
        result.detail = error;
        return result;
    }
    std::vector<std::string> files;
    files_under(staging, "", &files);
    // What goes where: the game's folder, or the images in it.
    std::vector<std::pair<std::string, std::string>> moves;
    std::string root;
    if (source.kind != sys::GameSourceKind::file && game_root(files, &root))
    {
        const std::string folder = install_name(root.empty() ? name : last_part(root));
        moves.emplace_back(root.empty() ? staging : staging + "/" + root, place + "/" + folder);
    }
    else
    {
        for (const std::string &file : files)
        {
            if (install_kind(file) == sys::GameSourceKind::file)
                moves.emplace_back(staging + "/" + file, place + "/" + last_part(file));
        }
    }
    if (moves.empty())
    {
        save::remove_tree(staging);
        result.problem = Problem::no_game;
        return result;
    }
    for (const auto &[from, to] : moves)
    {
        if (exists(to))
        {
            save::remove_tree(staging);
            result.problem = Problem::already_there;
            result.detail = to;
            return result;
        }
    }
    for (const auto &[from, to] : moves)
    {
        if (std::rename(from.c_str(), to.c_str()) != 0)
        {
            save::remove_tree(staging);
            result.problem = Problem::cannot_write;
            result.detail = to + " (errno " + std::to_string(errno) + ")";
            return result;
        }
        save::open_up_tree(to);
        result.installed = to;
    }
    save::remove_tree(staging);
    result.ok = true;
    return result;
}

} // namespace hui::orion
